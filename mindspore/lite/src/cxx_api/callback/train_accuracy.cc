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
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include "include/train/classification_train_accuracy_monitor.h"
#include "include/api/callback/train_accuracy.h"
#include "src/cxx_api/callback/callback_impl.h"
#include "src/common/log_adapter.h"

namespace mindspore {
TrainAccuracy::TrainAccuracy(int print_every_n, int accuracy_metrics, const std::vector<int> &input_indexes,
                             const std::vector<int> &output_indexes) {
  callback_impl_ = new CallbackImpl(
    new lite::ClassificationTrainAccuracyMonitor(print_every_n, accuracy_metrics, input_indexes, output_indexes));
}

TrainAccuracy::~TrainAccuracy() {
  if (callback_impl_ == nullptr) {
    MS_LOG(ERROR) << "Callback implement is null.";
  }
  auto internal_call_back = callback_impl_->GetInternalCallback();
  if (internal_call_back != nullptr) {
    delete internal_call_back;
  }
}

const std::vector<GraphPoint> &TrainAccuracy::GetAccuracyPoints() {
  static std::vector<GraphPoint> empty_vector;
  if (callback_impl_ == nullptr) {
    MS_LOG(ERROR) << "Callback implement is null.";
    return empty_vector;
  }

  session::TrainLoopCallBack *internal_call_back = callback_impl_->GetInternalCallback();
  if (internal_call_back == nullptr) {
    MS_LOG(ERROR) << "Internal callback is null.";
    return empty_vector;
  }

  return (reinterpret_cast<lite::ClassificationTrainAccuracyMonitor *>(internal_call_back))->GetAccuracyPoints();
}
}  // namespace mindspore
