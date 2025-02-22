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
#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_AUDIO_KERNELS_AUDIO_UTILS_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_AUDIO_KERNELS_AUDIO_UTILS_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "minddata/dataset/core/tensor.h"
#include "minddata/dataset/kernels/tensor_op.h"
#include "minddata/dataset/util/status.h"

constexpr double PI = 3.141592653589793;
namespace mindspore {
namespace dataset {
/// \brief Turn a tensor from the power/amplitude scale to the decibel scale.
/// \param input/output: Tensor of shape <...,freq,time>
/// \param multiplier: power - 10, amplitude - 20
/// \param amin: lower bound
/// \param db_multiplier: multiplier for decibels
/// \param top_db: the lower bound for decibels cut-off
/// \return Status code
template <typename T>
Status AmplitudeToDB(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output, T multiplier, T amin,
                     T db_multiplier, T top_db);

/// \brief Calculate the angles of the complex numbers
/// \param input/output: Tensor of shape <...,time>
template <typename T>
Status Angle(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output) {
  TensorShape shape = input->shape();
  std::vector output_shape = shape.AsVector();
  output_shape.pop_back();
  std::shared_ptr<Tensor> output_tensor;
  std::vector<T> out;
  T o;
  T x;
  T y;
  for (auto itr = input->begin<T>(); itr != input->end<T>(); itr++) {
    x = static_cast<T>(*itr);
    itr++;
    y = static_cast<T>(*itr);
    o = std::atan2(y, x);
    out.emplace_back(o);
  }
  // Generate multidimensional results corresponding to input
  Tensor::CreateFromVector(out, TensorShape{output_shape}, &output_tensor);
  *output = output_tensor;
  return Status::OK();
}

/// \brief Perform a biquad filter of input tensor.
/// \param input/output: Tensor of shape <...,time>
/// \param a0: denominator coefficient of current output y[n], typically 1
/// \param a1: denominator coefficient of current output y[n-1]
/// \param a2: denominator coefficient of current output y[n-2]
/// \param b0: numerator coefficient of current input, x[n]
/// \param b1: numerator coefficient of input one time step ago x[n-1]
/// \param b2: numerator coefficient of input two time steps ago x[n-2]
/// \return Status code
template <typename T>
Status Biquad(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output, T b0, T b1, T b2, T a0, T a1,
              T a2) {
  std::vector<T> a_coeffs;
  std::vector<T> b_coeffs;
  a_coeffs.push_back(a0);
  a_coeffs.push_back(a1);
  a_coeffs.push_back(a2);
  b_coeffs.push_back(b0);
  b_coeffs.push_back(b1);
  b_coeffs.push_back(b2);
  return LFilter(input, output, a_coeffs, b_coeffs, true);
}

/// \brief Perform an IIR filter by evaluating difference equation.
/// \param input/output: Tensor of shape <...,time>
/// \param a_coeffs: denominator coefficients of difference equation of dimension of (n_order + 1).
/// \param b_coeffs: numerator coefficients of difference equation of dimension of (n_order + 1).
/// \param clamp: If True, clamp the output signal to be in the range [-1, 1] (Default: True)
/// \return Status code
template <typename T>
Status LFilter(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output, std::vector<T> a_coeffs,
               std::vector<T> b_coeffs, bool clamp) {
  //  pack batch
  TensorShape input_shape = input->shape();
  TensorShape toShape({input->Size() / input_shape[-1], input_shape[-1]});
  input->Reshape(toShape);
  auto shape_0 = input->shape()[0];
  auto shape_1 = input->shape()[1];
  std::vector<T> signal;
  std::shared_ptr<Tensor> out;
  std::vector<T> out_vect(shape_0 * shape_1);
  size_t x_idx = 0;
  size_t channel_idx = 1;
  size_t m_num_order = b_coeffs.size() - 1;
  size_t m_den_order = a_coeffs.size() - 1;
  // init  A_coeffs and B_coeffs by div(a0)
  for (size_t i = 1; i < a_coeffs.size(); i++) {
    a_coeffs[i] /= a_coeffs[0];
  }
  for (size_t i = 0; i < b_coeffs.size(); i++) {
    b_coeffs[i] /= a_coeffs[0];
  }
  // Sliding window
  T *m_px = new T[m_num_order + 1];
  T *m_py = new T[m_den_order + 1];

  // Tensor -> vector
  for (auto itr = input->begin<T>(); itr != input->end<T>();) {
    while (x_idx < shape_1 * channel_idx) {
      signal.push_back(*itr);
      itr++;
      x_idx++;
    }
    // Sliding window
    for (size_t j = 0; j < m_den_order; j++) {
      m_px[j] = static_cast<T>(0);
    }
    for (size_t j = 0; j <= m_den_order; j++) {
      m_py[j] = static_cast<T>(0);
    }
    // Each channel is processed with the sliding window
    for (size_t i = x_idx - shape_1; i < x_idx; i++) {
      m_px[m_num_order] = signal[i];
      for (size_t j = 0; j < m_num_order + 1; j++) {
        m_py[m_num_order] += b_coeffs[j] * m_px[m_num_order - j];
      }
      for (size_t j = 1; j < m_den_order + 1; j++) {
        m_py[m_num_order] -= a_coeffs[j] * m_py[m_num_order - j];
      }
      if (clamp) {
        if (m_py[m_num_order] > static_cast<T>(1.))
          out_vect[i] = static_cast<T>(1.);
        else if (m_py[m_num_order] < static_cast<T>(-1.))
          out_vect[i] = static_cast<T>(-1.);
        else
          out_vect[i] = m_py[m_num_order];
      } else {
        out_vect[i] = m_py[m_num_order];
      }
      if (i + 1 == x_idx) continue;
      for (size_t j = 0; j < m_num_order; j++) {
        m_px[j] = m_px[j + 1];
      }
      for (size_t j = 0; j < m_num_order; j++) {
        m_py[j] = m_py[j + 1];
      }
      m_py[m_num_order] = static_cast<T>(0);
    }
    if (x_idx % shape_1 == 0) {
      ++channel_idx;
    }
  }
  // unpack batch
  Tensor::CreateFromVector(out_vect, input_shape, &out);
  *output = out;
  delete m_px;
  delete m_py;
  return Status::OK();
}

/// \brief Stretch STFT in time at a given rate, without changing the pitch.
/// \param[in] input - Tensor of shape <...,freq,time>.
/// \param[in] rate - Stretch factor.
/// \param[in] phase_advance - Expected phase advance in each bin.
/// \param[out] output - Tensor after stretch in time domain.
/// \return Status return code
Status TimeStretch(std::shared_ptr<Tensor> input, std::shared_ptr<Tensor> *output, float rate, float hop_length,
                   float n_freq);

/// \brief Apply a mask along axis.
/// \param input: Tensor of shape <...,freq,time>
/// \param output: Tensor of shape <...,freq,time>
/// \param mask_param: Number of columns to be masked will be uniformly sampled from [0, mask_param]
/// \param mask_value: Value to assign to the masked columns
/// \param axis: Axis to apply masking on (1 -> frequency, 2 -> time)
/// \param rnd: Number generator
/// \return Status code
Status RandomMaskAlongAxis(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output, int64_t mask_param,
                           double mask_value, int axis, std::mt19937 rnd);

/// \brief Apply a mask along axis. All examples will have the same mask interval.
/// \param input: Tensor of shape <...,freq,time>
/// \param output: Tensor of shape <...,freq,time>
/// \param mask_width: The width of the mask
/// \param mask_start: Starting position of the mask
///     Mask will be applied from indices [mask_start, mask_start + mask_width)
/// \param mask_value: Value to assign to the masked columns
/// \param axis: Axis to apply masking on (1 -> frequency, 2 -> time)
/// \return Status code
Status MaskAlongAxis(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output, int64_t mask_width,
                     int64_t mask_start, double mask_value, int axis);
}  // namespace dataset
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_AUDIO_KERNELS_AUDIO_UTILS_H_
