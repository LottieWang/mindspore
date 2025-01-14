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

#include "backend/kernel_compiler/cpu/nms_with_mask_cpu_kernel.h"
#include "runtime/device/cpu/cpu_device_address.h"

namespace mindspore {
namespace kernel {

int NmsRoundUpPower2(int v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

template <typename T>
void Swap(T *lhs, T *rhs) {
  T tmp = lhs[0];
  lhs[0] = rhs[0];
  rhs[0] = tmp;
}

// Sorting function based on BitonicSort from TopK kernel
template <typename T>
void NMSWithMaskCPUKernel<T>::NmsBitonicSortByKeyKernel(const int outer, const int inner, const int ceil_power2,
                                                        T *input, T *data_buff, int *index_buff, int box_size) {
  auto task1 = [&](int start, int end) {
    for (int i = start; i < end; i++) {
      data_buff[i] = (i < inner) ? input[(i * box_size) + 4] : std::numeric_limits<T>::max();
      index_buff[i] = i;
    }
  };
  CPUKernelUtils::ParallelFor(task1, ceil_power2);

  for (size_t i = 2; i <= static_cast<size_t>(ceil_power2); i <<= 1) {
    for (size_t j = (i >> 1); j > 0; j >>= 1) {
      auto task2 = [&](size_t start, size_t end) {
        for (size_t tid = start; tid < end; tid++) {
          size_t tid_comp = tid ^ j;
          if (tid_comp > tid) {
            if ((tid & i) == 0) {
              if (data_buff[tid] > data_buff[tid_comp]) {
                Swap(&data_buff[tid], &data_buff[tid_comp]);
                Swap(&index_buff[tid], &index_buff[tid_comp]);
              }
            } else {
              if (data_buff[tid] < data_buff[tid_comp]) {
                Swap(&data_buff[tid], &data_buff[tid_comp]);
                Swap(&index_buff[tid], &index_buff[tid_comp]);
              }
            }
          }
        }
      };
      CPUKernelUtils::ParallelFor(task2, ceil_power2);
    }
  }
}

// Initialize per row mask array to all true
template <typename T>
void NMSWithMaskCPUKernel<T>::MaskInit(int numSq, bool *row_mask) {
  auto task = [&](int start, int end) {
    for (int mat_pos = start; mat_pos < end; mat_pos++) {
      row_mask[mat_pos] = true;
    }
  };
  CPUKernelUtils::ParallelFor(task, numSq);
}

// copy data from input to output array sorted by indices returned from bitonic sort
// flips boxes if asked to,  default - false -> if (x1/y1 > x2/y2)
template <typename T>
void NMSWithMaskCPUKernel<T>::PopulateOutput(T *data_in, T *data_out, int *index_buff, const int num, int box_size,
                                             bool flip_mode) {
  auto task = [&](int start, int end) {
    for (int box_num = start; box_num < end; box_num++) {
      int correct_index = index_buff[(num - 1) - box_num];  // flip the array around
      int correct_arr_start = correct_index * box_size;
      int current_arr_start = box_num * box_size;
      if (flip_mode) {  // flip boxes
        // check x
        if (data_in[correct_arr_start + 0] > data_in[correct_arr_start + 2]) {
          data_out[current_arr_start + 0] = data_in[correct_arr_start + 2];
          data_out[current_arr_start + 2] = data_in[correct_arr_start + 0];
        } else {
          data_out[current_arr_start + 0] = data_in[correct_arr_start + 0];
          data_out[current_arr_start + 2] = data_in[correct_arr_start + 2];
        }
        // check y
        if (data_in[correct_arr_start + 1] > data_in[correct_arr_start + 3]) {
          data_out[current_arr_start + 1] = data_in[correct_arr_start + 3];
          data_out[current_arr_start + 3] = data_in[correct_arr_start + 1];
        } else {
          data_out[current_arr_start + 1] = data_in[correct_arr_start + 1];
          data_out[current_arr_start + 3] = data_in[correct_arr_start + 3];
        }
        data_out[current_arr_start + 4] = data_in[correct_arr_start + 4];
      } else {  // default behaviour, don't flip
        for (int x = 0; x < 5; x++) {
          data_out[current_arr_start + x] = data_in[correct_arr_start + x];
        }
      }
    }
  };
  CPUKernelUtils::ParallelFor(task, num);
}

// populated return mask (init to all true) and return index array
template <typename T>
void NMSWithMaskCPUKernel<T>::Preprocess(const int num, int *sel_idx, bool *sel_boxes, T *output, int box_size) {
  auto task = [&](int start, int end) {
    for (int box_num = start; box_num < end; box_num++) {
      sel_idx[box_num] = box_num;
      sel_boxes[box_num] = true;
    }
  };
  CPUKernelUtils::ParallelFor(task, num);
}

template <typename T>
bool NMSWithMaskCPUKernel<T>::IouDecision(T *output, int box_A_ix, int box_B_ix, int box_A_start, int box_B_start,
                                          float IOU_value) {
  T x_1 = std::max(output[box_A_start + 0], output[box_B_start + 0]);
  T y_1 = std::max(output[box_A_start + 1], output[box_B_start + 1]);
  T x_2 = std::min(output[box_A_start + 2], output[box_B_start + 2]);
  T y_2 = std::min(output[box_A_start + 3], output[box_B_start + 3]);
  T width = std::max(x_2 - x_1, T(0));  // in case of no overlap
  T height = std::max(y_2 - y_1, T(0));

  T area1 = (output[box_A_start + 2] - output[box_A_start + 0]) * (output[box_A_start + 3] - output[box_A_start + 1]);
  T area2 = (output[box_B_start + 2] - output[box_B_start + 0]) * (output[box_B_start + 3] - output[box_B_start + 1]);

  T combined_area = area1 + area2;
  return !(((width * height) / (combined_area - (width * height))) > static_cast<T>(IOU_value));
}

// Run parallel NMS pass
// Every position in the row_mask array is updated wit correct IOU decision after being init to all True
template <typename T>
void NMSWithMaskCPUKernel<T>::NmsPass(const int num, const float IOU_value, T *output, bool *sel_boxes, int box_size,
                                      bool *row_mask) {
  auto task = [&](int start, int end) {
    for (int mask_index = start; mask_index < end; mask_index++) {
      int box_i = mask_index / num;                // row in 2d row_mask array
      int box_j = mask_index % num;                // col in 2d row_mask array
      if (box_j > box_i) {                         // skip when box_j index lower/equal to box_i - will remain true
        int box_i_start_index = box_i * box_size;  // adjust starting indices
        int box_j_start_index = box_j * box_size;
        row_mask[mask_index] = IouDecision(output, box_i, box_j, box_i_start_index, box_j_start_index, IOU_value);
      }
    }
  };
  CPUKernelUtils::ParallelFor(task, num * num);
}

// Reduce pass runs on 1 block to allow thread sync
template <typename T>
void NMSWithMaskCPUKernel<T>::ReducePass(const int num, bool *sel_boxes, bool *row_mask) {
  // loop over every box in order of high to low confidence score
  for (int i = 0; i < num - 1; ++i) {
    if (!sel_boxes[i]) {
      continue;
    }
    // every thread handles a different set of boxes (per all boxes in order)
    auto task = [&](int start, int end) {
      for (int j = start; j < end; j++) {
        sel_boxes[j] = sel_boxes[j] && row_mask[i * num + j];
      }
    };
    CPUKernelUtils::ParallelFor(task, num);
  }
}

template <typename T>
void NMSWithMaskCPUKernel<T>::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  iou_value_ = AnfAlgo::GetNodeAttr<float>(kernel_node, "iou_threshold");
  size_t input_num = AnfAlgo::GetInputTensorNum(kernel_node);
  if (input_num != 1) {
    MS_LOG(ERROR) << "Input num is " << input_num << ", but NMSWithMask needs 1 input.";
  }

  size_t output_num = AnfAlgo::GetOutputTensorNum(kernel_node);
  if (output_num != 3) {
    MS_LOG(ERROR) << "Output num is " << output_num << ", but NMSWithMask needs 3 outputs.";
  }
}

template <typename T>
void NMSWithMaskCPUKernel<T>::InitInputOutputSize(const CNodePtr &kernel_node) {
  CPUKernel::InitInputOutputSize(kernel_node);
  auto input_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 0);
  num_input_ = input_shape[0];  //  Get N values in  [N, 5] data.
  ceil_power_2 = NmsRoundUpPower2(num_input_);

