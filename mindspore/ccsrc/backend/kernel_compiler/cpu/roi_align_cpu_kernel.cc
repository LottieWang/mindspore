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

#include "backend/kernel_compiler/cpu/roi_align_cpu_kernel.h"
#include "runtime/device/cpu/cpu_device_address.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kInputSize = 2;
constexpr size_t kOutputSize = 1;
}  //  namespace

template <typename T>
void ROIAlignCPUKernel<T>::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  //  Get the input shapes
  auto x_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 0);
  auto rois_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 1);

  auto x_shape_size = x_shape.size();
  if (x_shape_size != 4) {
    MS_LOG(ERROR) << "x shape size is " << x_shape_size << ", but should be 4.";
  }

  channels_ = x_shape[1];
  height_ = x_shape[2];
  width_ = x_shape[3];

  roi_rows_ = rois_shape[0];
  roi_cols_ = rois_shape[1];

  pooled_height_ = static_cast<int>(AnfAlgo::GetNodeAttr<int64_t>(kernel_node, "pooled_height"));
  pooled_width_ = static_cast<int>(AnfAlgo::GetNodeAttr<int64_t>(kernel_node, "pooled_width"));
  spatial_scale_ = static_cast<T>(AnfAlgo::GetNodeAttr<float>(kernel_node, "spatial_scale"));
  sample_num_ = static_cast<int>(AnfAlgo::GetNodeAttr<int64_t>(kernel_node, "sample_num"));
  roi_end_mode_ = static_cast<int>(AnfAlgo::GetNodeAttr<int64_t>(kernel_node, "roi_end_mode"));
}

template <typename T>
bool ROIAlignCPUKernel<T>::Launch(const std::vector<kernel::AddressPtr> &inputs,
                                  const std::vector<kernel::AddressPtr> &,
                                  const std::vector<kernel::AddressPtr> &outputs) {
  const T *input = reinterpret_cast<T *>(inputs[0]->addr);
  const T *rois = reinterpret_cast<T *>(inputs[1]->addr);
  auto out_data = reinterpret_cast<T *>(outputs[0]->addr);

  size_t elem_num = roi_rows_ * channels_ * pooled_height_ * pooled_width_;
  auto task = [&](size_t start, size_t end) {
    for (size_t thread_idx = start; thread_idx < end; thread_idx++) {
      int n = thread_idx / pooled_width_ / pooled_height_ / channels_;
      const T *roi_box = rois + n * roi_cols_;
      if (roi_box[1] < static_cast<T>(0.001) && roi_box[3] < static_cast<T>(0.001) &&
          roi_box[1] > static_cast<T>(-0.001) && roi_box[3] > static_cast<T>(-0.001)) {
        continue;
      }
      int offset = -1;
      int c, ph, pw, roi_bin_grid_h, roi_bin_grid_w;
      T bin_size_h, bin_size_w, roi_start_h, roi_start_w;

      bin_box(thread_idx, rois, roi_cols_, spatial_scale_, sample_num_, roi_end_mode_, channels_, height_, width_,
              pooled_height_, pooled_width_, &offset, &n, &c, &ph, &pw, &roi_bin_grid_h, &roi_bin_grid_w, &bin_size_h,
              &bin_size_w, &roi_start_h, &roi_start_w);

      // (n, c, ph, pw) is the base param of pooled map
      const T count_points_in_grid_cell = static_cast<T>(roi_bin_grid_h * roi_bin_grid_w);

      T accumulate_val = static_cast<T>(0.);
      for (int iy = 0; iy < roi_bin_grid_h; iy++) {
        // Shift half point RIGHT for y / x,  while previous scaled roi shift half point LEFT
        const T y = roi_start_h + static_cast<T>(ph) * bin_size_h +
                    static_cast<T>(iy + .5f) * bin_size_h / static_cast<T>(roi_bin_grid_h);
        for (int ix = 0; ix < roi_bin_grid_w; ix++) {
          const T x = roi_start_w + static_cast<T>(pw) * bin_size_w +
                      static_cast<T>(ix + .5f) * bin_size_w / static_cast<T>(roi_bin_grid_w);
          // bilinear interpolate by shifted y / x
          // calculate bilinear interpolation
          int x_low = 0, y_low = 0, x_high = 0, y_high = 0;
          T w1, w2, w3, w4;
          bilinear_interpolate(height_, width_, y, x, &x_low, &y_low, &x_high, &y_high, &w1, &w2, &w3, &w4);
          if (x_low >= 0 && x_high >= 0 && y_low >= 0 && y_high >= 0 && y_low < height_ && y_high < height_ &&
              x_low < width_ && x_high < width_) {
            T v1 = input[offset + y_low * width_ + x_low];
            T v2 = input[offset + y_low * width_ + x_high];
            T v3 = input[offset + y_high * width_ + x_low];
            T v4 = input[offset + y_high * width_ + x_high];

            T val = (w1 * v1 + w2 * v2 + w3 * v3 + w4 * v4);
            accumulate_val += val;
          }
        }
      }
      accumulate_val /= count_points_in_grid_cell;

      out_data[thread_idx] = accumulate_val;
    }
  };
  CPUKernelUtils::ParallelFor(task, elem_num);

  return true;
}

