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

#include "tools/benchmark/benchmark_unified_api.h"
#define __STDC_FORMAT_MACROS
#include <cinttypes>
#undef __STDC_FORMAT_MACROS
#include <algorithm>
#include <utility>
#include <functional>
#include "include/context.h"
#include "include/ms_tensor.h"
#include "include/version.h"
#include "schema/model_generated.h"
#include "src/common/common.h"
#include "src/tensor.h"
#ifdef ENABLE_ARM64
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <unistd.h>
#endif

namespace mindspore {
constexpr size_t kDataToStringMaxNum = 40;
constexpr int kPrintDataNum = 20;
constexpr int kFrequencyDefault = 3;
constexpr int kPercentageDivisor = 100;
constexpr int kDumpInputsAndOutputs = 0;
constexpr int kDumpOutputs = 2;

namespace lite {
int BenchmarkUnifiedApi::GenerateInputData() {
  for (auto tensor : ms_inputs_for_api_) {
    MS_ASSERT(tensor != nullptr);
    auto input_data = tensor.MutableData();
    if (input_data == nullptr) {
      MS_LOG(ERROR) << "MallocData for inTensor failed";
      return RET_ERROR;
    }
    int status;
    if (static_cast<int>(tensor.DataType()) == kObjectTypeString) {
      std::cerr << "Unsupported  kObjectTypeString:" << std::endl;
      MS_LOG(ERROR) << "Unsupported  kObjectTypeString:";
      return RET_ERROR;
    } else {
      status = GenerateRandomData(tensor.DataSize(), input_data, static_cast<int>(tensor.DataType()));
    }
    if (status != RET_OK) {
      std::cerr << "GenerateRandomData for inTensor failed: " << status << std::endl;
      MS_LOG(ERROR) << "GenerateRandomData for inTensor failed:" << status;
      return status;
    }
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::ReadInputFile() {
  if (ms_inputs_for_api_.empty()) {
    return RET_OK;
  }

  if (this->flags_->in_data_type_ == kImage) {
    MS_LOG(ERROR) << "Not supported image input";
    return RET_ERROR;
  } else {
    for (size_t i = 0; i < flags_->input_data_list_.size(); i++) {
      auto cur_tensor = ms_inputs_for_api_.at(i);
      MS_ASSERT(cur_tensor != nullptr);
      size_t size;
      char *bin_buf = ReadFile(flags_->input_data_list_[i].c_str(), &size);
      if (bin_buf == nullptr) {
        MS_LOG(ERROR) << "ReadFile return nullptr";
        return RET_ERROR;
      }
      if (static_cast<int>(cur_tensor.DataType()) == kObjectTypeString) {
        std::cerr << "Unsupported  kObjectTypeString:" << std::endl;
        MS_LOG(ERROR) << "Unsupported  kObjectTypeString:";
        return RET_ERROR;
      } else {
        auto tensor_data_size = cur_tensor.DataSize();
        if (size != tensor_data_size) {
          std::cerr << "Input binary file size error, required: " << tensor_data_size << ", in fact: " << size
                    << std::endl;
          MS_LOG(ERROR) << "Input binary file size error, required: " << tensor_data_size << ", in fact: " << size;
          delete[] bin_buf;
          return RET_ERROR;
        }
        auto input_data = cur_tensor.MutableData();
        if (input_data == nullptr) {
          MS_LOG(ERROR) << "input_data is nullptr.";
          return RET_ERROR;
        }
        memcpy(input_data, bin_buf, tensor_data_size);
      }
      delete[] bin_buf;
    }
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::ReadTensorData(std::ifstream &in_file_stream, const std::string &tensor_name,
                                        const std::vector<size_t> &dims) {
  std::string line;
  getline(in_file_stream, line);
  std::stringstream line_stream(line);
  if (this->benchmark_data_.find(tensor_name) != this->benchmark_data_.end()) {
    return RET_OK;
  }
  mindspore::MSTensor tensor = GetMSTensorByNameOrShape(tensor_name, dims);
  if (tensor == nullptr) {
    MS_LOG(ERROR) << "Get tensor failed, tensor name: " << tensor_name;
    return RET_ERROR;
  }
  std::vector<float> data;
  std::vector<std::string> strings_data;
  size_t shape_size = std::accumulate(dims.begin(), dims.end(), 1, std::multiplies<size_t>());
  if (static_cast<int>(tensor.DataType()) == kObjectTypeString) {
    strings_data.push_back(line);
    for (size_t i = 1; i < shape_size; i++) {
      getline(in_file_stream, line);
      strings_data.push_back(line);
    }
  } else {
    for (size_t i = 0; i < shape_size; i++) {
      float tmp_data;
      line_stream >> tmp_data;
      data.push_back(tmp_data);
    }
  }
  auto *check_tensor = new (std::nothrow) CheckTensor(dims, data, strings_data);
  if (check_tensor == nullptr) {
    MS_LOG(ERROR) << "New CheckTensor failed, tensor name: " << tensor_name;
    return RET_ERROR;
  }
  this->benchmark_data_.insert(std::make_pair(tensor_name, check_tensor));
  return RET_OK;
}

void BenchmarkUnifiedApi::InitMSContext(const std::shared_ptr<mindspore::Context> &context) {
  context->SetThreadNum(flags_->num_threads_);
  context->SetEnableParallel(flags_->enable_parallel_);
  context->SetThreadAffinity(flags_->cpu_bind_mode_);
  auto &device_list = context->MutableDeviceInfo();

  std::shared_ptr<CPUDeviceInfo> device_info = std::make_shared<CPUDeviceInfo>();
  device_info->SetEnableFP16(flags_->enable_fp16_);
  device_list.push_back(device_info);

  if (flags_->device_ == "GPU") {
    std::shared_ptr<GPUDeviceInfo> gpu_device_info = std::make_shared<GPUDeviceInfo>();
    gpu_device_info->SetEnableFP16(flags_->enable_fp16_);
    device_list.push_back(gpu_device_info);
  }

  if (flags_->device_ == "NPU") {
    std::shared_ptr<KirinNPUDeviceInfo> npu_device_info = std::make_shared<KirinNPUDeviceInfo>();
    npu_device_info->SetFrequency(kFrequencyDefault);
    device_list.push_back(npu_device_info);
  }
}

int BenchmarkUnifiedApi::CompareOutput() {
  std::cout << "================ Comparing Output data ================" << std::endl;
  float total_bias = 0;
  int total_size = 0;
  for (const auto &calib_tensor : benchmark_data_) {
    std::string node_or_tensor_name = calib_tensor.first;
    mindspore::MSTensor tensor = GetMSTensorByNameOrShape(node_or_tensor_name, calib_tensor.second->shape);
    if (tensor == nullptr) {
      MS_LOG(ERROR) << "Get tensor failed, tensor name: " << node_or_tensor_name;
      return RET_ERROR;
    }
    int ret;
    if (static_cast<int>(tensor.DataType()) == kObjectTypeString) {
      std::cerr << "Unsupported  kObjectTypeString:" << std::endl;
      MS_LOG(ERROR) << "Unsupported  kObjectTypeString:";
      return RET_ERROR;
    } else {
      ret = CompareDataGetTotalBiasAndSize(node_or_tensor_name, &tensor, &total_bias, &total_size);
    }
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Error in CompareData";
      std::cerr << "Error in CompareData" << std::endl;
      std::cout << "=======================================================" << std::endl << std::endl;
      return ret;
    }
  }
  float mean_bias;
  if (total_size != 0) {
    mean_bias = ((total_bias / float_t(total_size)) * kPercentageDivisor);
  } else {
    mean_bias = 0;
  }

  std::cout << "Mean bias of all nodes/tensors: " << mean_bias << "%" << std::endl;
  std::cout << "=======================================================" << std::endl << std::endl;

  if (mean_bias > this->flags_->accuracy_threshold_) {
    MS_LOG(ERROR) << "Mean bias of all nodes/tensors is too big: " << mean_bias << "%";
    std::cerr << "Mean bias of all nodes/tensors is too big: " << mean_bias << "%" << std::endl;
    return RET_ERROR;
  }
  return RET_OK;
}

mindspore::MSTensor BenchmarkUnifiedApi::GetMSTensorByNodeShape(const std::vector<size_t> &node_shape) {
  std::vector<mindspore::MSTensor> match_tensors;
  std::vector<int64_t> shape_vector = ConverterToInt64Vector<size_t>(node_shape);
  auto tensors = ms_model_.GetOutputs();
  for (auto &out_tensor_pair : tensors) {
    if (out_tensor_pair.Shape() == shape_vector) {
      match_tensors.emplace_back(out_tensor_pair);
    }
  }

  return match_tensors.front();
}

mindspore::MSTensor BenchmarkUnifiedApi::GetMSTensorByNameOrShape(const std::string &node_or_tensor_name,
                                                                  const std::vector<size_t> &dims) {
  mindspore::MSTensor tensor;
  auto tensors = ms_model_.GetOutputsByNodeName(node_or_tensor_name);
  if (tensors.empty() || tensors.size() != 1) {
    MS_LOG(INFO) << "Cannot find output node: " << node_or_tensor_name
                 << " or node has more than one output tensor, switch to GetOutputByTensorName";
    tensor = ms_model_.GetOutputByTensorName(node_or_tensor_name);
    if (tensor == nullptr) {
      return GetMSTensorByNodeShape(dims);
    }
  } else {
    tensor = tensors.front();
  }
  return tensor;
}

int BenchmarkUnifiedApi::CompareDataGetTotalBiasAndSize(const std::string &name, mindspore::MSTensor *tensor,
                                                        float *total_bias, int *total_size) {
  float bias = 0;
  auto mutableData = tensor->MutableData();
  if (mutableData == nullptr) {
    MS_LOG(ERROR) << "mutableData is nullptr.";
    return RET_ERROR;
  }
  switch (static_cast<int>(tensor->DataType())) {
    case TypeId::kNumberTypeFloat:
    case TypeId::kNumberTypeFloat32: {
      bias = CompareData<float, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    case TypeId::kNumberTypeInt8: {
      bias = CompareData<int8_t, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    case TypeId::kNumberTypeUInt8: {
      bias = CompareData<uint8_t, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    case TypeId::kNumberTypeInt32: {
      bias = CompareData<int32_t, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    case TypeId::kNumberTypeInt16: {
      bias = CompareData<int16_t, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    case TypeId::kNumberTypeBool: {
      bias = CompareData<bool, int64_t>(name, tensor->Shape(), mutableData);
      break;
    }
    default:
      MS_LOG(ERROR) << "Datatype " << static_cast<int>(tensor->DataType()) << " is not supported.";
      return RET_ERROR;
  }
  if (bias < 0) {
    MS_LOG(ERROR) << "CompareData failed, name: " << name;
    return RET_ERROR;
  }
  *total_bias += bias;
  *total_size += 1;
  return RET_OK;
}

int BenchmarkUnifiedApi::MarkPerformance() {
  MS_LOG(INFO) << "Running warm up loops...";
  std::cout << "Running warm up loops..." << std::endl;
  std::vector<MSTensor> outputs;

  for (int i = 0; i < flags_->warm_up_loop_count_; i++) {
    auto status = ms_model_.Predict(ms_inputs_for_api_, &outputs);
    if (status != kSuccess) {
      MS_LOG(ERROR) << "Inference error ";
      std::cerr << "Inference error " << std::endl;
      return RET_ERROR;
    }
  }

  MS_LOG(INFO) << "Running benchmark loops...";
  std::cout << "Running benchmark loops..." << std::endl;
  uint64_t time_min = 1000000;
  uint64_t time_max = 0;
  uint64_t time_avg = 0;

  for (int i = 0; i < flags_->loop_count_; i++) {
    auto inputs = ms_model_.GetInputs();
    for (auto tensor : inputs) {
      tensor.MutableData();  // prepare data
    }
    auto start = GetTimeUs();
    auto status = ms_model_.Predict(ms_inputs_for_api_, &outputs, ms_before_call_back_, ms_after_call_back_);
    if (status != kSuccess) {
      MS_LOG(ERROR) << "Inference error ";
      std::cerr << "Inference error ";
      return RET_ERROR;
    }

    auto end = GetTimeUs();
    auto time = end - start;
    time_min = std::min(time_min, time);
    time_max = std::max(time_max, time);
    time_avg += time;
  }

  if (flags_->time_profiling_) {
    const std::vector<std::string> per_op_name = {"opName", "avg(ms)", "percent", "calledTimes", "opTotalTime"};
    const std::vector<std::string> per_op_type = {"opType", "avg(ms)", "percent", "calledTimes", "opTotalTime"};
    PrintResult(per_op_name, op_times_by_name_);
    PrintResult(per_op_type, op_times_by_type_);
#ifdef ENABLE_ARM64
  } else if (flags_->perf_profiling_) {
    if (flags_->perf_event_ == "CACHE") {
      const std::vector<std::string> per_op_name = {"opName", "cache ref(k)", "cache ref(%)", "miss(k)", "miss(%)"};
      const std::vector<std::string> per_op_type = {"opType", "cache ref(k)", "cache ref(%)", "miss(k)", "miss(%)"};
      PrintPerfResult(per_op_name, op_perf_by_name_);
      PrintPerfResult(per_op_type, op_perf_by_type_);
    } else if (flags_->perf_event_ == "STALL") {
      const std::vector<std::string> per_op_name = {"opName", "frontend(k)", "frontend(%)", "backendend(k)",
                                                    "backendend(%)"};
      const std::vector<std::string> per_op_type = {"opType", "frontend(k)", "frontend(%)", "backendend(k)",
                                                    "backendend(%)"};
      PrintPerfResult(per_op_name, op_perf_by_name_);
      PrintPerfResult(per_op_type, op_perf_by_type_);
    } else {
      const std::vector<std::string> per_op_name = {"opName", "cycles(k)", "cycles(%)", "ins(k)", "ins(%)"};
      const std::vector<std::string> per_op_type = {"opType", "cycles(k)", "cycles(%)", "ins(k)", "ins(%)"};
      PrintPerfResult(per_op_name, op_perf_by_name_);
      PrintPerfResult(per_op_type, op_perf_by_type_);
    }
#endif
  }

  if (flags_->loop_count_ > 0) {
    time_avg /= flags_->loop_count_;
    MS_LOG(INFO) << "Model = " << flags_->model_file_.substr(flags_->model_file_.find_last_of(DELIM_SLASH) + 1).c_str()
                 << ", NumThreads = " << flags_->num_threads_ << ", MinRunTime = " << time_min / kFloatMSEC
                 << ", MaxRuntime = " << time_max / kFloatMSEC << ", AvgRunTime = " << time_avg / kFloatMSEC;
    printf("Model = %s, NumThreads = %d, MinRunTime = %f ms, MaxRuntime = %f ms, AvgRunTime = %f ms\n",
           flags_->model_file_.substr(flags_->model_file_.find_last_of(DELIM_SLASH) + 1).c_str(), flags_->num_threads_,
           time_min / kFloatMSEC, time_max / kFloatMSEC, time_avg / kFloatMSEC);
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::MarkAccuracy() {
  MS_LOG(INFO) << "MarkAccuracy";
  std::cout << "MarkAccuracy" << std::endl;

  auto status = PrintInputData();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "PrintInputData error " << status;
    std::cerr << "PrintInputData error " << status << std::endl;
    return status;
  }
  std::vector<MSTensor> outputs;
  auto ret = ms_model_.Predict(ms_inputs_for_api_, &outputs, ms_before_call_back_, ms_after_call_back_);
  if (ret != kSuccess) {
    MS_LOG(ERROR) << "Inference error ";
    std::cerr << "Inference error " << std::endl;
    return RET_ERROR;
  }
  status = ReadCalibData();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Read calib data error " << status;
    std::cerr << "Read calib data error " << status << std::endl;
    return status;
  }
  status = CompareOutput();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Compare output error " << status;
    std::cerr << "Compare output error " << status << std::endl;
    return status;
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::PrintInputData() {
  for (size_t i = 0; i < ms_inputs_for_api_.size(); i++) {
    auto input = ms_inputs_for_api_[i];
    MS_ASSERT(input != nullptr);
    auto tensor_data_type = static_cast<int>(input.DataType());

    std::cout << "InData" << i << ": ";
    if (tensor_data_type == TypeId::kObjectTypeString) {
      std::cerr << "Unsupported  kObjectTypeString:" << std::endl;
      MS_LOG(ERROR) << "Unsupported  kObjectTypeString:";
      return RET_ERROR;
    }
    size_t print_num = std::min(static_cast<int>(input.ElementNum()), kPrintDataNum);
    const void *in_data = input.MutableData();
    if (in_data == nullptr) {
      MS_LOG(ERROR) << "in_data is nullptr.";
      return RET_ERROR;
    }

    for (size_t j = 0; j < print_num; j++) {
      if (tensor_data_type == TypeId::kNumberTypeFloat32 || tensor_data_type == TypeId::kNumberTypeFloat) {
        std::cout << static_cast<const float *>(in_data)[j] << " ";
      } else if (tensor_data_type == TypeId::kNumberTypeInt8) {
        std::cout << static_cast<const int8_t *>(in_data)[j] << " ";
      } else if (tensor_data_type == TypeId::kNumberTypeUInt8) {
        std::cout << static_cast<const uint8_t *>(in_data)[j] << " ";
      } else if (tensor_data_type == TypeId::kNumberTypeInt32) {
        std::cout << static_cast<const int32_t *>(in_data)[j] << " ";
      } else if (tensor_data_type == TypeId::kNumberTypeInt64) {
        std::cout << static_cast<const int64_t *>(in_data)[j] << " ";
      } else if (tensor_data_type == TypeId::kNumberTypeBool) {
        std::cout << static_cast<const bool *>(in_data)[j] << " ";
      } else {
        MS_LOG(ERROR) << "Datatype: " << tensor_data_type << " is not supported.";
        return RET_ERROR;
      }
    }
    std::cout << std::endl;
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::RunBenchmark() {
  auto start_prepare_time = GetTimeUs();
  // Load graph
  std::string model_name = flags_->model_file_.substr(flags_->model_file_.find_last_of(DELIM_SLASH) + 1);

  MS_LOG(INFO) << "start reading model file";
  std::cout << "start reading model file" << std::endl;
  size_t size = 0;
  char *graph_buf = ReadFile(flags_->model_file_.c_str(), &size);
  if (graph_buf == nullptr) {
    MS_LOG(ERROR) << "Read model file failed while running " << model_name.c_str();
    std::cerr << "Read model file failed while running " << model_name.c_str() << std::endl;
    return RET_ERROR;
  }

  auto context = std::make_shared<mindspore::Context>();
  if (context == nullptr) {
    MS_LOG(ERROR) << "New context failed while running " << model_name.c_str();
    std::cerr << "New context failed while running " << model_name.c_str() << std::endl;
    return RET_ERROR;
  }

  (void)InitMSContext(context);
  auto ret = ms_model_.Build(graph_buf, size, kMindIR, context);
  if (ret != kSuccess) {
    MS_LOG(ERROR) << "ms_model_.Build failed while running ", model_name.c_str();
    std::cout << "ms_model_.Build failed while running ", model_name.c_str();
    return RET_ERROR;
  }

  if (!flags_->resize_dims_.empty()) {
    std::vector<std::vector<int64_t>> resize_dims;
    (void)std::transform(flags_->resize_dims_.begin(), flags_->resize_dims_.end(), std::back_inserter(resize_dims),
                         [&](auto &shapes) { return this->ConverterToInt64Vector<int>(shapes); });

    ret = ms_model_.Resize(ms_model_.GetInputs(), resize_dims);
    if (ret != kSuccess) {
      MS_LOG(ERROR) << "Input tensor resize failed.";
      std::cout << "Input tensor resize failed.";
      return RET_ERROR;
    }
  }

  ms_inputs_for_api_ = ms_model_.GetInputs();
  auto end_prepare_time = GetTimeUs();
  MS_LOG(INFO) << "PrepareTime = " << ((end_prepare_time - start_prepare_time) / kFloatMSEC) << " ms";
  std::cout << "PrepareTime = " << ((end_prepare_time - start_prepare_time) / kFloatMSEC) << " ms" << std::endl;

  // Load input
  MS_LOG(INFO) << "start generate input data";
  auto status = LoadInput();
  if (status != 0) {
    MS_LOG(ERROR) << "Generate input data error";
    return status;
  }
  if (!flags_->benchmark_data_file_.empty()) {
    status = MarkAccuracy();
    for (auto &data : benchmark_data_) {
      data.second->shape.clear();
      data.second->data.clear();
      delete data.second;
      data.second = nullptr;
    }
    benchmark_data_.clear();
    if (status != 0) {
      MS_LOG(ERROR) << "Run MarkAccuracy error: " << status;
      std::cout << "Run MarkAccuracy error: " << status << std::endl;
      return status;
    }
  } else {
    status = MarkPerformance();
    if (status != 0) {
      MS_LOG(ERROR) << "Run MarkPerformance error: " << status;
      std::cout << "Run MarkPerformance error: " << status << std::endl;
      return status;
    }
  }
  if (flags_->dump_tensor_data_) {
    std::cout << "Dumped file is saved to : " + dump_file_output_dir_ << std::endl;
  }
  return RET_OK;
}

int BenchmarkUnifiedApi::InitTimeProfilingCallbackParameter() {
  // before callback
  ms_before_call_back_ = [&](const std::vector<mindspore::MSTensor> &before_inputs,
                             const std::vector<mindspore::MSTensor> &before_outputs,
                             const MSCallBackParam &call_param) {
    if (before_inputs.empty()) {
      MS_LOG(INFO) << "The num of beforeInputs is empty";
    }
    if (before_outputs.empty()) {
      MS_LOG(INFO) << "The num of beforeOutputs is empty";
    }
    if (op_times_by_type_.find(call_param.node_type_) == op_times_by_type_.end()) {
      op_times_by_type_.insert(std::make_pair(call_param.node_type_, std::make_pair(0, 0.0f)));
    }
    if (op_times_by_name_.find(call_param.node_name_) == op_times_by_name_.end()) {
      op_times_by_name_.insert(std::make_pair(call_param.node_name_, std::make_pair(0, 0.0f)));
    }

    op_call_times_total_++;
    op_begin_ = GetTimeUs();
    return true;
  };

  // after callback
  ms_after_call_back_ = [&](const std::vector<mindspore::MSTensor> &after_inputs,
                            const std::vector<mindspore::MSTensor> &after_outputs, const MSCallBackParam &call_param) {
    uint64_t opEnd = GetTimeUs();

    if (after_inputs.empty()) {
      MS_LOG(INFO) << "The num of after inputs is empty";
    }
    if (after_outputs.empty()) {
      MS_LOG(INFO) << "The num of after outputs is empty";
    }

    float cost = static_cast<float>(opEnd - op_begin_) / kFloatMSEC;
    if (flags_->device_ == "GPU") {
      auto gpu_param = reinterpret_cast<const GPUCallBackParam &>(call_param);
      cost = static_cast<float>(gpu_param.execute_time);
    }
    op_cost_total_ += cost;
    op_times_by_type_[call_param.node_type_].first++;
    op_times_by_type_[call_param.node_type_].second += cost;
    op_times_by_name_[call_param.node_name_].first++;
    op_times_by_name_[call_param.node_name_].second += cost;
    return true;
  };
  return RET_OK;
}

int BenchmarkUnifiedApi::InitPerfProfilingCallbackParameter() {
#ifndef ENABLE_ARM64
  MS_LOG(ERROR) << "Only support perf_profiling on arm64.";
  return RET_ERROR;
#else
  struct perf_event_attr pe, pe2;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  memset(&pe2, 0, sizeof(struct perf_event_attr));
  pe.type = PERF_TYPE_HARDWARE;
  pe2.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(struct perf_event_attr);
  pe2.size = sizeof(struct perf_event_attr);
  pe.disabled = 1;
  pe2.disabled = 1;
  pe.exclude_kernel = 1;   // don't count kernel
  pe2.exclude_kernel = 1;  // don't count kernel
  pe.exclude_hv = 1;       // don't count hypervisor
  pe2.exclude_hv = 1;      // don't count hypervisor
  pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
  pe2.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
  if (flags_->perf_event_ == "CACHE") {
    pe.config = PERF_COUNT_HW_CACHE_REFERENCES;
    pe2.config = PERF_COUNT_HW_CACHE_MISSES;
  } else if (flags_->perf_event_ == "STALL") {
    pe.config = PERF_COUNT_HW_STALLED_CYCLES_FRONTEND;
    pe2.config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND;
  } else {
    pe.config = PERF_COUNT_HW_CPU_CYCLES;
    pe2.config = PERF_COUNT_HW_INSTRUCTIONS;
  }
  perf_fd = syscall(__NR_perf_event_open, pe, 0, -1, -1, 0);
  if (perf_fd == -1) {
    MS_LOG(ERROR) << "Failed to open perf event " << pe.config;
    return RET_ERROR;
  }
  perf_fd2 = syscall(__NR_perf_event_open, pe2, 0, -1, perf_fd, 0);
  if (perf_fd2 == -1) {
    MS_LOG(ERROR) << "Failed to open perf event " << pe2.config;
    return RET_ERROR;
  }
  struct PerfCount zero;
  zero.value[0] = 0;
  zero.value[1] = 0;
  // before callback
  ms_before_call_back_ = [&](const std::vector<mindspore::MSTensor> &before_inputs,
                             const std::vector<mindspore::MSTensor> &before_outputs,
                             const MSCallBackParam &call_param) {
    if (before_inputs.empty()) {
      MS_LOG(INFO) << "The num of beforeInputs is empty";
    }
    if (before_outputs.empty()) {
      MS_LOG(INFO) << "The num of beforeOutputs is empty";
    }
    if (op_perf_by_type_.find(call_param.node_type_) == op_perf_by_type_.end()) {
      op_perf_by_type_.insert(std::make_pair(call_param.node_type_, std::make_pair(0, zero)));
    }
    if (op_perf_by_name_.find(call_param.node_name_) == op_perf_by_name_.end()) {
      op_perf_by_name_.insert(std::make_pair(call_param.node_name_, std::make_pair(0, zero)));
    }

    op_call_times_total_++;
    ioctl(perf_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    return true;
  };

  // after callback
  ms_after_call_back_ = [&](const std::vector<mindspore::MSTensor> &after_inputs,
                            const std::vector<mindspore::MSTensor> &after_outputs, const MSCallBackParam &call_param) {
    struct PerfResult res;
    ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    if (read(perf_fd, &res, sizeof(struct PerfResult)) == -1) {
      MS_LOG(ERROR) << "Failed to read perf_fd";
      return false;
    }

    if (after_inputs.empty()) {
      MS_LOG(INFO) << "The num of after inputs is empty";
    }
    if (after_outputs.empty()) {
      MS_LOG(INFO) << "The num of after outputs is empty";
    }
    float cost1 = static_cast<float>(res.values[0].value);
    float cost2 = static_cast<float>(res.values[1].value);
    op_cost_total_ += cost1;
    op_cost2_total_ += cost2;
    op_perf_by_type_[call_param.node_type_].first++;
    op_perf_by_type_[call_param.node_type_].second.value[0] += cost1;
    op_perf_by_type_[call_param.node_type_].second.value[1] += cost2;
    op_perf_by_name_[call_param.node_name_].first++;
    op_perf_by_name_[call_param.node_name_].second.value[0] += cost1;
    op_perf_by_name_[call_param.node_name_].second.value[1] += cost2;
    return true;
  };
#endif
  return RET_OK;
}

namespace {
template <typename T>
std::string DataToString(void *data, size_t data_number) {
  if (data == nullptr) {
    return "Data of tensor is nullptr";
  }
  std::ostringstream oss;
  auto casted_data = static_cast<T *>(data);
  for (size_t i = 0; i < kDataToStringMaxNum && i < data_number; i++) {
    oss << " " << casted_data[i];
  }
  return oss.str();
}

std::string DumpMSTensor(mindspore::MSTensor *tensor) {
  if (tensor == nullptr) {
    return "Tensor is nullptr";
  }
  std::ostringstream oss;
  oss << " DataType: " << static_cast<int>(tensor->DataType());
  oss << " Shape:";
  for (auto &dim : tensor->Shape()) {
    oss << " " << dim;
  }
  oss << std::endl << " Data:";
  switch (static_cast<int>(tensor->DataType())) {
    case kNumberTypeFloat32: {
      oss << DataToString<float>(tensor->MutableData(), tensor->ElementNum());
    } break;
    case kNumberTypeFloat16: {
      oss << DataToString<int16_t>(tensor->MutableData(), tensor->ElementNum());
    } break;
    case kNumberTypeInt32: {
      oss << DataToString<int32_t>(tensor->MutableData(), tensor->ElementNum());
    } break;
    case kNumberTypeInt16: {
      oss << DataToString<int16_t>(tensor->MutableData(), tensor->ElementNum());
    } break;
    case kNumberTypeInt8: {
      oss << DataToString<int8_t>(tensor->MutableData(), tensor->ElementNum());
    } break;
    default:
      oss << "Unsupported data type to print";
      break;
  }
  return oss.str();
}

std::string GenerateOutputFileName(mindspore::MSTensor *tensor, const std::string &op_name,
                                   const std::string &file_type, const size_t &idx) {
  std::string file_name = op_name;
  auto pos = file_name.find_first_of('/');
  while (pos != std::string::npos) {
    file_name.replace(pos, 1, ".");
    pos = file_name.find_first_of('/');
  }
  file_name += "_" + file_type + "_" + std::to_string(idx) + "_shape_";
  for (const auto &dim : tensor->Shape()) {
    file_name += std::to_string(dim) + "_";
  }
  if (TYPE_ID_MAP.find(static_cast<int>(tensor->DataType())) != TYPE_ID_MAP.end()) {
    file_name += TYPE_ID_MAP.at(static_cast<int>(tensor->DataType()));
  }

  file_name += +".bin";
  return file_name;
}
}  // namespace

int BenchmarkUnifiedApi::InitPrintTensorDataCallbackParameter() {
  // before callback
  ms_before_call_back_ = [&](const std::vector<mindspore::MSTensor> &before_inputs,
                             const std::vector<mindspore::MSTensor> &before_outputs,
                             const MSCallBackParam &call_param) { return true; };

  // after callback
  ms_after_call_back_ = [&](const std::vector<mindspore::MSTensor> &after_inputs,
                            const std::vector<mindspore::MSTensor> &after_outputs, const MSCallBackParam &call_param) {
    std::cout << "================================================================" << std::endl;
    std::cout << call_param.node_name_ << " inputs : " << std::endl;
    for (auto ms_tensor : after_inputs) {
      std::cout << DumpMSTensor(&ms_tensor) << std::endl;
    }
    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << call_param.node_name_ << " outputs : " << std::endl;
    for (auto ms_tensor : after_outputs) {
      std::cout << DumpMSTensor(&ms_tensor) << std::endl;
    }
    std::cout << "================================================================" << std::endl;
    return true;
  };
  return RET_OK;
}
int BenchmarkUnifiedApi::InitDumpTensorDataCallbackParameter() {
  // before callback
  ms_before_call_back_ = [&](const std::vector<mindspore::MSTensor> &before_inputs,
                             const std::vector<mindspore::MSTensor> &before_outputs,
                             const MSCallBackParam &call_param) {
    auto dump_mode = dump_cfg_json_[dump::kSettings][dump::kMode].get<int>();
    auto input_output_mode = dump_cfg_json_[dump::kSettings][dump::kInputOutput].get<int>();
    auto kernels = dump_cfg_json_[dump::kSettings][dump::kKernels].get<std::vector<std::string>>();
    if (dump_mode == 0 || std::find(kernels.begin(), kernels.end(), call_param.node_name_) != kernels.end()) {
      if (input_output_mode == 0 || input_output_mode == 1) {
        for (size_t i = 0; i < before_inputs.size(); i++) {
          auto ms_tensor = before_inputs.at(i);
          auto file_name = GenerateOutputFileName(&ms_tensor, call_param.node_name_, "input", i);
          auto abs_file_path = dump_file_output_dir_ + "/" + file_name;
          if (WriteToBin(abs_file_path, ms_tensor.MutableData(), ms_tensor.DataSize()) != RET_OK) {  // save to file
            MS_LOG(ERROR) << "write tensor data to file failed.";
            return false;
          }
        }
      }
    }
    return true;
  };

  // after callback
  ms_after_call_back_ = [&](const std::vector<mindspore::MSTensor> &after_inputs,
                            const std::vector<mindspore::MSTensor> &after_outputs, const MSCallBackParam &call_param) {
    auto dump_mode = dump_cfg_json_[dump::kSettings][dump::kMode].get<int>();
    auto input_output_mode = dump_cfg_json_[dump::kSettings][dump::kInputOutput].get<int>();
    auto kernels = dump_cfg_json_[dump::kSettings][dump::kKernels].get<std::vector<std::string>>();
    if (dump_mode == kDumpInputsAndOutputs ||
        std::find(kernels.begin(), kernels.end(), call_param.node_name_) != kernels.end()) {
      if (input_output_mode == kDumpInputsAndOutputs || input_output_mode == kDumpOutputs) {
        for (size_t i = 0; i < after_outputs.size(); i++) {
          auto ms_tensor = after_outputs.at(i);
          auto file_name = GenerateOutputFileName(&ms_tensor, call_param.node_name_, "output", i);
          auto abs_file_path = dump_file_output_dir_ + "/" + file_name;
          if (WriteToBin(abs_file_path, ms_tensor.MutableData(), ms_tensor.DataSize()) != RET_OK) {  // save to file
            MS_LOG(ERROR) << "write tensor data to file failed.";
            return false;
          }
        }
      }
    }
    return true;
  };
  return RET_OK;
}

BenchmarkUnifiedApi::~BenchmarkUnifiedApi() {}
}  // namespace lite
}  // namespace mindspore