  workspace_size_list_.push_back(ceil_power_2 * sizeof(T));                //  data buff
  workspace_size_list_.push_back(ceil_power_2 * sizeof(int));              //  index buff
  workspace_size_list_.push_back(num_input_ * num_input_ * sizeof(bool));  //  mask list
}

template <typename T>
bool NMSWithMaskCPUKernel<T>::Launch(const std::vector<kernel::AddressPtr> &inputs,
                                     const std::vector<kernel::AddressPtr> &workspace,
                                     const std::vector<kernel::AddressPtr> &outputs) {
  auto input = reinterpret_cast<T *>(inputs[0]->addr);
  auto data_buff = reinterpret_cast<T *>(workspace[0]->addr);
  auto index_buff = reinterpret_cast<int *>(workspace[1]->addr);
  auto row_mask = reinterpret_cast<bool *>(workspace[2]->addr);
  auto output = reinterpret_cast<T *>(outputs[0]->addr);
  auto sel_idx = reinterpret_cast<int *>(outputs[1]->addr);
  auto sel_boxes = reinterpret_cast<bool *>(outputs[2]->addr);

  NmsBitonicSortByKeyKernel(1, num_input_, ceil_power_2, input, data_buff, index_buff, box_size_);
  int total_val = num_input_ * num_input_;
  MaskInit(total_val, row_mask);
  PopulateOutput(input, output, index_buff, num_input_, box_size_, false);
  Preprocess(num_input_, sel_idx, sel_boxes, output, box_size_);
  NmsPass(num_input_, iou_value_, output, sel_boxes, box_size_, row_mask);
  ReducePass(num_input_, sel_boxes, row_mask);
  return true;
}

}  // namespace kernel
}  // namespace mindspore
