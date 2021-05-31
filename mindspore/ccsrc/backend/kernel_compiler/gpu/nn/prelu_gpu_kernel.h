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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_PRELU_GPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_PRELU_GPU_KERNEL_H_

#include <vector>
#include <map>
#include <string>
#include "backend/kernel_compiler/gpu/gpu_kernel.h"
#include "backend/kernel_compiler/gpu/gpu_kernel_factory.h"
#include "backend/kernel_compiler/gpu/cuda_impl/relu_impl.cuh"

namespace mindspore {
namespace kernel {
template <typename T>
class PReLUGpuKernel : public GpuKernel {
 public:
  PReLUGpuKernel() { ResetResource(); }
  ~PReLUGpuKernel() override {}
  const std::vector<size_t> &GetInputSizeList() const override { return input_size_list_; }
  const std::vector<size_t> &GetOutputSizeList() const override { return output_size_list_; }
  const std::vector<size_t> &GetWorkspaceSizeList() const override { return workspace_size_list_; }

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override {
    if (is_null_input_) {
      return true;
    }
    T *input = GetDeviceAddress<T>(inputs, 0);
    T *weight = GetDeviceAddress<T>(inputs, 1);
    T *output = GetDeviceAddress<T>(outputs, 0);

    const int size = input_size_ / sizeof(T);
    CalPReLU(size, input, weight, output, reinterpret_cast<cudaStream_t>(stream_ptr));
    return true;
  }
  bool Init(const CNodePtr &kernel_node) override {
    size_t input_num = AnfAlgo::GetInputTensorNum(kernel_node);
    if (input_num != 2) {
      MS_LOG(ERROR) << "Argument number is " << input_num << ", but ReLUGpuFwdKernel needs 2.";
      return false;
    }
    auto input_shape = AnfAlgo::GetInputRealDeviceShapeIfExist(kernel_node, 0);
    is_null_input_ = CHECK_NULL_INPUT(input_shape);
    if (is_null_input_) {
      MS_LOG(WARNING) << "PReLUGpuFwdKernel input is null.";
    }
    size_t size = 1;
    for (size_t i = 0; i < input_shape.size(); i++) {
      size *= input_shape[i];
    }
    input_size_ = size * sizeof(T);

    auto weight_shape = AnfAlgo::GetInputRealDeviceShapeIfExist(kernel_node, 1);
    is_null_input_ = CHECK_NULL_INPUT(weight_shape);
    if (is_null_input_) {
      MS_LOG(WARNING) << "PReLUGpuFwdKernel weight is null.";
    }
    size = 1;
    for (size_t i = 0; i < weight_shape.size(); i++) {
      size *= weight_shape[i];
    }
    weight_size_ = size * sizeof(T);

    InitSizeLists();
    return true;
  }

  void ResetResource() noexcept override {
    is_null_input_ = false;
    input_size_list_.clear();
    output_size_list_.clear();
    workspace_size_list_.clear();
    input_size_ = 0;
    workspace_size_ = 0;
  }

 protected:
  void InitSizeLists() override {
    input_size_list_.push_back(input_size_);
    output_size_list_.push_back(input_size_);
    workspace_size_list_.push_back(workspace_size_);
  }

 private:
  bool is_null_input_;
  std::vector<size_t> input_size_list_;
  std::vector<size_t> output_size_list_;
  std::vector<size_t> workspace_size_list_;
  size_t input_size_;
  size_t weight_size_;
  size_t workspace_size_;
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_PRELU_GPU_KERNEL_H_