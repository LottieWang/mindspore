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

#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP16_GROUP_CONVOLUTION_FP16_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP16_GROUP_CONVOLUTION_FP16_H_

#include <utility>
#include <vector>
#include "src/inner_kernel.h"
#include "nnacl/op_base.h"
#include "src/runtime/kernel/arm/base/group_convolution_base.h"
#include "nnacl/fp16/conv_fp16.h"

namespace mindspore::kernel {
class GroupConvolutionFP16CPUKernel : public GroupConvolutionBaseCPUKernel {
 public:
  GroupConvolutionFP16CPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                                const std::vector<lite::Tensor *> &outputs, const lite::InnerContext *ctx,
                                GroupConvCreator *group_conv_creator, const int group_num)
      : GroupConvolutionBaseCPUKernel(parameter, inputs, outputs, ctx, group_conv_creator, group_num) {
  }  // opParameter(in channel, out channel) in this kernel has been split to groups, if
  // you want to get real params, multiply in channel / out channel with group num
  ~GroupConvolutionFP16CPUKernel() override = default;
  int Init() override;
  int SeparateInput(int group_id) override;
  int PostConcat(int group_id) override;
};
}  // namespace mindspore::kernel

#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP16_GROUP_CONVOLUTION_FP16_H_
