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

#include "tools/benchmark/benchmark_base.h"
#define __STDC_FORMAT_MACROS
#include <cinttypes>
#undef __STDC_FORMAT_MACROS
#include <algorithm>
#include <utility>
#include <regex>
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
namespace lite {
constexpr int kThreadNumMin = 1;
constexpr int kParallelThreadNumMin = 1;
constexpr int kColumnLen = 4;
constexpr int kPrintColNum = 5;
constexpr int kPrintRowLenMax = 100;

constexpr float kInputDataFloatMin = 0.1f;
constexpr float kInputDataFloatMax = 1.0f;
constexpr double kInputDataDoubleMin = 0.1;
constexpr double kInputDataDoubleMax = 1.0;
constexpr int64_t kInputDataInt64Min = 0;
constexpr int64_t kInputDataInt64Max = 1;
constexpr int32_t kInputDataInt32Min = 0;
constexpr int32_t kInputDataInt32Max = 1;
constexpr int16_t kInputDataInt16Min = 0;
constexpr int16_t kInputDataInt16Max = 1;
constexpr int16_t kInputDataInt8Min = -127;
constexpr int16_t kInputDataInt8Max = 127;
constexpr int16_t kInputDataUint8Min = 0;
constexpr int16_t kInputDataUint8Max = 254;

const std::unordered_map<int, std::string> TYPE_ID_MAP{
  {kNumberTypeFloat16, "Float16"}, {kNumberTypeFloat, "Float32"},    {kNumberTypeFloat32, "Float32"},
  {kNumberTypeInt8, "Int8"},       {kNumberTypeInt16, "Int16"},      {kNumberTypeInt, "Int32"},
  {kNumberTypeInt32, "Int32"},     {kNumberTypeUInt8, "UInt8"},      {kNumberTypeUInt16, "UInt16"},
  {kNumberTypeUInt, "UInt32"},     {kNumberTypeUInt32, "UInt32"},    {kObjectTypeString, "String"},
  {kNumberTypeBool, "Bool"},       {kObjectTypeTensorType, "Tensor"}};
const std::unordered_map<schema::Format, std::string> TENSOR_FORMAT_MAP{
  {schema::Format_NCHW, "NCHW"}, {schema::Format_NHWC, "NHWC"},     {schema::Format_NHWC4, "NHWC4"},
  {schema::Format_HWKC, "HWKC"}, {schema::Format_HWCK, "HWCK"},     {schema::Format_KCHW, "KCHW"},
  {schema::Format_CKHW, "CKHW"}, {schema::Format_KHWC, "KHWC"},     {schema::Format_CHWK, "CHWK"},
  {schema::Format_HW, "HW"},     {schema::Format_HW4, "HW4"},       {schema::Format_NC, "NC"},
  {schema::Format_NC4, "NC4"},   {schema::Format_NC4HW4, "NC4HW4"}, {schema::Format_NCDHW, "NCDHW"}};

int BenchmarkBase::GenerateRandomData(size_t size, void *data, int data_type) {
  MS_ASSERT(data != nullptr);
  switch (data_type) {
    case kNumberTypeFloat32:
    case kNumberTypeFloat:
      FillInputData<float>(size, data, std::uniform_real_distribution<float>(kInputDataFloatMin, kInputDataFloatMax));
      break;
    case kNumberTypeFloat64:
      FillInputData<double>(size, data,
                            std::uniform_real_distribution<double>(kInputDataDoubleMin, kInputDataDoubleMax));
      break;
    case kNumberTypeInt64:
      FillInputData<int64_t>(size, data,
                             std::uniform_int_distribution<int64_t>(kInputDataInt64Min, kInputDataInt64Max));
      break;
    case kNumberTypeInt:
    case kNumberTypeInt32:
      FillInputData<int32_t>(size, data,
                             std::uniform_int_distribution<int32_t>(kInputDataInt32Min, kInputDataInt32Max));
      break;
    case kNumberTypeInt16:
      FillInputData<int16_t>(size, data,
                             std::uniform_int_distribution<int16_t>(kInputDataInt16Min, kInputDataInt16Max));
      break;
    case kNumberTypeInt8:
      FillInputData<int8_t>(size, data, std::uniform_int_distribution<int16_t>(kInputDataInt8Min, kInputDataInt8Max));
      break;
    case kNumberTypeUInt8:
      FillInputData<uint8_t>(size, data,
                             std::uniform_int_distribution<uint16_t>(kInputDataUint8Min, kInputDataUint8Max));
      break;
    default:
      char *casted_data = static_cast<char *>(data);
      for (size_t i = 0; i < size; i++) {
        casted_data[i] = static_cast<char>(i);
      }
  }
  return RET_OK;
}

int BenchmarkBase::LoadInput() {
  if (flags_->in_data_file_.empty()) {
    auto status = GenerateInputData();
    if (status != 0) {
      std::cerr << "Generate input data error " << status << std::endl;
      MS_LOG(ERROR) << "Generate input data error " << status;
      return status;
    }
  } else {
    auto status = ReadInputFile();
    if (status != 0) {
      std::cerr << "ReadInputFile error, " << status << std::endl;
      MS_LOG(ERROR) << "ReadInputFile error, " << status;
      return status;
    }
  }
  return RET_OK;
}

// calibData is FP32
int BenchmarkBase::ReadCalibData() {
  const char *calib_data_path = flags_->benchmark_data_file_.c_str();
  // read calib data
  std::ifstream in_file(calib_data_path);
  if (!in_file.good()) {
    std::cerr << "file: " << calib_data_path << " is not exist" << std::endl;
    MS_LOG(ERROR) << "file: " << calib_data_path << " is not exist";
    return RET_ERROR;
  }

  if (!in_file.is_open()) {
    std::cerr << "file: " << calib_data_path << " open failed" << std::endl;
    MS_LOG(ERROR) << "file: " << calib_data_path << " open failed";
    in_file.close();
    return RET_ERROR;
  }
  MS_LOG(INFO) << "Start reading calibData file";
  std::string line;
  std::string tensor_name;

  while (!in_file.eof()) {
    getline(in_file, line);
    std::stringstream string_line1(line);
    size_t dim = 0;
    string_line1 >> tensor_name >> dim;
    std::vector<size_t> dims;
    for (size_t i = 0; i < dim; i++) {
      size_t tmp_dim;
      string_line1 >> tmp_dim;
      dims.push_back(tmp_dim);
    }
    auto ret = ReadTensorData(in_file, tensor_name, dims);
    if (ret != RET_OK) {
      MS_LOG(ERROR) << "Read tensor data failed, tensor name: " << tensor_name;
      return RET_ERROR;
    }
  }
  in_file.close();
  MS_LOG(INFO) << "Finish reading calibData file";
  return RET_OK;
}

int BenchmarkBase::CompareStringData(const std::string &name, tensor::MSTensor *tensor) {
  auto iter = this->benchmark_data_.find(name);
  if (iter != this->benchmark_data_.end()) {
    std::vector<std::string> calib_strings = iter->second->strings_data;
    std::vector<std::string> output_strings = MSTensorToStrings(tensor);
    size_t compare_num = std::min(calib_strings.size(), output_strings.size());
    size_t print_num = std::min(compare_num, static_cast<size_t>(kNumPrintMin));

    std::cout << "Data of node " << name << " : " << std::endl;
    for (size_t i = 0; i < compare_num; i++) {
      if (i < print_num) {
        std::cout << "  " << output_strings[i] << std::endl;
      }
      if (calib_strings[i] != output_strings[i]) {
        MS_LOG(ERROR) << "Compare failed, index: " << i;
        return RET_ERROR;
      }
    }
  }
  return RET_OK;
}

void BenchmarkFlags::InitInputDataList() {
  if (in_data_file_.empty()) {
    input_data_list_ = {};
    return;
  }
  std::regex re{"[\\s,]+"};
  input_data_list_ = std::vector<std::string>{
    std::sregex_token_iterator(in_data_file_.begin(), in_data_file_.end(), re, -1), std::sregex_token_iterator()};
}

void BenchmarkFlags::InitResizeDimsList() {
  std::string content = this->resize_dims_in_;
  std::vector<int> shape;
  auto shape_strs = StrSplit(content, std::string(DELIM_COLON));
  for (const auto &shape_str : shape_strs) {
    shape.clear();
    auto dim_strs = StrSplit(shape_str, std::string(DELIM_COMMA));
    std::cout << "Resize Dims: ";
    for (const auto &dim_str : dim_strs) {
      std::cout << dim_str << " ";
      shape.emplace_back(static_cast<int>(std::stoi(dim_str)));
    }
    std::cout << std::endl;
    this->resize_dims_.emplace_back(shape);
  }
}

int BenchmarkBase::CheckThreadNumValid() {
  if (this->flags_->num_threads_ < kThreadNumMin) {
    MS_LOG(ERROR) << "numThreads:" << this->flags_->num_threads_ << " must be greater than 0";
    std::cerr << "numThreads:" << this->flags_->num_threads_ << " must be greater than 0" << std::endl;
    return RET_ERROR;
  }

  if (flags_->enable_parallel_) {
    if (flags_->num_threads_ < kParallelThreadNumMin) {
      MS_LOG(ERROR) << "enable parallel need more than 1 thread.";
      std::cerr << "enable parallel need more than 1 thread." << std::endl;
      return RET_ERROR;
    }
  }
  return RET_OK;
}

int BenchmarkBase::InitDumpConfigFromJson(char *path) {
  auto real_path = RealPath(path);
  std::ifstream ifs(real_path);
  if (!ifs.good()) {
    MS_LOG(ERROR) << "file: " << real_path << " is not exist";
    return RET_ERROR;
  }
  if (!ifs.is_open()) {
    MS_LOG(ERROR) << "file: " << real_path << " open failed";
    return RET_ERROR;
  }

  try {
    dump_cfg_json_ = nlohmann::json::parse(ifs);
  } catch (const nlohmann::json::parse_error &error) {
    MS_LOG(ERROR) << "parse json file failed, please check your file.";
    return RET_ERROR;
  }
  if (dump_cfg_json_[dump::kSettings] == nullptr) {
    MS_LOG(ERROR) << "\"common_dump_settings\" is required.";
    return RET_ERROR;
  }
  if (dump_cfg_json_[dump::kSettings][dump::kMode] == nullptr) {
    MS_LOG(ERROR) << "\"dump_mode\" is required.";
    return RET_ERROR;
  }
  if (dump_cfg_json_[dump::kSettings][dump::kPath] == nullptr) {
    MS_LOG(ERROR) << "\"path\" is required.";
    return RET_ERROR;
  }
  if (dump_cfg_json_[dump::kSettings][dump::kNetName] == nullptr) {
    dump_cfg_json_[dump::kSettings][dump::kNetName] = "Default";
  }
  if (dump_cfg_json_[dump::kSettings][dump::kInputOutput] == nullptr) {
    dump_cfg_json_[dump::kSettings][dump::kInputOutput] = 0;
  }
  if (dump_cfg_json_[dump::kSettings][dump::kKernels] != nullptr &&
      !dump_cfg_json_[dump::kSettings][dump::kKernels].empty()) {
    if (dump_cfg_json_[dump::kSettings][dump::kMode] == 0) {
      MS_LOG(ERROR) << R"("dump_mode" should be 1 when "kernels" isn't empty.)";
      return RET_ERROR;
    }
  }

  auto abs_path = dump_cfg_json_[dump::kSettings][dump::kPath].get<std::string>();
  auto net_name = dump_cfg_json_[dump::kSettings][dump::kNetName].get<std::string>();
  if (abs_path.back() == '\\' || abs_path.back() == '/') {
    dump_file_output_dir_ = abs_path + net_name;
  } else {
#ifdef _WIN32
    dump_file_output_dir_ = abs_path + "\\" + net_name;
#else
    dump_file_output_dir_ = abs_path + "/" + net_name;
#endif
  }

  auto status = CreateOutputDir(&dump_file_output_dir_);
  if (status != RET_OK) {
    MS_LOG(ERROR) << "create data output directory failed.";
    return RET_ERROR;
  }

  return RET_OK;
}

int BenchmarkBase::InitCallbackParameter() {
  int ret = RET_OK;
  if (flags_->time_profiling_) {
    ret = InitTimeProfilingCallbackParameter();
  } else if (flags_->perf_profiling_) {
    ret = InitPerfProfilingCallbackParameter();
  } else if (flags_->print_tensor_data_) {
    ret = InitPrintTensorDataCallbackParameter();
  } else if (flags_->dump_tensor_data_) {
    ret = InitDumpTensorDataCallbackParameter();
  }
  return ret;
}

int BenchmarkBase::Init() {
  if (this->flags_ == nullptr) {
    return 1;
  }
  MS_LOG(INFO) << "ModelPath = " << this->flags_->model_file_;
  MS_LOG(INFO) << "InDataPath = " << this->flags_->in_data_file_;
  MS_LOG(INFO) << "InDataType = " << this->flags_->in_data_type_in_;
  MS_LOG(INFO) << "LoopCount = " << this->flags_->loop_count_;
  MS_LOG(INFO) << "DeviceType = " << this->flags_->device_;
  MS_LOG(INFO) << "AccuracyThreshold = " << this->flags_->accuracy_threshold_;
  MS_LOG(INFO) << "WarmUpLoopCount = " << this->flags_->warm_up_loop_count_;
  MS_LOG(INFO) << "NumThreads = " << this->flags_->num_threads_;
  MS_LOG(INFO) << "Fp16Priority = " << this->flags_->enable_fp16_;
  MS_LOG(INFO) << "EnableParallel = " << this->flags_->enable_parallel_;
  MS_LOG(INFO) << "calibDataPath = " << this->flags_->benchmark_data_file_;
  std::cout << "ModelPath = " << this->flags_->model_file_ << std::endl;
  std::cout << "InDataPath = " << this->flags_->in_data_file_ << std::endl;
  std::cout << "InDataType = " << this->flags_->in_data_type_in_ << std::endl;
  std::cout << "LoopCount = " << this->flags_->loop_count_ << std::endl;
  std::cout << "DeviceType = " << this->flags_->device_ << std::endl;
  std::cout << "AccuracyThreshold = " << this->flags_->accuracy_threshold_ << std::endl;
  std::cout << "WarmUpLoopCount = " << this->flags_->warm_up_loop_count_ << std::endl;
  std::cout << "NumThreads = " << this->flags_->num_threads_ << std::endl;
  std::cout << "Fp16Priority = " << this->flags_->enable_fp16_ << std::endl;
  std::cout << "EnableParallel = " << this->flags_->enable_parallel_ << std::endl;
  std::cout << "calibDataPath = " << this->flags_->benchmark_data_file_ << std::endl;
  if (this->flags_->loop_count_ < 1) {
    MS_LOG(ERROR) << "LoopCount:" << this->flags_->loop_count_ << " must be greater than 0";
    std::cerr << "LoopCount:" << this->flags_->loop_count_ << " must be greater than 0" << std::endl;
    return RET_ERROR;
  }

  auto thread_ret = CheckThreadNumValid();
  if (thread_ret != RET_OK) {
    MS_LOG(ERROR) << "Invalid numThreads.";
    std::cerr << "Invalid numThreads." << std::endl;
    return RET_ERROR;
  }
  static std::vector<std::string> CPU_BIND_MODE_MAP = {"NO_BIND", "HIGHER_CPU", "MID_CPU"};
  if (this->flags_->cpu_bind_mode_ >= 1) {
    MS_LOG(INFO) << "cpuBindMode = " << CPU_BIND_MODE_MAP[this->flags_->cpu_bind_mode_];
    std::cout << "cpuBindMode = " << CPU_BIND_MODE_MAP[this->flags_->cpu_bind_mode_] << std::endl;
  } else {
    MS_LOG(INFO) << "cpuBindMode = NO_BIND";
    std::cout << "cpuBindMode = NO_BIND" << std::endl;
  }

  this->flags_->in_data_type_ = this->flags_->in_data_type_in_ == "img" ? kImage : kBinary;

  if (!flags_->benchmark_data_type_.empty()) {
    if (data_type_map_.find(flags_->benchmark_data_type_) == data_type_map_.end()) {
      MS_LOG(ERROR) << "CalibDataType not supported: " << flags_->benchmark_data_type_.c_str();
      return RET_ERROR;
    }
    msCalibDataType = data_type_map_.at(flags_->benchmark_data_type_);
    MS_LOG(INFO) << "CalibDataType = " << flags_->benchmark_data_type_.c_str();
    std::cout << "CalibDataType = " << flags_->benchmark_data_type_.c_str() << std::endl;
  }

  if (flags_->model_file_.empty()) {
    MS_LOG(ERROR) << "modelPath is required";
    std::cerr << "modelPath is required" << std::endl;
    return 1;
  }
  flags_->InitInputDataList();
  flags_->InitResizeDimsList();
  if (!flags_->resize_dims_.empty() && !flags_->input_data_list_.empty() &&
      flags_->resize_dims_.size() != flags_->input_data_list_.size()) {
    MS_LOG(ERROR) << "Size of input resizeDims should be equal to size of input inDataPath";
    std::cerr << "Size of input resizeDims should be equal to size of input inDataPath" << std::endl;
    return RET_ERROR;
  }

  if (flags_->device_ != "CPU" && flags_->device_ != "GPU" && flags_->device_ != "NPU") {
    MS_LOG(ERROR) << "Device type:" << flags_->device_ << " is not supported.";
    std::cerr << "Device type:" << flags_->device_ << " is not supported." << std::endl;
    return RET_ERROR;
  }

  if (flags_->time_profiling_ && flags_->perf_profiling_) {
    MS_LOG(INFO) << "time_profiling is enabled, will not run perf_profiling.";
  }

  // get dump data output path
  auto dump_cfg_path = std::getenv(dump::kConfigPath);
  if (dump_cfg_path != nullptr) {
    flags_->dump_tensor_data_ = true;
    if (InitDumpConfigFromJson(dump_cfg_path) != RET_OK) {
      MS_LOG(ERROR) << "parse dump config file failed.";
      return RET_ERROR;
    }
  } else {
    MS_LOG(INFO) << "No MINDSPORE_DUMP_CONFIG in env, don't need to dump data";
  }

  auto status = InitCallbackParameter();
  if (status != RET_OK) {
    MS_LOG(ERROR) << "Init callback Parameter failed.";
    std::cerr << "Init callback Parameter failed." << std::endl;
    return RET_ERROR;
  }

  return RET_OK;
}

int BenchmarkBase::PrintResult(const std::vector<std::string> &title,
                               const std::map<std::string, std::pair<int, float>> &result) {
  std::vector<size_t> columnLenMax(kPrintColNum);
  std::vector<std::vector<std::string>> rows;

  for (auto &iter : result) {
    char stringBuf[kPrintColNum][kPrintRowLenMax] = {};
    std::vector<std::string> columns;
    size_t len = 0;
    int index = 0;
    len = iter.first.size();
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.push_back(iter.first);

    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%f",
                   iter.second.second / static_cast<float>(flags_->loop_count_));
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%f", iter.second.second / op_cost_total_);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%d", iter.second.first);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%f", iter.second.second);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    rows.push_back(columns);
  }

  printf("-------------------------------------------------------------------------\n");
  for (int i = 0; i < kPrintColNum; i++) {
    auto printBuf = title[i];
    if (printBuf.size() > columnLenMax.at(i)) {
      columnLenMax.at(i) = printBuf.size();
    }
    printBuf.resize(columnLenMax.at(i), ' ');
    printf("%s\t", printBuf.c_str());
  }
  printf("\n");
  for (auto &row : rows) {
    for (int j = 0; j < kPrintColNum; j++) {
      auto printBuf = row[j];
      printBuf.resize(columnLenMax.at(j), ' ');
      printf("%s\t", printBuf.c_str());
    }
    printf("\n");
  }
  return RET_OK;
}

