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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_CONVOLUTION_DEPTHWISE_INDIRECT_FP32_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_CONVOLUTION_DEPTHWISE_INDIRECT_FP32_H_

#include <vector>
#include "src/inner_kernel.h"
#include "src/runtime/kernel/arm/base/convolution_base.h"
#include "nnacl/fp32/conv_depthwise_fp32.h"

namespace mindspore::kernel {
class ConvolutionDepthwiseIndirectCPUKernel : public ConvolutionBaseCPUKernel {
 public:
  ConvolutionDepthwiseIndirectCPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                                        const std::vector<lite::Tensor *> &outputs, const lite::InnerContext *ctx)
      : ConvolutionBaseCPUKernel(parameter, inputs, outputs, ctx, inputs.at(kWeightIndex)->data_c(),
                                 inputs.size() == kInputSize2 ? inputs.at(kBiasIndex)->data_c() : nullptr) {}
  ~ConvolutionDepthwiseIndirectCPUKernel() override;

  int Init() override;
  int ReSize() override;
  int Run() override;

  int Execute(int task_id);
  int Eval() override;

 private:
  int MallocIndirectBuffer();
  int MallocPackedInput();
  int MallocWeightBiasData() override;
  void PackWeight() override;
  int step_w = 0;
  int step_h = 0;
  float **indirect_buffer_ = nullptr;
  float *zero_ptr_ = nullptr;
  float *output_ptr_ = nullptr;
  float *packed_input_ = nullptr;
};
}  // namespace mindspore::kernel

#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_CONVOLUTION_DEPTHWISE_INDIRECT_FP32_H_
