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

#ifndef MINDSPORE_MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_LAUNCH_TRANSDATA_H_
#define MINDSPORE_MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_LAUNCH_TRANSDATA_H_

#include <vector>
#include <memory>
#include <string>
#include "runtime/device/ascend/ascend_launch_kernel.h"

namespace mindspore::device::ascend {
class AscendLaunchTransData : public AscendLaunchKernel {
 public:
  AscendLaunchTransData(void *stream, TypeId dtype, size_t total_size, std::string src_format, std::string dst_format,
                        std::vector<size_t> host_shape)
      : AscendLaunchKernel(stream),
        dtype_(dtype),
        total_size_(total_size),
        transdata_graph_(nullptr),
        input_addr_(nullptr),
        src_format_(src_format),
        dst_format_(dst_format),
        shape_(host_shape) {}

  ~AscendLaunchTransData() override = default;

  void SetInputAddr(uint8_t *input_addr) override { input_addr_ = input_addr; }
  void FreeDeviceMem(void *addr) override;
  size_t AlignSizeForLaunchKernel(size_t size) override;
  uint8_t *AllocDeviceMem(size_t size) override;
  void KernelSelect(std::shared_ptr<session::KernelGraph> kernel_graph) override;
  void KernelBuild(std::shared_ptr<session::KernelGraph> kernel_graph) override;

  void LaunchOpKernel() override;
  void FreeLaunchDeviceMem() override;

 protected:
  TypeId dtype_;
  size_t total_size_;
  std::shared_ptr<session::KernelGraph> transdata_graph_;
  uint8_t *input_addr_;
  std::string src_format_;
  std::string dst_format_;
  std::vector<size_t> shape_;

 private:
  std::shared_ptr<session::KernelGraph> ObtainTransDataKernelGraph();
  void ConstructKernelGraphAndSetAttr();
};
}  // namespace mindspore::device::ascend

#endif  // MINDSPORE_MINDSPORE_CCSRC_RUNTIME_DEVICE_ASCEND_ASCEND_LAUNCH_TRANSDATA_H_
