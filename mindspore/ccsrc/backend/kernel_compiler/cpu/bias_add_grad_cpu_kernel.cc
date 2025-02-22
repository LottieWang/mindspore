/**
 * Copyright 2019 Huawei Technologies Co., Ltd
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

#include "backend/kernel_compiler/cpu/bias_add_grad_cpu_kernel.h"
#include "nnacl/fp32/reduce_fp32.h"
namespace mindspore {
namespace kernel {
void BiasAddGradCPUKernel::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  input_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, 0);
  if (input_shape_.size() < 2) {
    MS_LOG(EXCEPTION) << "Input tensor's rank must be at least 2 for 'BiasAddGrad' Op, but input tensor's rank is "
                      << input_shape_.size();
  }
}

bool BiasAddGradCPUKernel::Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
                                  const std::vector<AddressPtr> &outputs) {
  if (inputs.size() != 1 || outputs.size() != 1) {
    MS_LOG(EXCEPTION) << "input output size not support";
  }
  auto output_addr = reinterpret_cast<float *>(outputs[0]->addr);
  auto input_addr = reinterpret_cast<float *>(inputs[0]->addr);

  if (input_shape_.size() > 2) {
    size_t hw_size = 1;
    for (size_t i = 2; i < input_shape_.size(); ++i) {
      hw_size *= input_shape_[i];
    }

    size_t c_size = input_shape_[1];
    for (size_t c = 0; c < c_size; ++c) {
      output_addr[c] = 0;
      for (size_t n = 0; n < input_shape_[0]; ++n) {
        size_t offset = n * c_size * hw_size + c * hw_size;
        for (size_t hw = 0; hw < hw_size; ++hw) {
          output_addr[c] += input_addr[offset + hw];
        }
      }
    }
  } else if (input_shape_.size() == 2) {
    auto task = [&](size_t start, size_t end) {
      ReduceSumDim2Axis0(end - start, input_shape_[1], input_shape_[0], input_addr + start, output_addr + start);
    };
    ParallelLaunchAutoSearch(task, input_shape_[1], this, &parallel_search_info_);
  }
  return true;
}
}  // namespace kernel
}  // namespace mindspore
