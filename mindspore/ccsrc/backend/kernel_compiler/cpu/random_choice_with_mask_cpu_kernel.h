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
#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RANDOM_CHOICE_WITH_MASK_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RANDOM_CHOICE_WITH_MASK_CPU_KERNEL_H_
#include <vector>
#include <random>
#include <algorithm>
#include "backend/kernel_compiler/cpu/cpu_kernel.h"
#include "backend/kernel_compiler/cpu/cpu_kernel_factory.h"

namespace mindspore {
namespace kernel {

class RandomChoiceWithMaskCPUKernel : public CPUKernel {
 public:
  RandomChoiceWithMaskCPUKernel() = default;
  ~RandomChoiceWithMaskCPUKernel() override = default;

  void InitKernel(const CNodePtr &kernel_node) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs) override;

 private:
  int32_t count_{0};
  std::vector<int64_t> dims_;
  int input_shape_size_{0};
  int seed_{0};
  int seed2_{0};
  int input_size_{1};
  std::mt19937 generator_;
};

MS_REG_CPU_KERNEL(
  RandomChoiceWithMask,
  KernelAttr().AddInputAttr(kNumberTypeBool).AddOutputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeBool),
  RandomChoiceWithMaskCPUKernel);

}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_RANDOM_CHOICE_WITH_MASK_CPU_KERNEL_H_
