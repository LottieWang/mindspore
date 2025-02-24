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

#ifndef MINDSPORE_LITE_TOOLS_OPTIMIZER_FORMAT_TO_FORMAT_BASE_H_
#define MINDSPORE_LITE_TOOLS_OPTIMIZER_FORMAT_TO_FORMAT_BASE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "backend/optimizer/common/pass.h"
#include "tools/converter/converter_flags.h"
#include "tools/optimizer/common/format_utils.h"
#include "tools/optimizer/graph/infershape_pass.h"

using mindspore::converter::FmkType;
namespace mindspore {
namespace opt {
class ToFormatBase : public Pass {
 public:
  explicit ToFormatBase(FmkType fmk_type = converter::FmkType_MS, bool train_flag = false,
                        std::string pass_name = "to_format_base")
      : Pass(pass_name), fmk_type_(fmk_type), train_flag_(train_flag) {}
  ~ToFormatBase() override = default;
  bool Run(const FuncGraphPtr &func_graph) override;

 private:
  bool BasicProcess(const FuncGraphPtr &func_graph, bool main_graph);
  STATUS HandleGraphInput(const FuncGraphPtr &func_graph);
  STATUS HandleGraphNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode);
  STATUS InsertPostTransNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode, const std::vector<int> &perm);
  STATUS InsertPreTransNode(const FuncGraphPtr &func_graph, const CNodePtr &cnode, const std::vector<int> &perm);
  STATUS GenNewInput(const FuncGraphPtr &func_graph, const CNodePtr &cnode, std::vector<int> perm, bool before,
                     size_t index = 0);
  STATUS ModifyCNode(const CNodePtr &cnode);
  STATUS ConvWeightFormatTrans(const FuncGraphPtr &graph, std::set<AnfNodePtr> *has_visited);

 protected:
  virtual STATUS GetTransNodeFormatType(const CNodePtr &cnode, opt::TransTypePair *trans_info) = 0;
  virtual void SetSensitiveOps() { sensitive_ops_ = opt::GetNHWCOpMap(); }
  virtual bool DecideWhetherHandleGraphInput(const FuncGraphPtr &func_graph, const ShapeVector &shape) { return true; }
  virtual bool DecideWhetherInferShapeForNewNode() { return true; }
  virtual STATUS DecideConvWeightSrcAndDstFormat(const CNodePtr &cnode, schema::Format *src_format,
                                                 schema::Format *dst_format) = 0;
  FmkType fmk_type_{converter::FmkType_MS};
  bool train_flag_{false};
  mindspore::Format format_{mindspore::NHWC};
  std::shared_ptr<NodeInferShape> node_infer_shape_{nullptr};
  std::unordered_map<std::string, std::vector<size_t>> sensitive_ops_;
};
}  // namespace opt
}  // namespace mindspore

#endif  // MINDSPORE_LITE_TOOLS_OPTIMIZER_FORMAT_TO_FORMAT_BASE_H_
