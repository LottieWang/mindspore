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

#include "src/runtime/kernel/arm/int8/group_convolution_int8.h"
#include "src/runtime/kernel/arm/int8/convolution_int8_creator.h"

using mindspore::lite::RET_OK;

namespace mindspore::kernel {
int GroupConvolutionInt8CPUKernel::SeparateInput(int group_id) {
  int in_plane = conv_param_->input_h_ * conv_param_->input_w_ * conv_param_->input_batch_;
  int sub_in_channel = conv_param_->input_channel_;
  int ori_in_channel = sub_in_channel * group_num_;
  auto sub_in_data =
    reinterpret_cast<int8_t *>(static_cast<lite::Tensor *>(group_convs_.at(group_id)->in_tensors().front())->data_c());
  int8_t *src_ptr = reinterpret_cast<int8_t *>(ori_in_data_) + group_id * sub_in_channel;
  int8_t *dst_ptr = sub_in_data;
  for (int i = 0; i < in_plane; ++i) {
    memcpy(dst_ptr, src_ptr, static_cast<size_t>(sub_in_channel) * sizeof(int8_t));
    src_ptr += ori_in_channel;
    dst_ptr += sub_in_channel;
  }
  return RET_OK;
}

int GroupConvolutionInt8CPUKernel::PostConcat(int group_id) {
  int out_plane = conv_param_->output_h_ * conv_param_->output_w_ * conv_param_->output_batch_;
  int sub_out_channel = conv_param_->output_channel_;
  int ori_out_channel = sub_out_channel * group_num_;
  auto sub_out_data =
    reinterpret_cast<int8_t *>(static_cast<lite::Tensor *>(group_convs_.at(group_id)->out_tensors().front())->data_c());
  int8_t *src_ptr = sub_out_data;
  int8_t *dst_ptr = reinterpret_cast<int8_t *>(ori_out_data_) + group_id * sub_out_channel;
  for (int i = 0; i < out_plane; ++i) {
    memcpy(dst_ptr, src_ptr, static_cast<size_t>(sub_out_channel) * sizeof(int8_t));
    src_ptr += sub_out_channel;
    dst_ptr += ori_out_channel;
  }
  return RET_OK;
}

int GroupConvolutionInt8CPUKernel::Init() {
  if (group_conv_creator_ == nullptr) {
    return lite::RET_ERROR;
  }
  group_conv_creator_->SetShapeOfTensors();
  for (int i = 0; i < conv_param_->group_; ++i) {
    auto *new_conv_param = CreateNewConvParameter(conv_param_);
    std::vector<lite::Tensor *> new_inputs;
    std::vector<lite::Tensor *> new_outputs;
    auto ret = group_conv_creator_->GetSingleConvParam(new_conv_param, &new_inputs, &new_outputs, i);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "GetSingleConv for fp32 group conv failed.";
      return lite::RET_ERROR;
    }
    group_conv_creator_->CopyQuantParam(&new_inputs);
    group_convs_.emplace_back(
      CpuConvInt8KernelSelect(new_inputs, new_outputs, reinterpret_cast<OpParameter *>(new_conv_param), ctx_));
  }
  return GroupConvolutionBaseCPUKernel::Init();
}
}  // namespace mindspore::kernel
