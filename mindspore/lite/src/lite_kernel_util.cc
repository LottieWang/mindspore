/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#include "src/lite_kernel_util.h"
#include <queue>
#include <set>
#include "src/sub_graph_kernel.h"

namespace mindspore::kernel {
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_OK;
std::vector<kernel::LiteKernel *> LiteKernelUtil::SubgraphInputNodes(const std::vector<kernel::LiteKernel *> &kernels) {
  std::vector<kernel::LiteKernel *> input_nodes;
  for (const auto &kernel : kernels) {
    // if kernel has no pre-kernel, kernel is a graph input, it must be a subgraph input
    if (kernel->in_kernels().empty() && !kernel->in_tensors().empty()) {
      if (!lite::IsContain(input_nodes, kernel)) {
        input_nodes.push_back(kernel);
      }
      continue;
    }
    auto all_input_tensors = kernel->in_tensors();
    // remove all const tensor from input tensors
    for (auto iter = all_input_tensors.begin(); iter != all_input_tensors.end();) {
      if ((*iter)->IsConst()) {
        iter = all_input_tensors.erase(iter);
      } else {
        iter++;
      }
    }
    for (const auto &kernel_in_subgraph : kernels) {
      // remove input tensors from kernel in subgraph
      for (const auto *tensor : kernel_in_subgraph->out_tensors()) {
        auto ret = std::find(all_input_tensors.begin(), all_input_tensors.end(), tensor);
        if (ret != all_input_tensors.end()) {
          all_input_tensors.erase(ret);
        }
      }
    }
    // if some input tensor is not from kernel in subgraph
    if (!all_input_tensors.empty()) {
      if (!lite::IsContain(input_nodes, kernel)) {
        input_nodes.push_back(kernel);
      }
    }
  }
  return input_nodes;
}

std::vector<kernel::LiteKernel *> LiteKernelUtil::SubgraphOutputNodes(
  const std::vector<kernel::LiteKernel *> &kernels) {
  std::vector<kernel::LiteKernel *> output_nodes;
  // if kernel has no post-kernel, kernel is a graph output, it must be a subgraph output
  for (const auto &kernel : kernels) {
    if (kernel->is_model_output() || (kernel->out_kernels().empty() && !kernel->out_tensors().empty())) {
      if (!lite::IsContain(output_nodes, kernel)) {
        output_nodes.push_back(kernel);
      }
      continue;
    }
    for (const auto &output : kernel->out_kernels()) {
      auto out_kernel_in_graph = std::find(kernels.begin(), kernels.end(), output);
      if (out_kernel_in_graph == kernels.end()) {
        if (!lite::IsContain(output_nodes, kernel)) {
          output_nodes.push_back(kernel);
        }
        break;
      }
    }
  }
  return output_nodes;
}

std::vector<lite::Tensor *> LiteKernelUtil::SubgraphInputTensors(const std::vector<kernel::LiteKernel *> &kernels) {
  std::vector<lite::Tensor *> input_tensors;
  std::vector<kernel::LiteKernel *> input_nodes = SubgraphInputNodes(kernels);
  for (const auto &input_node : input_nodes) {
    auto &in_node_in_kernels = input_node->in_kernels();
    auto &in_node_in_tensors = input_node->in_tensors();
    for (auto &in_node_in_tensor : in_node_in_tensors) {
      if (in_node_in_tensor->IsGraphInput()) {
        if (!lite::IsContain(input_tensors, in_node_in_tensor)) {
          input_tensors.push_back(in_node_in_tensor);
        }
      }
    }
    for (auto in_node_in_kernel : in_node_in_kernels) {
      auto iter = std::find(kernels.begin(), kernels.end(), in_node_in_kernel);
      if (iter != kernels.end()) {
        continue;
      }
      auto &outer_in_kernel_out_tensors = in_node_in_kernel->out_tensors();
      for (auto in_node_in_tensor : in_node_in_tensors) {
        auto outer_in_kernel_out_tensors_iter =
          std::find(outer_in_kernel_out_tensors.begin(), outer_in_kernel_out_tensors.end(), in_node_in_tensor);
        if (outer_in_kernel_out_tensors_iter != outer_in_kernel_out_tensors.end()) {
          if (!lite::IsContain(input_tensors, in_node_in_tensor)) {
            input_tensors.push_back(in_node_in_tensor);
          }
        }
      }
    }
  }
  return input_tensors;
}

std::vector<lite::Tensor *> LiteKernelUtil::SubgraphOutputTensors(const std::vector<kernel::LiteKernel *> &kernels) {
  std::vector<lite::Tensor *> output_tensors;
  std::vector<kernel::LiteKernel *> output_nodes = SubgraphOutputNodes(kernels);
  for (const auto &output_kernel : output_nodes) {
    auto &outer_out_kernels = output_kernel->out_kernels();
    auto &out_kernel_out_tensors = output_kernel->out_tensors();
    for (auto out_kernel_out_tensor : out_kernel_out_tensors) {
      if (out_kernel_out_tensor->IsGraphOutput()) {
        if (!lite::IsContain(output_tensors, out_kernel_out_tensor)) {
          output_tensors.push_back(out_kernel_out_tensor);
        }
      }
    }
    if (!outer_out_kernels.empty()) {
      for (auto outer_out_kernel : outer_out_kernels) {
        auto iter = std::find(kernels.begin(), kernels.end(), outer_out_kernel);
        if (iter != kernels.end()) {
          continue;
        }
        auto &outer_out_kernel_in_tensors = outer_out_kernel->in_tensors();
        for (auto out_kernel_out_tensor : out_kernel_out_tensors) {
          auto outer_out_kernel_in_tensors_iter =
            std::find(outer_out_kernel_in_tensors.begin(), outer_out_kernel_in_tensors.end(), out_kernel_out_tensor);
          if (outer_out_kernel_in_tensors_iter != outer_out_kernel_in_tensors.end()) {
            if (!lite::IsContain(output_tensors, out_kernel_out_tensor)) {
              output_tensors.push_back(out_kernel_out_tensor);
            }
          }
        }
      }
    }
  }
  return output_tensors;
}

int LiteKernelUtil::TopologicalSortKernels(std::vector<kernel::LiteKernel *> *kernels) {
  auto old_kernels = *kernels;
  kernels->clear();
  std::queue<kernel::LiteKernel *> kernel_queue;
  for (auto kernel : old_kernels) {
    if (kernel->in_kernels().empty()) {
      kernel_queue.push(kernel);
      kernels->emplace_back(kernel);
    }
  }
  while (!kernel_queue.empty()) {
    auto cur_kernel = kernel_queue.front();
    kernel_queue.pop();
    MS_ASSERT(cur_kernel != nullptr);
    auto next_kernels = cur_kernel->out_kernels();
    for (auto next_kernel : next_kernels) {
      auto in_kernels = next_kernel->in_kernels();
      if (lite::IsContain(*kernels, const_cast<kernel::LiteKernel *>(next_kernel))) {
        MS_LOG(ERROR) << "TopologicalSortKernels failed, loop exist";
        return RET_ERROR;
      }
      if (std::all_of(in_kernels.begin(), in_kernels.end(), [&](const kernel::LiteKernel *in_kernel) {
            return lite::IsContain(*kernels, const_cast<kernel::LiteKernel *>(in_kernel));
          })) {
        kernel_queue.push(next_kernel);
      }
    }
  }
  if (kernels->size() != old_kernels.size()) {
    MS_LOG(ERROR) << "TopologicalSortKernels failed, kernels size before sort: " << old_kernels.size()
                  << ", kernels size after sort: " << kernels->size();
    return RET_ERROR;
  }
  return RET_OK;
}

void LiteKernelUtil::InitTensorInitRefCount(const std::vector<kernel::LiteKernel *> &kernels) {
  for (auto *kernel : kernels) {
    kernel->InitOutTensorInitRefCount(&kernels);
  }
}

int LiteKernelUtil::SetInput(const LiteKernel &kernelMod, const std::vector<lite::Tensor *> &inputs) { return -1; }

#ifdef ENABLE_CONTROL_TENSORLIST
bool LiteKernelUtil::IsSwitchCall(kernel::LiteKernel *kernel) {
#ifdef ENABLE_DELEGATE_USE
  if (kernel->desc().delegate != nullptr) {
    return false;
  }
#endif
  auto *subgraph_kernel = reinterpret_cast<kernel::SubGraphKernel *>(kernel);
  if (subgraph_kernel == nullptr) {
    return false;
  }
  for (auto &node : subgraph_kernel->nodes()) {
    if (node->type() == schema::PrimitiveType_Switch &&
        InputsContainsSpecificNode(node, schema::PrimitiveType_PartialFusion) && node->out_kernels().size() == 1 &&
        node->out_kernels().front()->type() == schema::PrimitiveType_Call) {
      return true;
    }
  }

  return false;
}
#endif

kernel::LiteKernel *LiteKernelUtil::GetInputsSpecificNode(const kernel::LiteKernel *kernel,
                                                          const schema::PrimitiveType &primitive_type) {
  for (auto input : kernel->in_kernels()) {
    if (input->type() == primitive_type) {
      return input;
    }
  }
  return nullptr;
}

bool LiteKernelUtil::InputsContainsSpecificNode(const kernel::LiteKernel *kernel,
                                                const schema::PrimitiveType &primitive_type) {
  if (GetInputsSpecificNode(kernel, primitive_type)) {
    return true;
  }
  return false;
}
}  // namespace mindspore::kernel