template <typename T>
void ROIAlignCPUKernel<T>::CheckParam(const std::vector<kernel::AddressPtr> &inputs,
                                      const std::vector<kernel::AddressPtr> &outputs) {
  if (inputs.size() != kInputSize) {
    MS_LOG(EXCEPTION) << "Input number is: " << inputs.size() << ", but ROIAlign needs " << kInputSize << " inputs.";
  }

  if (outputs.size() != kOutputSize) {
    MS_LOG(EXCEPTION) << "Output number is: " << outputs.size() << ", but ROIAlign needs " << kOutputSize << "outputs.";
  }
}

template <typename T>
void ROIAlignCPUKernel<T>::bilinear_interpolate(const int height, const int width, T y, T x, int *x_low, int *y_low,
                                                int *x_high, int *y_high, T *w1, T *w2, T *w3, T *w4) {
  constexpr float eps = 0.00007;
  if (y < static_cast<T>(-1.0) || y > static_cast<T>(height) || x < static_cast<T>(-1.0) || x > static_cast<T>(width)) {
    *w1 = *w2 = *w3 = *w4 = static_cast<T>(0);
    *x_low = *x_high = *y_low = *y_high = -1;
    return;
  }

  // low bounder is at least zero
  y = y <= static_cast<T>(.0) ? static_cast<T>(.0) : y;
  x = x <= static_cast<T>(.0) ? static_cast<T>(.0) : x;

  // top left point
  *y_low = (y <= static_cast<T>(eps) ? 0 : static_cast<int>(floor(y)));
  *x_low = (x <= static_cast<T>(eps) ? 0 : static_cast<int>(floor(x)));

  // bottom right point
  if (*y_low >= height - 1) {
    *y_high = *y_low = height - 1;
    y = static_cast<T>(*y_low);
  } else {
    *y_high = *y_low + 1;
  }

  if (*x_low >= width - 1) {
    *x_high = *x_low = width - 1;
    x = static_cast<T>(*x_low);
  } else {
    *x_high = *x_low + 1;
  }

  // distance to nearest points
  T lx, ly, hx, hy;
  ly = y - static_cast<T>(*y_low), lx = x - static_cast<T>(*x_low);
  hy = static_cast<T>(1.) - ly, hx = static_cast<T>(1.) - lx;

  // weight is evaluated by the distance to point away.
  //   the closer to point home, the more weight, the farther to point away.
  *w1 = hy * hx, *w2 = hy * lx, *w3 = ly * hx, *w4 = ly * lx;
  return;
}

template <typename T>
void ROIAlignCPUKernel<T>::bin_box(int thread_idx, const T *roi_boxes, int roi_cols, const T spatial_scale,
                                   const int sample_num, int roi_end_mode, const int channels, const int height,
                                   const int width, const int pooled_height, const int pooled_width, int *offset,
                                   int *n, int *c, int *ph, int *pw, int *roi_bin_grid_h, int *roi_bin_grid_w,
                                   T *bin_size_h, T *bin_size_w, T *roi_start_h, T *roi_start_w) {
  // (n, c, ph, pw) is the base param of pooled map
  *pw = thread_idx % pooled_width;
  *ph = (thread_idx / pooled_width) % pooled_height;
  *c = (thread_idx / pooled_width / pooled_height) % channels;
  *n = thread_idx / pooled_width / pooled_height / channels;

  // Roi has
  //   1. 4 points, or
  //   2. indicator + 4 points (1 + 4)
  const T *roi_box = roi_boxes + (*n) * roi_cols;
  int roi_batch_ind = 0;
  if (roi_cols == 5) {
    roi_batch_ind = static_cast<int>(rint(static_cast<float>(roi_box[0]) + static_cast<float>(0.00007)));
    roi_box++;
  }

  // Scale and shift ROI
  *roi_start_w = roi_box[0] * spatial_scale;
  *roi_start_h = roi_box[1] * spatial_scale;
  T roi_end_w = (roi_box[2] + static_cast<T>(roi_end_mode)) * spatial_scale;
  T roi_end_h = (roi_box[3] + static_cast<T>(roi_end_mode)) * spatial_scale;

  // New ROI height/width
  T roi_width = roi_end_w - (*roi_start_w);
  T roi_height = roi_end_h - (*roi_start_h);

  if (roi_end_mode == 0) {  // backward compatibility
    // Force malformed ROIs to be 1x1
    roi_width = roi_width > static_cast<T>(1.0) ? roi_width : static_cast<T>(1.0);
    roi_height = roi_height > static_cast<T>(1.0) ? roi_height : static_cast<T>(1.0);
  }

  // ratio of roi / pooled
  *bin_size_h = static_cast<T>(roi_height) / static_cast<T>(pooled_height);
  *bin_size_w = static_cast<T>(roi_width) / static_cast<T>(pooled_width);

  *offset = (roi_batch_ind * channels + (*c)) * height * width;

  // grid (int) by Sample ratio if defined, otherwise by pooled H/W
  *roi_bin_grid_h = (sample_num > 0) ? sample_num : static_cast<int>(floor(roi_height / static_cast<T>(pooled_height)));
  *roi_bin_grid_w = (sample_num > 0) ? sample_num : static_cast<int>(floor(roi_width / static_cast<T>(pooled_width)));
  return;
}

}  // namespace kernel
}  // namespace mindspore