#ifdef ENABLE_ARM64
int BenchmarkBase::PrintPerfResult(const std::vector<std::string> &title,
                                   const std::map<std::string, std::pair<int, struct PerfCount>> &result) {
  std::vector<size_t> columnLenMax(kPrintColNum);
  std::vector<std::vector<std::string>> rows;

  for (auto &iter : result) {
    char stringBuf[kPrintColNum][kPrintRowLenMax] = {};
    std::vector<std::string> columns;
    size_t len = 0;
    int index = 0;
    len = iter.first.size();
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.push_back(iter.first);
    index++;
    float tmp = float_t(flags_->num_threads_) * iter.second.second.value[0] / float_t(flags_->loop_count_) / kFloatMSEC;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%.2f", tmp);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);
    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%f", iter.second.second.value[0] / op_cost_total_);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    index++;
    tmp = float_t(flags_->num_threads_) * iter.second.second.value[1] / float_t(flags_->loop_count_) / kFloatMSEC;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%.2f", tmp);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    index++;
    len = snprintf(stringBuf[index], sizeof(stringBuf[index]), "%f", iter.second.second.value[1] / op_cost2_total_);
    if (len > columnLenMax.at(index)) {
      columnLenMax.at(index) = len + kColumnLen;
    }
    columns.emplace_back(stringBuf[index]);

    rows.push_back(columns);
  }

  printf("-------------------------------------------------------------------------\n");
  for (int i = 0; i < kPrintColNum; i++) {
    auto printBuf = title[i];
    if (printBuf.size() > columnLenMax.at(i)) {
      columnLenMax.at(i) = printBuf.size();
    }
    printBuf.resize(columnLenMax.at(i), ' ');
    printf("%s\t", printBuf.c_str());
  }
  printf("\n");
  for (auto &row : rows) {
    for (int j = 0; j < kPrintColNum; j++) {
      auto printBuf = row[j];
      printBuf.resize(columnLenMax.at(j), ' ');
      printf("%s\t", printBuf.c_str());
    }
    printf("\n");
  }
  return RET_OK;
}
#endif

BenchmarkBase::~BenchmarkBase() {
  for (const auto &iter : this->benchmark_data_) {
    delete (iter.second);
  }
  this->benchmark_data_.clear();
}
}  // namespace lite
}  // namespace mindspore
