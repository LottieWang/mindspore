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

#include "src/runtime/kernel/arm/base/reshape_base.h"
#include "schema/model_generated.h"
#include "src/kernel_registry.h"
#include "include/errorcode.h"

using mindspore::kernel::KERNEL_ARCH;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_NULL_PTR;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_ExpandDims;
using mindspore::schema::PrimitiveType_Flatten;
using mindspore::schema::PrimitiveType_FlattenGrad;
using mindspore::schema::PrimitiveType_Reshape;
using mindspore::schema::PrimitiveType_Squeeze;
using mindspore::schema::PrimitiveType_Unsqueeze;

namespace mindspore::kernel {
int ReshapeBaseCPUKernel::Run() {
  auto in_tensor = in_tensors().front();
  auto out_tensor = out_tensors().front();

  /*
   * in_tensor : CPU-allocator ;  out_tensor : GPU-allocator
   * out_tensor data_c can not change
   * */
  if (in_tensor->allocator() == nullptr || in_tensor->allocator() != out_tensor->allocator() ||
      op_parameter_->is_train_session_) {
    memcpy(out_tensor->data_c(), in_tensor->data_c(), in_tensor->Size());
    return RET_OK;
  }

  out_tensor->FreeData();
  out_tensor->ResetRefCount();

  in_tensor->allocator()->IncRefCount(in_tensor->data(), out_tensor->ref_count());

  out_tensor->set_data(in_tensor->data_c());
  out_tensor->set_own_data(in_tensor->own_data());
  return RET_OK;
}

REG_KERNEL(kCPU, kNumberTypeInt32, PrimitiveType_Reshape, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_Reshape, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Reshape, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeBool, PrimitiveType_Reshape, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Flatten, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_Flatten, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_FlattenGrad, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_FlattenGrad, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeInt32, PrimitiveType_ExpandDims, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_ExpandDims, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_ExpandDims, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeInt8, PrimitiveType_ExpandDims, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_Squeeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Squeeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeInt32, PrimitiveType_Squeeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeBool, PrimitiveType_Squeeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat16, PrimitiveType_Unsqueeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeFloat32, PrimitiveType_Unsqueeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeInt32, PrimitiveType_Unsqueeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
REG_KERNEL(kCPU, kNumberTypeInt64, PrimitiveType_Unsqueeze, LiteKernelCreator<ReshapeBaseCPUKernel>)
}  // namespace mindspore::kernel
