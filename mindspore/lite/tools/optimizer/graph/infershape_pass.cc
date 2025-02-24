/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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

#include "tools/optimizer/graph/infershape_pass.h"

namespace mindspore {
namespace opt {
bool InferShapePass::Run(const FuncGraphPtr &func_graph) {
  if (func_graph == nullptr) {
    MS_LOG(ERROR) << "func_graph is nullptr.";
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
    return false;
  }
  node_infer_shape_ = std::make_shared<NodeInferShape>(fmk_type_, train_flag_);
  if (node_infer_shape_ == nullptr) {
    MS_LOG(ERROR) << "create NodeInferShape object failed.";
    return false;
  }
  if (!JudgeAllOpsCanInfer(func_graph)) {
    MS_LOG(ERROR) << "exist op cannot support infer shape.";
    return false;
  }
  if (InferProcess(func_graph) != lite::RET_OK) {
    MS_LOG(ERROR) << "infer shape failed.";
    return false;
  }
  ResetSubGraphInput();
  return true;
}

bool InferShapePass::JudgeAllOpsCanInfer(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto node_list = TopoSort(func_graph->get_return());
  bool all_op_can_infer = true;
  for (auto &node : node_list) {
    if (!utils::isa<CNodePtr>(node)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    if (IsSpecialType(cnode)) {
      continue;
    }
    if (CheckPrimitiveType(node, prim::kPrimIf) || CheckPrimitiveType(node, prim::kPrimWhile)) {
      auto sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(1));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        all_op_can_infer = false;
      } else {
        all_op_can_infer = all_op_can_infer && JudgeAllOpsCanInfer(sub_func_graph);
      }
      sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(1));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        all_op_can_infer = false;
      } else {
        all_op_can_infer = all_op_can_infer && JudgeAllOpsCanInfer(sub_func_graph);
      }
      continue;
    }
    auto cur_op_can_infer = node_infer_shape_->JudgeOpSupportInfer(cnode);
    if (!cur_op_can_infer) {
      auto prim = GetValueNode<PrimitivePtr>(cnode->input(0));
      MS_ASSERT(prim != nullptr);
      lite::NotSupportOp::GetInstance()->InsertOp(prim->name());
      lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NOT_SUPPORT);
      all_op_can_infer = false;
    }
  }
  return all_op_can_infer;
}

STATUS InferShapePass::InferProcess(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto node_list = TopoSort(func_graph->get_return());
  for (auto &node : node_list) {
    if (!utils::isa<CNode>(node)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    if (IsSpecialType(cnode)) {
      continue;
    }
    if (opt::CheckPrimitiveType(node, prim::kPrimIf) || opt::CheckPrimitiveType(node, prim::kPrimWhile)) {
      auto sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(1));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return false;
      }
      SetSubGraphInput(cnode, sub_func_graph);
      if (InferProcess(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "subgraph infer shape failed.";
        return false;
      }
      SetSubGraphOutput(cnode, sub_func_graph);
      sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(kInputIndexTwo));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return false;
      }
      SetSubGraphInput(cnode, sub_func_graph);
      if (InferProcess(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "subgraph infer shape failed.";
        return false;
      }
      SetSubGraphOutput(cnode, sub_func_graph);
      SetSubGraphAbstract(cnode, sub_func_graph);
      continue;
    }
    auto status = node_infer_shape_->InferShape(cnode);
    if (status != lite::RET_OK && status != lite::RET_INFER_INVALID) {
      MS_LOG(ERROR) << "node infer shape failed, node is " << node->fullname_with_scope();
      return lite::RET_ERROR;
    }
  }
  return lite::RET_OK;
}

void InferShapePass::SetSubGraphInput(const CNodePtr &cnode, const FuncGraphPtr &sub_graph) {
  MS_ASSERT(cnode != nullptr && sub_graph != nullptr);
  auto sub_inputs = sub_graph->get_inputs();
  sub_inputs_map_[sub_graph] = sub_inputs;
  for (auto &node : sub_inputs) {
    auto param_node = node->cast<ParameterPtr>();
    MS_ASSERT(param_node != nullptr);
    auto node_name = node->fullname_with_scope();
    auto last_underline = node_name.find_last_of("_");
    node_name = node_name.substr(0, last_underline);
    last_underline = node_name.find_last_of("_");
    auto index = std::stoi(node_name.substr(last_underline + 1)) + 3;
    param_node->set_abstract(opt::GetCNodeInputAbstract(cnode, index)->Clone());
    if (utils::isa<CNodePtr>(cnode->input(index))) {
      ShapeVector shape_vec = {-1};
      auto out_cnode = cnode->input(index)->cast<CNodePtr>();
      MS_ASSERT(trans_cnode != nullptr);
      auto out_prim = GetValueNode<PrimitivePtr>(out_cnode->input(0));
      if (out_prim->GetAttr(opt::kInferDone) == nullptr || !GetValue<bool>(out_prim->GetAttr(opt::kInferDone))) {
        param_node->abstract()->set_shape(std::make_shared<abstract::Shape>(shape_vec));
      }
    } else {
      lite::DataInfo data_info;
      if (utils::isa<ParameterPtr>(cnode->input(index))) {
        if (cnode->input(index)->cast<ParameterPtr>()->has_default()) {
          param_node->set_default_param(cnode->input(index)->cast<ParameterPtr>()->default_param());
        }
        continue;
      }
      auto status = lite::FetchDataFromValueNode(cnode, index, fmk_type_, train_flag_, &data_info);
      if (status != lite::RET_OK) {
        continue;
      }
      ShapeVector shape_vec(data_info.shape_.begin(), data_info.shape_.end());
      if (data_info.data_.empty()) {
        param_node->set_default_param(std::make_shared<tensor::Tensor>((TypeId)data_info.data_type_, shape_vec));
      } else {
        param_node->set_default_param(std::make_shared<tensor::Tensor>((TypeId)data_info.data_type_, shape_vec,
                                                                       data_info.data_.data(), data_info.data_.size()));
      }
    }
  }
}

