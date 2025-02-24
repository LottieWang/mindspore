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

#include "tools/optimizer/fusion/affine_fusion.h"
#include <memory>
#include <vector>
#include "ops/affine.h"
#include "src/common/log_adapter.h"
#include "schema/inner/model_generated.h"
#include "ops/splice.h"
#include "ops/mat_mul.h"
#include "tools/optimizer/common/gllo_utils.h"

namespace mindspore::opt {
constexpr auto kRowAxis = 1;
constexpr auto kInputWithBiasNum = 4;
constexpr auto kInputBias = 3;

static bool IsSpliceNode(const BaseRef &n) {
  if (utils::isa<AnfNodePtr>(n)) {
    return CheckPrimitiveType(utils::cast<AnfNodePtr>(n), prim::kPrimSplice);
  }
  return false;
}

static bool IsMatMulNode(const BaseRef &n) {
  if (utils::isa<AnfNodePtr>(n)) {
    auto anf_node = utils::cast<AnfNodePtr>(n);
    return CheckPrimitiveType(anf_node, prim::kPrimMatMul);
  }
  return false;
}

const BaseRef AffineFusion::DefinePattern() const {
  auto matmul_var = std::make_shared<CondVar>(IsMatMulNode);
  auto splice_var = std::make_shared<CondVar>(IsSpliceNode);
  auto matmul_weight_var = std::make_shared<CondVar>(IsParamNode);
  auto matmul_bias_var = std::make_shared<SeqVar>();
  return VectorRef({matmul_var, splice_var, matmul_weight_var, matmul_bias_var});
}

const AnfNodePtr AffineFusion::Process(const FuncGraphPtr &func_graph, const AnfNodePtr &node,
                                       const EquivPtr &equiv) const {
  MS_ASSERT(equiv != nullptr);
  if (CheckIfFuncGraphIsNull(func_graph) != lite::RET_OK || CheckIfAnfNodeIsNull(node) != lite::RET_OK) {
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
    return nullptr;
  }
  // matmul
  if (!CheckPrimitiveType(node, prim::kPrimMatMul)) {
    MS_LOG(ERROR) << "the layer processed by affine fusion is not matmul.";
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_PARAM_INVALID);
    return nullptr;
  }
  auto matmul_node = node->cast<CNodePtr>();
  if (CheckIfCNodeIsNull(matmul_node) != lite::RET_OK) {
    MS_LOG(ERROR) << "the matmul_node is null.";
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
    return nullptr;
  }
  auto matmul_prim = GetValueNode<std::shared_ptr<ops::MatMul>>(matmul_node->input(kAnfPrimitiveIndex));
  MS_ASSERT(matmul_prim != nullptr);
  // splice
  AnfNodePtr pre_node = matmul_node->input(1);
  if (!CheckPrimitiveType(pre_node, prim::kPrimSplice)) {
    MS_LOG(ERROR) << "previous node is not splice.";
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_PARAM_INVALID);
    return nullptr;
  }
  auto splice_node = pre_node->cast<CNodePtr>();
  if (CheckIfAnfNodeIsNull(pre_node) != lite::RET_OK) {
    MS_LOG(ERROR) << "the splice_node is null.";
    lite::ReturnCode::GetSingleReturnCode()->UpdateReturnCode(lite::RET_NULL_PTR);
    return nullptr;
  }
  auto splice_prim = GetValueNode<std::shared_ptr<ops::Splice>>(splice_node->input(kAnfPrimitiveIndex));
  MS_ASSERT(prim != nullptr);
  /**
   * Affine attribute:
   * 1. context
   * 2. transpose_a
   * 3. transpose_b
   * 4. output_dim
   */
  // new primitive
  auto affine_prim = std::make_shared<ops::Affine>();
  // copy splice attr to affine
  affine_prim->set_context(splice_prim->get_context());
  affine_prim->set_output_dim(splice_prim->get_output_dim());
  // copy matmul attribute to affine
  if (matmul_prim->HasAttr(ops::kTransposeA)) {
    affine_prim->set_transpose_a(matmul_prim->get_transpose_a());
  }
  if (matmul_prim->HasAttr(ops::kTransposeB)) {
    affine_prim->set_transpose_b(matmul_prim->get_transpose_b());
  }
  // construct affine node
  std::vector<AnfNodePtr> affine_inputs = {NewValueNode(affine_prim), splice_node->input(1), matmul_node->input(2)};
  if (matmul_node->inputs().size() == kInputWithBiasNum) {
    affine_inputs.push_back(matmul_node->input(kInputBias));
  }
  auto affine_node = func_graph->NewCNode(affine_inputs);
  affine_node->set_fullname_with_scope(matmul_node->fullname_with_scope());
  affine_node->set_abstract(matmul_node->abstract()->Clone());

  MS_LOG(INFO) << "splice + matmul fused to affine node: " << affine_node->fullname_with_scope() << "success.";
  return affine_node;
}
}  // namespace mindspore::opt
