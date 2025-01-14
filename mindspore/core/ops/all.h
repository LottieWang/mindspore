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

#ifndef MINDSPORE_CORE_OPS_ALL_H_
#define MINDSPORE_CORE_OPS_ALL_H_
#include "ops/primitive_c.h"
#include "abstract/abstract_value.h"
#include "utils/check_convert_utils.h"

namespace mindspore {
namespace ops {
constexpr auto kNameAll = "All";
class MS_CORE_API All : public PrimitiveC {
 public:
  All() : PrimitiveC(kNameAll) {}
  ~All() = default;
  MS_DECLARE_PARENT(All, PrimitiveC);
  void Init(const int64_t keep_dims);
  void set_keep_dims(const int64_t keep_dims);
  int64_t get_keep_dims() const;
};
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_ALL_H_
