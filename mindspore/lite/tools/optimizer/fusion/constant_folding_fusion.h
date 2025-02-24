/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_SRC_PASS_FUSION_CONSTANT_FOLDING_FUSION_H_
#define MINDSPORE_LITE_SRC_PASS_FUSION_CONSTANT_FOLDING_FUSION_H_

#include <utility>
#include <memory>
#include "schema/inner/model_generated.h"
#include "src/common/context_util.h"
#include "src/tensor.h"
#include "src/lite_kernel.h"
#include "nnacl/op_base.h"
#include "backend/optimizer/common/optimizer.h"
#include "tools/converter/converter_flags.h"

namespace mindspore {
namespace opt {
class ConstFoldPass : public PatternProcessPass {
 public:
  explicit ConstFoldPass(converter::FmkType fmk_type = converter::FmkType_MS, bool multigraph = true)
      : PatternProcessPass("constfold_pass", multigraph), fmk_type_(fmk_type) {
    context_ = std::make_shared<lite::InnerContext>();
    context_->Init();
    ms_context_ = std::shared_ptr<mindspore::Context>(lite::MSContextFromContext(context_.get()));
  }
  ~ConstFoldPass() override = default;
  const AnfNodePtr Process(const FuncGraphPtr &, const AnfNodePtr &, const EquivPtr &) const override;

 private:
  converter::FmkType fmk_type_{converter::FmkType_MS};
  std::shared_ptr<lite::InnerContext> context_{nullptr};
  std::shared_ptr<mindspore::Context> ms_context_{nullptr};
};
}  // namespace opt
}  // namespace mindspore
#endif  // MINDSPORE_LITE_SRC_PASS_FUSION_CONSTANT_FOLDING_FUSION_H_
