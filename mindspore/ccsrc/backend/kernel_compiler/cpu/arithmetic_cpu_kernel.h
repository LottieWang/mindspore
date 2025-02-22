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
#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ARITHMETIC_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ARITHMETIC_CPU_KERNEL_H_
#include <memory>
#include <vector>
#include <limits>
#include "backend/kernel_compiler/cpu/cpu_kernel.h"
#include "backend/kernel_compiler/cpu/cpu_kernel_factory.h"
#include "nnacl/arithmetic.h"

const float MAX_SUB_SERIAL_SIZE = 10000;
const float MAX_DIV_SERIAL_SIZE = 10000;
const float MAX_POW_SERIAL_SIZE = 700;

namespace mindspore {
namespace kernel {
template <typename T>
class ArithmeticCPUKernel : public CPUKernel {
 public:
  ArithmeticCPUKernel() = default;
  ~ArithmeticCPUKernel() override = default;

  void InitKernel(const CNodePtr &kernel_node) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override;

 private:
  void Sub(const T *input1, const T *input2, T *out);
  void Add(const T *input1, const T *input2, T *out);
  void Mul(const T *input1, const T *input2, T *out);
  void RealDiv(const T *input1, const T *input2, T *out);
  void Div(const T *input1, const T *input2, T *out);
  void FloorDiv(const T *input1, const T *input2, T *out);
  void Mod(const T *input1, const T *input2, T *out);
  void FloorMod(const T *input1, const T *input2, T *out);
  void Pow(const T *input1, const T *input2, T *out);
  void AssignAdd(T *input1, const T *input2, T *out);
  void Atan2(const T *input1, const T *input2, T *out);
  void SquaredDifference(const T *input1, const T *input2, T *out);
  std::vector<size_t> input_shape1_;
  std::vector<size_t> input_shape2_;
  std::vector<size_t> input_element_num1_;
  std::vector<size_t> input_element_num2_;
  std::vector<size_t> output_shape_;
  std::vector<size_t> output_element_num_;
  size_t output_size_;
  ArithmeticParameter op_para;
  OperateType operate_type_{ADD};
  TypeId dtype_{kTypeUnknown};
  TypeId target_dtype_{kTypeUnknown};
};

MS_REG_CPU_KERNEL_T(Sub, KernelAttr(), ArithmeticCPUKernel, int32_t);
MS_REG_CPU_KERNEL_T(Sub, KernelAttr(), ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(Sub, KernelAttr(), ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(Pow, KernelAttr(), ArithmeticCPUKernel, int32_t);
MS_REG_CPU_KERNEL_T(Pow, KernelAttr(), ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(Pow, KernelAttr(), ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(RealDiv, KernelAttr(), ArithmeticCPUKernel, int32_t);
MS_REG_CPU_KERNEL_T(RealDiv, KernelAttr(), ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(RealDiv, KernelAttr(), ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(Div, KernelAttr(), ArithmeticCPUKernel, int32_t);
MS_REG_CPU_KERNEL_T(Div, KernelAttr(), ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(Div, KernelAttr(), ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(Mul, KernelAttr(), ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(Mul, KernelAttr(), ArithmeticCPUKernel, int32_t);
MS_REG_CPU_KERNEL_T(
  FloorDiv, KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
  ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(
  FloorDiv, KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
  ArithmeticCPUKernel, int);
MS_REG_CPU_KERNEL_T(
  FloorDiv,
  KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
  ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(
  Mod, KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
  ArithmeticCPUKernel, int);
MS_REG_CPU_KERNEL_T(
  Mod, KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
  ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(
  Mod, KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
  ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(
  FloorMod, KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
  ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(
  FloorMod, KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
  ArithmeticCPUKernel, int);
MS_REG_CPU_KERNEL_T(
  FloorMod,
  KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
  ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(
  FloorMod,
  KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeFloat16).AddOutputAttr(kNumberTypeFloat16),
  ArithmeticCPUKernel, float16);
MS_REG_CPU_KERNEL_T(
  AssignAdd, KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
  ArithmeticCPUKernel, int);
MS_REG_CPU_KERNEL_T(
  AssignAdd, KernelAttr().AddInputAttr(kNumberTypeInt64).AddInputAttr(kNumberTypeInt64).AddOutputAttr(kNumberTypeInt64),
  ArithmeticCPUKernel, int64_t);
MS_REG_CPU_KERNEL_T(
  SquaredDifference,
  KernelAttr().AddInputAttr(kNumberTypeInt32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeInt32),
  ArithmeticCPUKernel, int);
MS_REG_CPU_KERNEL_T(
  SquaredDifference,
  KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeFloat16).AddOutputAttr(kNumberTypeFloat16),
  ArithmeticCPUKernel, float16);
MS_REG_CPU_KERNEL_T(
  SquaredDifference,
  KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
  ArithmeticCPUKernel, float);
MS_REG_CPU_KERNEL_T(
  Atan2,
  KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeFloat32).AddOutputAttr(kNumberTypeFloat32),
  ArithmeticCPUKernel, float);
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ARITHMETIC_CPU_KERNEL_H_
