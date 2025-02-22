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

#include "nnacl/infer/scatter_nd_update_infer.h"
#include "nnacl/infer/infer_register.h"

int ScatterNdUpdateInferShape(const TensorC *const *inputs, size_t inputs_size, TensorC **outputs, size_t outputs_size,
                              OpParameter *parameter) {
  int check_ret = CheckAugmentNullSize(inputs, inputs_size, outputs, outputs_size, parameter, 3, 1);
  if (check_ret != NNACL_OK) {
    return check_ret;
  }

  const TensorC *input_x = inputs[0];
  const TensorC *indices = inputs[1];
  const TensorC *update = inputs[2];
  TensorC *output = outputs[0];

  SetDataTypeFormat(output, input_x);
  if (!InferFlag(inputs, inputs_size)) {
    return NNACL_INFER_INVALID;
  }
  if (indices->shape_size_ != update->shape_size_) {
    return NNACL_ERR;
  }

  SetShapeArray(output, input_x->shape_, input_x->shape_size_);
  return NNACL_OK;
}

REG_INFER(ScatterNdUpdate, PrimType_ScatterNdUpdate, ScatterNdUpdateInferShape)
