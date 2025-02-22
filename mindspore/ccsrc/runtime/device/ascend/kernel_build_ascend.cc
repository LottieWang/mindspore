/**
 * Copyright 2019-2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "runtime/device/ascend/kernel_build_ascend.h"

#include <vector>
#include <string>
#include <memory>
#include <set>
#include <map>
#include "runtime/device/ascend/kernel_select_ascend.h"
#include "runtime/device/kernel_info.h"
#include "backend/kernel_compiler/kernel.h"
#include "backend/kernel_compiler/tbe/ascend_kernel_compile.h"
#include "backend/kernel_compiler/tbe/tbe_kernel_parallel_build.h"
#include "backend/kernel_compiler/akg/ascend/akg_ascend_kernel_build.h"
#include "backend/kernel_compiler/aicpu/aicpu_kernel_build.h"
#include "backend/kernel_compiler/host/host_kernel_build.h"
#include "backend/kernel_compiler/hccl/hccl_kernel_build.h"
#include "backend/kernel_compiler/rts/rt_kernel_build.h"
#include "backend/kernel_compiler/tbe/tbe_utils.h"
#include "backend/kernel_compiler/common_utils.h"
#include "frontend/operator/ops.h"
#include "backend/session/anf_runtime_algorithm.h"

namespace mindspore {
namespace device {
namespace ascend {
using mindspore::kernel::tbe::TbeUtils;
using std::make_shared;
constexpr size_t kMaxAttrMemListSize = 192;

static kernel::KernelModPtr SerialCompileImpl(const AnfNodePtr &anf_node) {
  kernel::KernelModPtr kernel_mod_ptr = nullptr;
  KernelType kernel_type = AnfAlgo::GetKernelType(anf_node);
  switch (kernel_type) {
    case KernelType::AICPU_KERNEL: {
      kernel_mod_ptr = kernel::AicpuOpBuild(anf_node);
      break;
    }
    case KernelType::HOST_KERNEL: {
      kernel_mod_ptr = kernel::HostOpBuild(anf_node);
      break;
    }
    case KernelType::RT_KERNEL: {
      kernel_mod_ptr = kernel::RtOpBuild(anf_node);
      break;
    }
    case KernelType::HCCL_KERNEL: {
      kernel_mod_ptr = kernel::HcclOpBuild(anf_node);
      break;
    }
    default: {
      MS_LOG(EXCEPTION) << "node [" << anf_node->DebugString() << "] Unsupported kernel_type:" << kernel_type;
    }
  }
  return kernel_mod_ptr;
}

static bool KernelBuildParallelCompile(const std::vector<CNodePtr> &kernels) {
  std::vector<AnfNodePtr> tbe_nodes;
  std::vector<AnfNodePtr> akg_nodes;
  std::vector<AnfNodePtr> other_nodes;
  for (const auto &anf_node : kernels) {
    MS_EXCEPTION_IF_NULL(anf_node);
    if (!AnfAlgo::IsRealKernel(anf_node)) {
      continue;
    }
    KernelType kernel_type = AnfAlgo::GetKernelType(anf_node);
    switch (kernel_type) {
      case KernelType::TBE_KERNEL: {
        if (AnfAlgo::GetKernelMod(anf_node) == nullptr) {
          tbe_nodes.push_back(anf_node);
        }
        break;
      }
      case KernelType::AKG_KERNEL: {
        akg_nodes.push_back(anf_node);
        break;
      }
      default: {
        other_nodes.push_back(anf_node);
        break;
      }
    }
  }
  bool tbe_ret = true;
  bool akg_ret = true;
  auto bin_map = kernel::tbe::KernelMeta::GetInstance();
  if (!tbe_nodes.empty()) {
    std::string old_build = common::GetEnv("MS_OLD_BUILD_PROCESS");
    if (!old_build.empty()) {
      tbe_ret = kernel::TbeOpParallelBuild(tbe_nodes);
    } else {
      auto build_manager = kernel::ascend::AscendKernelCompileManager::GetInstance();
      MS_EXCEPTION_IF_NULL(build_manager);
      build_manager->ResetOldTask();
      tbe_ret = build_manager->AscendSingleOpCompile(tbe_nodes);
    }
    auto config_path = TbeUtils::GetOpDebugPath();
    std::string dir = config_path + "kernel_meta/";
    (void)bin_map->ReadIndex(dir);
  }
  if (!akg_nodes.empty()) {
    kernel::AkgAscendKernelBuilder akg_ascend_kernel_builder;
    akg_ret = akg_ascend_kernel_builder.AkgKernelParallelBuild(akg_nodes);
    (void)bin_map->ReadIndex(kernel::kCceKernelMeta);
  }
  for (const auto &anf_node : other_nodes) {
    kernel::KernelModPtr kernel_mod_ptr = SerialCompileImpl(anf_node);
    MS_EXCEPTION_IF_NULL(kernel_mod_ptr);
    AnfAlgo::SetKernelMod(kernel_mod_ptr, anf_node.get());
  }
  return tbe_ret && akg_ret;
}

static std::vector<size_t> CalCleanZerosSize(const CNodePtr &pre_node) {
  MS_EXCEPTION_IF_NULL(pre_node);
  auto kernel_mod = AnfAlgo::GetKernelMod(pre_node);
  MS_EXCEPTION_IF_NULL(kernel_mod);
  std::vector<size_t> clean_size_list;
  constexpr size_t kAlignBytes = 32 - 1;
  // clean output
  if (AnfAlgo::HasNodeAttr(kAttrAtomicOutputIndexs, pre_node)) {
    auto output_indexs = AnfAlgo::GetNodeAttr<std::vector<size_t>>(pre_node, kAttrAtomicOutputIndexs);
    auto output_men_size = kernel_mod->GetOutputSizeList();
    for (auto index : output_indexs) {
      auto clean_item = (output_men_size.at(index) + kMemAlignSize + kAlignBytes) / kMemAlignSize * kMemAlignSize;
      clean_size_list.emplace_back(clean_item);
    }
  }
  // clean workspace
  if (AnfAlgo::HasNodeAttr(kAttrAtomicWorkspaceIndexs, pre_node)) {
    auto workspace_indexs = AnfAlgo::GetNodeAttr<std::vector<size_t>>(pre_node, kAttrAtomicWorkspaceIndexs);
    auto workspace_men_sizes = kernel_mod->GetWorkspaceSizeList();
    for (const auto &index : workspace_indexs) {
      auto clean_item = (workspace_men_sizes.at(index) + kMemAlignSize + kAlignBytes) / kMemAlignSize * kMemAlignSize;
      clean_size_list.emplace_back(clean_item);
    }
  }
  MS_LOG(INFO) << "clear output size:" << clean_size_list.size() << ",pre_node:" << pre_node->fullname_with_scope();
  return clean_size_list;
}

static void AddTbeClearZeroNode(mindspore::session::KernelGraph *const kernel_graph,
                                const mindspore::CNodePtr &pre_node, std::vector<mindspore::CNodePtr> *new_nodes) {
  MS_EXCEPTION_IF_NULL(kernel_graph);
  MS_EXCEPTION_IF_NULL(pre_node);
  MS_EXCEPTION_IF_NULL(new_nodes);
  auto clear_zero_prim = std::make_shared<Primitive>(kAtomicAddrCleanOpName);
  MS_EXCEPTION_IF_NULL(clear_zero_prim);
  auto new_value_node = NewValueNode(clear_zero_prim);
  MS_EXCEPTION_IF_NULL(new_value_node);
  std::vector<AnfNodePtr> inputs = {new_value_node};
  inputs.push_back(pre_node);
  CNodePtr clear_zero = kernel_graph->NewCNode(inputs);
  MS_EXCEPTION_IF_NULL(clear_zero);
  AbstractBasePtr abstract = std::make_shared<abstract::AbstractNone>();
  MS_EXCEPTION_IF_NULL(abstract);
  clear_zero->set_abstract(abstract);
  auto builder = std::make_shared<kernel::KernelBuildInfo::KernelBuildInfoBuilder>();
  builder->SetKernelType(KernelType::TBE_KERNEL);
  AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), clear_zero.get());
  auto clean_size = CalCleanZerosSize(pre_node);
  AnfAlgo::SetNodeAttr(kAttrAtomicAddMemSize, MakeValue(clean_size), clear_zero);
  AnfAlgo::SetStreamDistinctionLabel(AnfAlgo::GetStreamDistinctionLabel(pre_node.get()), clear_zero.get());
  new_nodes->push_back(clear_zero);
}

static void AddFusionTbeClearZeroNode(mindspore::session::KernelGraph *const kernel_graph,
                                      const mindspore::CNodePtr &first_clear_node,
                                      const std::vector<AnfNodePtr> &fusion_clear_inputs,
                                      const std::vector<size_t> &clean_size_list,
                                      std::vector<mindspore::CNodePtr> *new_nodes) {
  MS_EXCEPTION_IF_NULL(first_clear_node);
  auto clear_zero_prim = std::make_shared<Primitive>(kAtomicAddrCleanOpName);
  MS_EXCEPTION_IF_NULL(clear_zero_prim);
  auto new_value_node = NewValueNode(clear_zero_prim);
  MS_EXCEPTION_IF_NULL(new_value_node);
  std::vector<AnfNodePtr> inputs = {new_value_node};
  inputs.insert(inputs.end(), fusion_clear_inputs.begin(), fusion_clear_inputs.end());
  CNodePtr clear_zero = kernel_graph->NewCNode(inputs);
  MS_EXCEPTION_IF_NULL(clear_zero);
  AbstractBasePtr abstract = std::make_shared<abstract::AbstractNone>();
  MS_EXCEPTION_IF_NULL(abstract);
  clear_zero->set_abstract(abstract);
  auto builder = std::make_shared<kernel::KernelBuildInfo::KernelBuildInfoBuilder>();
  builder->SetKernelType(KernelType::TBE_KERNEL);
  AnfAlgo::SetSelectKernelBuildInfo(builder->Build(), clear_zero.get());
  AnfAlgo::SetNodeAttr(kAttrAtomicAddMemSize, MakeValue(clean_size_list), clear_zero);
  AnfAlgo::SetStreamDistinctionLabel(AnfAlgo::GetStreamDistinctionLabel(first_clear_node.get()), clear_zero.get());
  auto it = std::find(new_nodes->begin(), new_nodes->end(), first_clear_node);
  if (it != new_nodes->end()) {
    new_nodes->insert(it, clear_zero);
  } else {
    new_nodes->insert(new_nodes->begin(), clear_zero);
  }
}

static bool IsAtomicNode(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  auto kernel_mod = AnfAlgo::GetKernelMod(kernel_node);
  MS_EXCEPTION_IF_NULL(kernel_mod);
  auto parameters_indexs = kernel_mod->GenParameters();
  if (parameters_indexs.empty()) {
    return false;
  }
  if (AnfAlgo::IsDynamicShape(kernel_node)) {
    if (parameters_indexs.at(0) == 1) {
      (void)parameters_indexs.erase(parameters_indexs.begin());
    } else {
      parameters_indexs.pop_back();
    }
  }
  size_t input_num = AnfAlgo::GetInputTensorNum(kernel_node);
  size_t output_num = AnfAlgo::GetOutputTensorNum(kernel_node);
  size_t workspace_num = kernel_mod->GetWorkspaceSizeList().size();
  size_t param_num = parameters_indexs.size();
  size_t total_num = input_num + output_num + workspace_num;
  size_t pad_index = param_num;

  for (; pad_index < total_num; ++pad_index) {
    parameters_indexs.emplace_back(0);
  }

  for (size_t j = 0; j < input_num; ++j) {
    if (parameters_indexs.at(j) == 1) {
      MS_LOG(EXCEPTION) << "Atomic addr clean doesn't support clean input address, input index: " << j;
    }
  }

  if (parameters_indexs.size() < total_num) {
    MS_LOG(EXCEPTION) << "parameters indexes size: " << parameters_indexs.size()
                      << " less than total num: " << total_num;
  }
  // process output
  std::vector<size_t> output_indexs = {};
  if (AnfAlgo::HasNodeAttr(kAttrAtomicOutputIndexs, kernel_node)) {
    output_indexs = AnfAlgo::GetNodeAttr<std::vector<size_t>>(kernel_node, kAttrAtomicOutputIndexs);
  }

  for (size_t i = 0; i < output_num; ++i) {
    auto param_output = parameters_indexs.at(input_num + i);
    if (param_output == 1) {
      output_indexs.emplace_back(i);
      MS_LOG(INFO) << "Atomic clear output index: " << i;
    }
  }

  if (!output_indexs.empty()) {
    std::set<size_t> s(output_indexs.begin(), output_indexs.end());
    output_indexs.assign(s.begin(), s.end());
    AnfAlgo::SetNodeAttr(kAttrAtomicOutputIndexs, MakeValue(output_indexs), kernel_node);
  }
  // process workspace
  std::vector<size_t> workspace_indexs = {};
  for (size_t k = 0; k < workspace_num; ++k) {
    auto param_workspace = parameters_indexs.at(input_num + output_num + k);
    if (param_workspace == 1) {
      workspace_indexs.emplace_back(k);
      MS_LOG(INFO) << "Atomic clear workspace index: " << k;
    }
  }
  if (!workspace_indexs.empty()) {
    AnfAlgo::SetNodeAttr(kAttrAtomicWorkspaceIndexs, MakeValue(workspace_indexs), kernel_node);
  }
  return !(workspace_indexs.empty() && output_indexs.empty());
}

bool KernelBuild(const std::vector<CNodePtr> &kernels) {
  TbeUtils::LoadCache();
  return device::ascend::KernelBuildParallelCompile(kernels);
}

std::map<AnfNodePtr, std::vector<size_t>> GetCommunicationOpInputInfo(
  const mindspore::session::KernelGraph *kernel_graph) {
  std::map<AnfNodePtr, std::vector<size_t>> comm_input_info_map;
  for (auto &kernel : kernel_graph->execution_order()) {
    auto input_num = AnfAlgo::GetInputTensorNum(kernel);
    if (mindspore::session::AnfRuntimeAlgorithm::IsCommunicationOp(kernel)) {
      for (size_t i = 0; i < input_num; i++) {
        auto input_node = kernel->input(i + 1);
        auto kernel_input = AnfAlgo::VisitKernelWithReturnType(input_node, 0, true);
        if (!kernel_input.first->isa<CNode>()) {
          continue;
        }
        auto cnode = kernel_input.first->cast<CNodePtr>();
        if (AnfAlgo::IsCommunicationOp(cnode) || AnfAlgo::IsIndependentNode(cnode) ||
            AnfAlgo::GetCNodeName(cnode) == kGetNextOpName) {
          // no need to add atomic for communication/independent/getnext op 's output
          MS_LOG(INFO) << "No need to add atomic clean for op " << kernel_input.first->fullname_with_scope()
                       << "'s output";
          continue;
        }
        MS_LOG(INFO) << "Add atomic clean for single communication op input, comm:" << kernel->fullname_with_scope()
                     << " input_node: " << kernel_input.first->fullname_with_scope()
                     << " index: " << kernel_input.second;
        auto iter = comm_input_info_map.find(kernel_input.first);
        if (iter != comm_input_info_map.end()) {
          iter->second.push_back(kernel_input.second);
        } else {
          std::vector<size_t> indexes = {kernel_input.second};
          comm_input_info_map[kernel_input.first] = indexes;
        }
      }
    }
  }

  // remove duplicate index
  for (auto &info : comm_input_info_map) {
    std::set<size_t> s(info.second.begin(), info.second.end());
    info.second.assign(s.begin(), s.end());
  }

  return comm_input_info_map;
}

bool IsNeedClearZeroNodeFusion(const size_t clean_total_num, const mindspore::CNodePtr &first_node,
                               const mindspore::CNodePtr &current_node) {
  if (first_node == nullptr || current_node == nullptr) {
    return false;
  }
  auto first_graph_id = AnfAlgo::GetGraphId(first_node.get());
  auto current_graph_id = AnfAlgo::GetGraphId(current_node.get());
  if (clean_total_num >= kMaxAttrMemListSize || first_graph_id != current_graph_id) {
    return true;
  }
  return false;
}

static void TbeClearZeroNodeFusion(mindspore::session::KernelGraph *const kernel_graph) {
  std::vector<CNodePtr> new_nodes;
  std::vector<size_t> clean_size_list;
  std::vector<AnfNodePtr> fusion_clear_inputs;
  CNodePtr first_node = nullptr;
  std::map<AnfNodePtr, std::vector<size_t>> comm_input_info_map = GetCommunicationOpInputInfo(kernel_graph);
  for (const auto &anf_node : kernel_graph->execution_order()) {
    std::string apply_function_name = AnfAlgo::GetCNodeName(anf_node);
    bool is_comm_input = false;
    // set communication input output index attr
    if (comm_input_info_map.find(anf_node) != comm_input_info_map.end()) {
      auto indexes = comm_input_info_map[anf_node];
      AnfAlgo::SetNodeAttr(kAttrAtomicOutputIndexs, MakeValue(indexes), anf_node);
      is_comm_input = true;
    }

    if (apply_function_name == prim::kPrimMaxPoolGrad->name() &&
        AnfAlgo::GetKernelType(anf_node) == KernelType::AKG_KERNEL) {
      auto clear_zero_prim = std::make_shared<Primitive>(kClearZeroOpName);
      MS_EXCEPTION_IF_NULL(clear_zero_prim);
      auto new_value_node = NewValueNode(clear_zero_prim);
      MS_EXCEPTION_IF_NULL(new_value_node);
      std::vector<AnfNodePtr> inputs = {new_value_node};
      inputs.push_back(anf_node);
      CNodePtr clear_zero = kernel_graph->NewCNode(inputs);
      MS_EXCEPTION_IF_NULL(clear_zero);
      auto kernel_info = std::make_shared<device::KernelInfo>();
      MS_EXCEPTION_IF_NULL(kernel_info);
      clear_zero->set_kernel_info(kernel_info);
      AbstractBasePtr abstract = std::make_shared<abstract::AbstractNone>();
      MS_EXCEPTION_IF_NULL(abstract);
      AnfAlgo::SetNodeAttr("input_names", MakeValue(std::vector<std::string>({"x"})), clear_zero);
      SelectKernelInfo(clear_zero);
      // set the distinction label of clear same with anf
      AnfAlgo::SetStreamDistinctionLabel(AnfAlgo::GetStreamDistinctionLabel(anf_node.get()), clear_zero.get());
      new_nodes.push_back(clear_zero);
    } else if (is_comm_input ||
               (AnfAlgo::GetKernelType(anf_node) == KernelType::TBE_KERNEL && IsAtomicNode(anf_node))) {
      auto clean_sizes = CalCleanZerosSize(anf_node);
      if (!clean_sizes.empty()) {
        auto clean_total_num = clean_size_list.size() + clean_sizes.size();
        if (IsNeedClearZeroNodeFusion(clean_total_num, first_node, anf_node)) {
          // create clean node
          AddFusionTbeClearZeroNode(kernel_graph, first_node, fusion_clear_inputs, clean_size_list, &new_nodes);
          clean_size_list.clear();
          fusion_clear_inputs.clear();
          first_node = nullptr;
        }
        if (fusion_clear_inputs.empty()) {
          first_node = anf_node;
        }
        clean_size_list.insert(clean_size_list.end(), clean_sizes.begin(), clean_sizes.end());
        fusion_clear_inputs.emplace_back(anf_node);
        MS_LOG(DEBUG) << "fusion_clear_inputs size: " << fusion_clear_inputs.size()
                      << ", clean_size_list: " << clean_size_list.size();
      }
    }
    new_nodes.emplace_back(anf_node);
  }

  if (!fusion_clear_inputs.empty() && !clean_size_list.empty()) {
    // create clean node
    AddFusionTbeClearZeroNode(kernel_graph, first_node, fusion_clear_inputs, clean_size_list, &new_nodes);
  }
  kernel_graph->set_execution_order(new_nodes);
}

void KernelBuildPreprocess(mindspore::session::KernelGraph *kernel_graph) {
  MS_EXCEPTION_IF_NULL(kernel_graph);
  static const auto enable_fusion_clear = (common::GetEnv("ENV_FUSION_CLEAR") == "1");
  bool is_dynamic_graph = kernel_graph->is_dynamic_shape();
  if (!is_dynamic_graph && enable_fusion_clear) {
    TbeClearZeroNodeFusion(kernel_graph);
  } else {
    std::vector<CNodePtr> new_nodes;
    std::map<AnfNodePtr, std::vector<size_t>> comm_input_info_map = GetCommunicationOpInputInfo(kernel_graph);
    for (const auto &anf_node : kernel_graph->execution_order()) {
      std::string apply_function_name = AnfAlgo::GetCNodeName(anf_node);
      bool is_comm_input = false;
      if (comm_input_info_map.find(anf_node) != comm_input_info_map.end()) {
        auto indexes = comm_input_info_map[anf_node];
        AnfAlgo::SetNodeAttr(kAttrAtomicOutputIndexs, MakeValue(indexes), anf_node);
        is_comm_input = true;
      }

      if (is_comm_input) {
        AddTbeClearZeroNode(kernel_graph, anf_node, &new_nodes);
      } else if (apply_function_name == prim::kPrimMaxPoolGrad->name() &&
                 AnfAlgo::GetKernelType(anf_node) == KernelType::AKG_KERNEL) {
        auto clear_zero_prim = std::make_shared<Primitive>(kClearZeroOpName);
        MS_EXCEPTION_IF_NULL(clear_zero_prim);
        auto new_value_node = NewValueNode(clear_zero_prim);
        MS_EXCEPTION_IF_NULL(new_value_node);
        std::vector<AnfNodePtr> inputs = {new_value_node};
        inputs.push_back(anf_node);
        CNodePtr clear_zero = kernel_graph->NewCNode(inputs);
        MS_EXCEPTION_IF_NULL(clear_zero);
        auto kernel_info = std::make_shared<device::KernelInfo>();
        MS_EXCEPTION_IF_NULL(kernel_info);
        clear_zero->set_kernel_info(kernel_info);
        AbstractBasePtr abstract = std::make_shared<abstract::AbstractNone>();
        MS_EXCEPTION_IF_NULL(abstract);
        AnfAlgo::SetNodeAttr("input_names", MakeValue(std::vector<std::string>({"x"})), clear_zero);
        SelectKernelInfo(clear_zero);
        // set the distinction label of clear same with anf
        AnfAlgo::SetStreamDistinctionLabel(AnfAlgo::GetStreamDistinctionLabel(anf_node.get()), clear_zero.get());
        new_nodes.push_back(clear_zero);
      } else if (AnfAlgo::GetKernelType(anf_node) == KernelType::TBE_KERNEL) {
        if (IsAtomicNode(anf_node)) {
          AddTbeClearZeroNode(kernel_graph, anf_node, &new_nodes);
        }
      }
      new_nodes.push_back(anf_node);
    }
    kernel_graph->set_execution_order(new_nodes);
  }
}
}  // namespace ascend
}  // namespace device
}  // namespace mindspore
