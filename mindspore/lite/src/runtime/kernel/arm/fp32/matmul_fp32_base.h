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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_MATMUL_FP32_BASE_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_MATMUL_FP32_BASE_H_

#include <vector>
#include "src/inner_kernel.h"
#include "nnacl/matmul_parameter.h"
#include "include/errorcode.h"

using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_MEMORY_FAILED;
using mindspore::lite::RET_OK;

namespace mindspore::kernel {
using MatrixPackFun = void (*)(const float *src_ptr, float *dst_ptr, int row, int col);
class MatmulFp32BaseCPUKernel : public InnerKernel {
 public:
  MatmulFp32BaseCPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                          const std::vector<lite::Tensor *> &outputs, const mindspore::lite::InnerContext *ctx)
      : InnerKernel(parameter, inputs, outputs, ctx) {
    params_ = reinterpret_cast<MatMulParameter *>(op_parameter_);
    vec_matmul_ = false;
  }
  ~MatmulFp32BaseCPUKernel() override;
  int Init() override;
  int ReSize() override;
  int Run() override;

 public:
  int FloatRun(int task_id) const;

 protected:
  int InitBufferA();
  int InitBufferB();
  int InitMatrixA(const float *src_ptr);
  int InitMatrixB(const float *src_ptr);
  void FreeBiasBuf();
  int InitBiasData();
  void InitParameter();

 private:
  void ResizeParameter();
  void FreeResizeBufA();
  void FreeResizeBufB();
  void FreeBuffSrcB();
  int CalBroadCastBiasDataElements();
  int InitTmpOutBuffer();

 protected:
  MatMulParameter *params_ = nullptr;
  float *a_pack_ptr_ = nullptr;
  float *b_pack_ptr_ = nullptr;

 private:
  int col_tile_ = 0;
  int row_tile_ = 0;
  int oc_res_ = 0;
  int thread_stride_ = 0;
  int thread_count_ = 0;
  bool vec_matmul_ = false;
  float *bias_ptr_ = nullptr;
  float *batch_a_ptr_ = nullptr;
  float *batch_b_ptr_ = nullptr;
  float *batch_c_ptr_ = nullptr;
  float *output_data_ = nullptr;
  int matrix_a_pack_size_ = -1;
  int matrix_b_pack_size_ = -1;
  float *src_b_ = nullptr;
  MatrixPackFun matrix_a_pack_fun_ = nullptr;
  MatrixPackFun matrix_b_pack_fun_ = nullptr;
};
}  // namespace mindspore::kernel
#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_MATMUL_FP32_BASE_H_
