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

#include "tools/optimizer/format/delete_redundant_transpose.h"
#include <vector>
#include "tools/optimizer/common/format_utils.h"

namespace mindspore {
namespace opt {
namespace {
constexpr size_t kDimNumber = 4;
}  // namespace

STATUS DeleteRedundantTranspose::DeleteNot4DTranspose(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto manager = func_graph->manager();
  MS_ASSERT(manager != nullptr);
  auto node_list = TopoSort(func_graph->get_return());
  for (auto &node : node_list) {
    if (!utils::isa<CNode>(node)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    if (CheckPrimitiveType(cnode, prim::kPrimIf) || CheckPrimitiveType(cnode, prim::kPrimWhile)) {
      auto sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(1));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return lite::RET_NULL_PTR;
      }
      if (DeleteNot4DTranspose(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "delete transpose failed.";
        return lite::RET_ERROR;
      }
      sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(kInputIndexTwo));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return lite::RET_NULL_PTR;
      }
      if (DeleteNot4DTranspose(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "delete transpose failed.";
        return lite::RET_ERROR;
      }
      continue;
    }
    if (!CheckPrimitiveType(node, prim::kPrimTranspose)) {
      continue;
    }
    auto abstract = GetCNodeInputAbstract(cnode, 1);
    ShapeVector shape;
    if (FetchShapeFromAbstract(abstract, &shape) != lite::RET_OK) {
      MS_LOG(ERROR) << "fetch shape failed.";
      return lite::RET_ERROR;
    }
    std::vector<int> perm;
    if (GetTransposePerm(cnode, &perm) != lite::RET_OK) {
      MS_LOG(ERROR) << "fetch transpose perm failed.";
      return lite::RET_ERROR;
    }
    if (!shape.empty() && shape.size() != perm.size()) {
      MS_LOG(DEBUG) << "transpose node need to be deleted.";
      if (UpdateNodeFormat(func_graph, cnode) != lite::RET_OK) {
        MS_LOG(ERROR) << "update cnode format failed.";
        return lite::RET_ERROR;
      }
      manager->Replace(node, cnode->input(1));
    }
  }
  return lite::RET_OK;
}

STATUS DeleteRedundantTranspose::TransTransFusion(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto node_lite = TopoSort(func_graph->get_return());
  for (auto &node : node_lite) {
    if (!utils::isa<CNode>(node)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    if (CheckPrimitiveType(cnode, prim::kPrimIf) || CheckPrimitiveType(cnode, prim::kPrimWhile)) {
      auto sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(1));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return lite::RET_NULL_PTR;
      }
      if (TransTransFusion(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "delete transpose failed.";
        return lite::RET_ERROR;
      }
      sub_func_graph = GetValueNode<FuncGraphPtr>(cnode->input(kInputIndexTwo));
      if (sub_func_graph == nullptr) {
        lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
        return lite::RET_NULL_PTR;
      }
      if (TransTransFusion(sub_func_graph) != lite::RET_OK) {
        MS_LOG(ERROR) << "delete transpose failed.";
        return lite::RET_ERROR;
      }
      continue;
    }
    if (!CheckPrimitiveType(cnode, prim::kPrimTranspose) ||
        !CheckPrimitiveType(cnode->input(1), prim::kPrimTranspose)) {
      continue;
    }
    std::vector<int> post_perm;
    if (GetTransposePerm(cnode, &post_perm) != lite::RET_OK) {
      MS_LOG(ERROR) << "transpose rm cannot be obtained, " << cnode->fullname_with_scope();
      return lite::RET_ERROR;
    }
    std::vector<int> pre_perm;
    auto pre_cnode = cnode->input(1)->cast<CNodePtr>();
    MS_ASSERT(pre_cnode != nullptr);
    if (GetTransposePerm(pre_cnode, &pre_perm) != lite::RET_OK) {
      MS_LOG(ERROR) << "transpose rm cannot be obtained, " << pre_cnode->fullname_with_scope();
      return lite::RET_ERROR;
    }
    if ((pre_perm == kNH2NC && post_perm == kNC2NH) || (pre_perm == kNC2NH && post_perm == kNH2NC)) {
      func_graph->manager()->Replace(cnode, pre_cnode->input(1));
    }
  }
  return lite::RET_OK;
}

STATUS DeleteRedundantTranspose::UpdateNodeFormat(const FuncGraphPtr &func_graph, const CNodePtr &cnode) {
  MS_ASSERT(func_graph != nullptr && cnode != nullptr);
  auto manager = func_graph->manager();
  MS_ASSERT(manager != nullptr);
  auto prim = GetValueNode<PrimitivePtr>(cnode->input(0));
  MS_ASSERT(prim != nullptr);
  if (prim->GetAttr(ops::kFormat) == nullptr) {
    return lite::RET_OK;
  }
  auto format = GetValue<int64_t>(prim->GetAttr(ops::kFormat));
  auto node_users = manager->node_users()[cnode];
  for (auto &node_user : node_users) {
    if (node_user.second != 1) {
      continue;
    }
    if (!utils::isa<CNode>(node_user.first)) {
      MS_LOG(ERROR) << "post node is not cnode, which is invalid.";
      return lite::RET_ERROR;
    }
    auto post_cnode = node_user.first->cast<CNodePtr>();
    auto post_prim = GetValueNode<PrimitivePtr>(post_cnode->input(0));
    MS_ASSERT(post_prim != nullptr);
    post_prim->AddAttr(ops::kFormat, MakeValue<int64_t>(format));
  }
  return lite::RET_OK;
}

bool DeleteRedundantTranspose::Run(const FuncGraphPtr &func_graph) {
  MS_ASSERT(func_graph != nullptr);
  auto manager = Manage(func_graph, true);
  if (manager == nullptr) {
    MS_LOG(ERROR) << "manager is nullptr.";
    return false;
  }
  if (TransTransFusion(func_graph) != lite::RET_OK) {
    MS_LOG(ERROR) << "ranspose and transpose fusion failed.";
    return false;
  }
  if (DeleteNot4DTranspose(func_graph) != lite::RET_OK) {
    MS_LOG(ERROR) << "delete not 4D transpose failed.";
    return false;
  }
  return true;
}
}  // namespace opt
}  // namespace mindspore
