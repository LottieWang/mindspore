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

#ifndef MINDSPORE_CORE_OPS_PARTIAL_FUSION_H_
#define MINDSPORE_CORE_OPS_PARTIAL_FUSION_H_
#include "ops/primitive_c.h"
#include "abstract/abstract_value.h"
#include "utils/check_convert_utils.h"

namespace mindspore {
namespace ops {
constexpr auto kNamePartialFusion = "PartialFusion";
class MS_CORE_API PartialFusion : public PrimitiveC {
 public:
  PartialFusion() : PrimitiveC(kNamePartialFusion) {}
  ~PartialFusion() = default;
  MS_DECLARE_PARENT(PartialFusion, PrimitiveC);
  void Init(const int64_t sub_graph_index);
  void set_sub_graph_index(const int64_t sub_graph_index);
  int64_t get_sub_graph_index() const;
};
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_PARTIAL_FUSION_H_