void InferShapePass::SetSubGraphOutput(const CNodePtr &cnode, const FuncGraphPtr &sub_graph) {
  MS_ASSERT(cnode != nullptr && sub_graph != nullptr);
  auto return_node = sub_graph->get_return();
  auto origin_input = return_node->inputs();
  lite::RemoveIfDepend(return_node);
  lite::RemoveIfMakeTuple(return_node);
  for (size_t i = 1; i < return_node->size(); ++i) {
    if (!opt::CheckPrimitiveType(return_node->input(i), prim::kPrimTranspose)) {
      continue;
    }
    auto node_name = return_node->input(i)->fullname_with_scope();
    if (node_name.size() < kInputSizeFive || node_name.substr(node_name.size() - kInputSizeFive) != "_post") {
      continue;
    }
    auto trans_cnode = return_node->input(i)->cast<CNodePtr>();
    MS_ASSERT(trans_cnode != nullptr);
    auto trans_input = trans_cnode->input(1);
    auto trans_input_name = trans_input->fullname_with_scope();
    if (utils::isa<ParameterPtr>(trans_input)) {
      trans_input->cast<ParameterPtr>()->set_name(node_name);
    } else if (utils::isa<CNodePtr>(trans_input)) {
      trans_input->cast<CNodePtr>()->set_fullname_with_scope(node_name);
    }
    trans_input_name = trans_input_name.substr(0, trans_input_name.find_last_of("_")) + "_cnode";
    trans_cnode->set_fullname_with_scope(trans_input_name);
  }
  return_node->set_inputs(origin_input);
}

void InferShapePass::SetSubGraphAbstract(const CNodePtr &cnode, const FuncGraphPtr &sub_graph) {
  MS_ASSERT(cnode != nullptr && sub_graph != nullptr);
  auto return_node = sub_graph->get_return();
  auto origin_inputs = return_node->inputs();
  lite::RemoveIfDepend(return_node);
  lite::RemoveIfMakeTuple(return_node);
  AbstractBasePtrList abstract_list;
  bool infer_done = true;
  for (size_t i = 1; i < return_node->size(); ++i) {
    auto abstract_base = opt::GetCNodeInputAbstract(return_node, i);
    MS_ASSERT(abstract_base != nullptr);
    abstract_list.emplace_back(abstract_base->Clone());
    auto abstract_tensor = abstract_base->cast<abstract::AbstractTensorPtr>();
    MS_ASSERT(abstract_tensor != nullptr);
    auto shape_ptr = utils::cast<abstract::ShapePtr>(abstract_tensor->BuildShape());
    MS_ASSERT(shape_ptr != nullptr);
    auto shape = shape_ptr->shape();
    if (std::find(shape.begin(), shape.end(), -1) != shape.end()) {
      infer_done = false;
    }
    if (utils::isa<CNodePtr>(return_node->input(i))) {
      auto input_cnode = return_node->input(i)->cast<CNodePtr>();
      if (opt::CheckPrimitiveType(input_cnode, prim::kPrimTupleGetItem)) {
        input_cnode = input_cnode->input(1)->cast<CNodePtr>();
      }
      auto input_prim = GetValueNode<PrimitivePtr>(input_cnode->input(0));
      if (input_prim->GetAttr(opt::kInferDone) == nullptr || !GetValue<bool>(input_prim->GetAttr(opt::kInferDone))) {
        infer_done = false;
      }
    }
  }
  return_node->set_inputs(origin_inputs);
  if (utils::isa<abstract::AbstractTuplePtr>(cnode->abstract())) {
    cnode->set_abstract(std::make_shared<abstract::AbstractTuple>(abstract_list));
  } else {
    if (abstract_list.size() != 1) {
      MS_LOG(ERROR) << "cnode output is invalid.";
    }
    cnode->set_abstract(abstract_list.front());
  }
  auto prim = GetValueNode<PrimitivePtr>(cnode->input(0));
  prim->AddAttr(opt::kInferDone, MakeValue<bool>(infer_done));
}

void InferShapePass::ResetSubGraphInput() {
  for (auto iter = sub_inputs_map_.begin(); iter != sub_inputs_map_.end(); ++iter) {
    auto &sub_graph = iter->first;
    auto &sub_inputs = iter->second;
    auto manager = sub_graph->manager();
    MS_ASSERT(manager != nullptr);
    for (auto &sub_input : sub_inputs) {
      auto param_node = sub_graph->add_parameter();
      MS_ASSERT(param_node != nullptr);
      param_node->set_abstract(sub_input->abstract()->Clone());
      param_node->set_name(sub_input->fullname_with_scope());
      manager->Replace(sub_input, param_node);
      auto sub_param_input = sub_input->cast<ParameterPtr>();
      MS_ASSERT(sub_param_input != nullptr);
      sub_param_input->set_default_param(nullptr);
    }
  }
}
}  // namespace opt
}  // namespace mindspore
