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

#ifndef MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_UNIFY_FORMAT_PASS_H_
#define MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_UNIFY_FORMAT_PASS_H_

#include <vector>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include "backend/optimizer/common/optimizer.h"
#include "utils/utils.h"
#include "tools/converter/converter_flags.h"
#include "tools/optimizer/common/format_utils.h"
#include "tools/optimizer/graph/transpose_strategy.h"

using mindspore::converter::FmkType;
namespace mindspore {
namespace opt {
class DecreaseTransposeAlgo : public Pass {
 public:
  explicit DecreaseTransposeAlgo(FmkType fmk_type = FmkType::FmkType_MS, bool train_flag = false)
      : Pass("DecreaseTransposeAlgo"), fmk_type_(fmk_type), train_flag_(train_flag) {}
  ~DecreaseTransposeAlgo() override = default;
  void Init(FmkType fmk_type, bool train_flag) {
    fmk_type_ = fmk_type;
    train_flag_ = train_flag;
    node_infer_shape_.Init(fmk_type, train_flag);
    transpose_strategy_.Init(fmk_type, train_flag);
  }
  bool Run(const FuncGraphPtr &func_graph) override;

 private:
  STATUS InsertPostTransNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode, const std::vector<int> &perm);
  STATUS InsertPreTransNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode, const std::vector<int> &perm);
  STATUS GenNewInput(const FuncGraphPtr &func_graph, const CNodePtr &cnode, std::vector<int> perm, bool before,
                     size_t index = 0);
  bool RunDoFixFormat(const FuncGraphPtr &func_graph, const CNodePtr &cnode);
  bool DoFixFormat(const FuncGraphPtr &func_graph);
  bool DecreaseTransposeForSingleOp(const FuncGraphPtr &func_graph);
  bool DecreaseTransposeForMultiOp(const FuncGraphPtr &func_graph);
  STATUS PostTransposeFusion(const FuncGraphPtr &func_graph, const CNodePtr &cnode);

  STATUS HandleGraphMultiNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode,
                              std::set<CNodePtr> *visit_transposes);
  STATUS InsertPreTransNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode, TransTypePair *trans_insert_info);
  void SetSubGraphInput(const CNodePtr &cnode, const FuncGraphPtr &sub_graph);
  void ResetSubGraphInput();
  void SetSubGraphOutput(const CNodePtr &cnode, const FuncGraphPtr &sub_graph);
  void SetSubGraphAbstract(const CNodePtr &cnode, const FuncGraphPtr &sub_graph);
  FmkType fmk_type_{converter::FmkType_MS};
  bool train_flag_{false};
  NodeInferShape node_infer_shape_;
  TransposeStrategy transpose_strategy_;
  std::unordered_map<FuncGraphPtr, std::vector<AnfNodePtr>> sub_inputs_map_;
};
}  // namespace opt
}  // namespace mindspore

#endif  // MINDSPORE_LITE_TOOLS_OPTIMIZER_GRAPH_UNIFY_FORMAT_PASS_H_
