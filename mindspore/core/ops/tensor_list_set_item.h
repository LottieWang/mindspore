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

#ifndef MINDSPORE_CORE_OPS_TENSOR_LIST_SET_ITEM_H_
#define MINDSPORE_CORE_OPS_TENSOR_LIST_SET_ITEM_H_
#include <memory>
#include "ops/primitive_c.h"
#include "abstract/abstract_value.h"
#include "utils/check_convert_utils.h"

namespace mindspore {
namespace ops {
constexpr auto kNameTensorListSetItem = "TensorListSetItem";
class MS_CORE_API TensorListSetItem : public PrimitiveC {
 public:
  TensorListSetItem() : PrimitiveC(kNameTensorListSetItem) {}
  ~TensorListSetItem() = default;
  MS_DECLARE_PARENT(TensorListSetItem, PrimitiveC);
  void Init(const int64_t element_dtype);
  void set_element_dtype(const int64_t element_dtype);
  int64_t get_element_dtype() const;
};
}  // namespace ops
}  // namespace mindspore

#endif  // MINDSPORE_CORE_OPS_TENSOR_LIST_SET_ITEM_H_
