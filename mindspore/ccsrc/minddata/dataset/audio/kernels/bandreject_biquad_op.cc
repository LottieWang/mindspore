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
#include "minddata/dataset/audio/kernels/bandreject_biquad_op.h"

#include "minddata/dataset/audio/kernels/audio_utils.h"
#include "minddata/dataset/util/status.h"

namespace mindspore {
namespace dataset {
Status BandrejectBiquadOp::Compute(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output) {
  IO_CHECK(input, output);
  // check input type and input shape
  TensorShape input_shape = input->shape();
  CHECK_FAIL_RETURN_UNEXPECTED(input_shape.Size() > 0, "BandrejectBiquad: input dimension should be greater than 0.");
  CHECK_FAIL_RETURN_UNEXPECTED(input->type() == DataType(DataType::DE_FLOAT32) ||
                                 input->type() == DataType(DataType::DE_FLOAT16) ||
                                 input->type() == DataType(DataType::DE_FLOAT64),
                               "BandrejectBiquad: input type should be float, but got " + input->type().ToString());
  double w0 = 2 * PI * central_freq_ / sample_rate_;
  double alpha = sin(w0) / 2 / Q_;
  double b0 = 1;
  double b1 = -2 * cos(w0);
  double b2 = 1;
  double a0 = 1 + alpha;
  double a1 = b1;
  double a2 = 1 - alpha;
  if (input->type() == DataType(DataType::DE_FLOAT32))
    return Biquad(input, output, static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(b2),
                  static_cast<float>(a0), static_cast<float>(a1), static_cast<float>(a2));
  else if (input->type() == DataType(DataType::DE_FLOAT64))
    return Biquad(input, output, static_cast<double>(b0), static_cast<double>(b1), static_cast<double>(b2),
                  static_cast<double>(a0), static_cast<double>(a1), static_cast<double>(a2));
  else
    return Biquad(input, output, static_cast<float16>(b0), static_cast<float16>(b1), static_cast<float16>(b2),
                  static_cast<float16>(a0), static_cast<float16>(a1), static_cast<float16>(a2));
}
}  // namespace dataset
}  // namespace mindspore
