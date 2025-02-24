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
#ifndef MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_ONE_HOT_H_
#define MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_ONE_HOT_H_

#include <vector>
#include "src/inner_kernel.h"

namespace mindspore::kernel {
class OneHotCPUKernel : public InnerKernel {
 public:
  OneHotCPUKernel(OpParameter *parameter, const std::vector<lite::Tensor *> &inputs,
                  const std::vector<lite::Tensor *> &outputs, const lite::InnerContext *ctx)
      : InnerKernel(parameter, inputs, outputs, ctx) {}

  ~OneHotCPUKernel() override = default;

  int Init() override;
  int ReSize() override;
  int Run() override;
  int OneHotImpl(int task_id);

 private:
  int InitParamsAndOnOffValue();
  int InitOnOffValueForThreeInputs();
  int InitOnOffValueForFourInputs();

 private:
  int thread_num_ = 1;
  int axis_ = 0;
  int outer_size_ = 0;
  int inner_size_ = 0;
#if defined(ENABLE_ARM) && defined(ENABLE_FP16)
  float16_t on_value_ = 0.;
  float16_t off_value_ = 0.;
#else
  float on_value_ = 0.;
  float off_value_ = 0.;
#endif
};
}  // namespace mindspore::kernel

#endif  // MINDSPORE_LITE_SRC_RUNTIME_KERNEL_ARM_FP32_ONE_HOT_H_
