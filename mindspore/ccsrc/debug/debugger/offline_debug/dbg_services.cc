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
#include "debugger/offline_debug/dbg_services.h"

#include <algorithm>
#include <chrono>

DbgServices::DbgServices(bool verbose) {
  DbgLogger::verbose = verbose;
  char *dbg_log_path = getenv("OFFLINE_DBG_LOG");
  if (dbg_log_path != NULL) {
    DbgLogger::verbose = true;
  }
  debug_services_ = new DebugServices();
}

DbgServices::DbgServices(const DbgServices &other) {
  MS_LOG(INFO) << "cpp DbgServices object is created via copy";
  debug_services_ = new DebugServices(*other.debug_services_);
}

DbgServices &DbgServices::operator=(const DbgServices &other) {
  MS_LOG(INFO) << "cpp DbgServices object is being assigned a different state";
  if (this != &other) {
    delete debug_services_;
    debug_services_ = new DebugServices(*other.debug_services_);
  }
  return *this;
}

DbgServices::~DbgServices() {
  MS_LOG(INFO) << "cpp DbgServices object is deleted";
  delete debug_services_;
}

std::string DbgServices::GetVersion() {
  MS_LOG(INFO) << "get version is called";
  return "1.4.0";
}

int32_t DbgServices::Initialize(std::string net_name, std::string dump_folder_path, bool is_sync_mode) {
  MS_LOG(INFO) << "cpp DbgServices initialize network name " << net_name;
  MS_LOG(INFO) << "cpp DbgServices initialize dump folder path " << dump_folder_path;
  MS_LOG(INFO) << "cpp DbgServices initialize sync mode " << is_sync_mode;
  if (debug_services_ == nullptr) {
    MS_LOG(EXCEPTION) << "Debugger services initialize failed as occur null pointer error,"
                      << "may be due to memory allocation failure, check as: top";
  }
  debug_services_->SetNetName(net_name);
  debug_services_->SetDumpDir(dump_folder_path);
  debug_services_->SetSyncMode(is_sync_mode);
  return 0;
}

int32_t DbgServices::AddWatchpoint(
  unsigned int id, unsigned int watch_condition,
  std::map<std::string, std::map<std::string, std::variant<bool, std::vector<std::string>>>> check_nodes,
  std::vector<parameter_t> parameter_list) {
  MS_LOG(INFO) << "cpp start";

  MS_LOG(INFO) << "cpp DbgServices AddWatchpoint id " << id;
  MS_LOG(INFO) << "cpp DbgServices AddWatchpoint watch_condition " << watch_condition;
  for (auto const &node : check_nodes) {
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint name " << node.first;
    auto attr_map = node.second;

    bool is_output = std::get<bool>(attr_map["is_output"]);
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint is_output " << is_output;

    std::vector<std::string> rank_id_str = std::get<std::vector<std::string>>(attr_map["rank_id"]);
    std::vector<std::uint32_t> rank_id;
    std::transform(rank_id_str.begin(), rank_id_str.end(), std::back_inserter(rank_id),
                   [](std::string &id_str) -> std::uint32_t { return static_cast<uint32_t>(std::stoul(id_str)); });
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint rank_id ";
    for (auto const &i : rank_id) {
      MS_LOG(INFO) << i << " ";
    }

    // std::vector<uint32_t> root_graph_id = std::get<std::vector<uint32_t>>(attr_map["root_graph_id"]);
    std::vector<std::string> root_graph_id_str = std::get<std::vector<std::string>>(attr_map["root_graph_id"]);
    std::vector<std::uint32_t> root_graph_id;
    std::transform(
      root_graph_id_str.begin(), root_graph_id_str.end(), std::back_inserter(root_graph_id),
      [](std::string &graph_str) -> std::uint32_t { return static_cast<uint32_t>(std::stoul(graph_str)); });
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint root_graph_id";
    for (auto const &j : root_graph_id) {
      MS_LOG(INFO) << j << " ";
    }
  }

  for (auto const &parameter : parameter_list) {
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint parameter name " << parameter.name;
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint parameter disabled " << parameter.disabled;
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint parameter value " << parameter.value;
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint parameter hit " << parameter.hit;
    MS_LOG(INFO) << "cpp DbgServices AddWatchpoint parameter actual_value " << parameter.actual_value;
  }

  std::vector<std::tuple<std::string, bool>> check_node_list;
  std::vector<std::tuple<std::string, std::vector<uint32_t>>> check_node_device_list;
  std::vector<std::tuple<std::string, std::vector<uint32_t>>> check_node_graph_list;
  std::vector<DebugServices::parameter_t> parameter_list_backend;

  std::transform(check_nodes.begin(), check_nodes.end(), std::back_inserter(check_node_list),
                 [](auto &node) -> std::tuple<std::string, bool> {
                   auto attr_map = node.second;
                   return std::make_tuple(node.first, std::get<bool>(attr_map["is_output"]));
                 });

  std::transform(check_nodes.begin(), check_nodes.end(), std::back_inserter(check_node_device_list),
                 [](auto &node) -> std::tuple<std::string, std::vector<uint32_t>> {
                   auto attr_map = node.second;
                   std::vector<std::string> rank_id_str = std::get<std::vector<std::string>>(attr_map["rank_id"]);
                   std::vector<std::uint32_t> rank_id;
                   std::transform(
                     rank_id_str.begin(), rank_id_str.end(), std::back_inserter(rank_id),
                     [](std::string &id_str) -> std::uint32_t { return static_cast<uint32_t>(std::stoul(id_str)); });
                   return std::make_tuple(node.first, rank_id);
                 });

  std::transform(
    check_nodes.begin(), check_nodes.end(), std::back_inserter(check_node_graph_list),
    [](auto &node) -> std::tuple<std::string, std::vector<uint32_t>> {
      auto attr_map = node.second;
      std::vector<std::string> root_graph_id_str = std::get<std::vector<std::string>>(attr_map["root_graph_id"]);
      std::vector<std::uint32_t> root_graph_id;
      std::transform(
        root_graph_id_str.begin(), root_graph_id_str.end(), std::back_inserter(root_graph_id),
        [](std::string &graph_str) -> std::uint32_t { return static_cast<uint32_t>(std::stoul(graph_str)); });
      return std::make_tuple(node.first, root_graph_id);
    });

  std::transform(
    parameter_list.begin(), parameter_list.end(), std::back_inserter(parameter_list_backend),
    [](const parameter_t &parameter) -> DebugServices::parameter_t {
      return DebugServices::parameter_t{parameter.name, parameter.disabled, parameter.value, parameter.hit};
    });

  debug_services_->AddWatchpoint(id, watch_condition, 0, check_node_list, parameter_list_backend,
                                 &check_node_device_list, &check_node_graph_list);
  MS_LOG(INFO) << "cpp end";
  return 0;
}

int32_t DbgServices::RemoveWatchpoint(unsigned int id) {
  MS_LOG(INFO) << "cpp DbgServices RemoveWatchpoint id " << id;
  debug_services_->RemoveWatchpoint(id);
  return 0;
}

std::vector<watchpoint_hit_t> DbgServices::CheckWatchpoints(unsigned int iteration) {
  MS_LOG(INFO) << "cpp DbgServices CheckWatchpoint iteration " << iteration;

  std::vector<std::string> name;
  std::vector<std::string> slot;
  std::vector<int> condition;
  std::vector<unsigned int> watchpoint_id;
  std::vector<std::string> overflow_ops;
  std::vector<std::vector<DebugServices::parameter_t>> parameters;
  std::vector<int32_t> error_codes;
  std::vector<unsigned int> rank_id;
  std::vector<unsigned int> root_graph_id;
  std::vector<std::shared_ptr<TensorData>> tensor_list;
  std::vector<std::string> file_paths;

  const bool init_dbg_suspend = (iteration == UINT_MAX);

  tensor_list = debug_services_->ReadNeededDumpedTensors(iteration, &file_paths);

  debug_services_->CheckWatchpoints(&name, &slot, &condition, &watchpoint_id, &parameters, &error_codes, overflow_ops,
                                    file_paths, &tensor_list, init_dbg_suspend, true, true, &rank_id, &root_graph_id);

  std::vector<watchpoint_hit_t> hits;
  for (unsigned int i = 0; i < name.size(); i++) {
    std::vector<DebugServices::parameter_t> &parameter = parameters[i];
    std::vector<parameter_t> api_parameter_vector;
    for (const auto &p : parameter) {
      parameter_t api_parameter(p.name, p.disabled, p.value, p.hit, p.actual_value);
      api_parameter_vector.push_back(api_parameter);
    }
    watchpoint_hit_t hit(name[i], std::stoi(slot[i]), condition[i], watchpoint_id[i], api_parameter_vector,
                         error_codes[i], rank_id[i], root_graph_id[i]);

    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t name " << hit.name;
    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t slot " << hit.slot;
    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t watchpoint_id " << hit.watchpoint_id;
    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t error_code " << hit.error_code;
    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t rank_id " << hit.rank_id;
    MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t root_graph_id " << hit.root_graph_id;

    for (auto const &parameter_i : api_parameter_vector) {
      MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t parameter name " << parameter_i.name;
      MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t parameter disabled " << parameter_i.disabled;
      MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t parameter value " << parameter_i.value;
      MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t parameter hit " << parameter_i.hit;
      MS_LOG(INFO) << "cpp DbgServices watchpoint_hit_t parameter actual_value " << parameter_i.actual_value;
    }

    hits.push_back(hit);
  }
  return hits;
}

std::string GetTensorFullName(tensor_info_t info) { return info.node_name + ":" + std::to_string(info.slot); }

unsigned int GetTensorRankId(tensor_info_t info) { return info.rank_id; }

unsigned int GetTensorRootGraphId(tensor_info_t info) { return info.root_graph_id; }

unsigned int GetTensorIteration(tensor_info_t info) { return info.iteration; }

unsigned int GetTensorSlot(tensor_info_t info) { return info.slot; }

bool GetTensorIsOutput(tensor_info_t info) { return info.is_output; }

std::vector<tensor_data_t> DbgServices::ReadTensors(std::vector<tensor_info_t> info) {
  for (auto i : info) {
    MS_LOG(INFO) << "cpp DbgServices ReadTensor info name " << i.node_name << ", slot " << i.slot << ", iteration "
                 << i.iteration << ", rank_id " << i.rank_id << ", root_graph_id " << i.root_graph_id << ", is_output "
                 << i.is_output;
  }
  std::vector<std::string> backend_name;
  std::vector<unsigned int> rank_id;
  std::vector<unsigned int> root_graph_id;
  std::vector<unsigned int> iteration;
  std::vector<size_t> slot;
  std::vector<std::shared_ptr<TensorData>> result_list;
  std::vector<tensor_data_t> tensors_read;
  std::vector<bool> is_output;

  std::transform(info.begin(), info.end(), std::back_inserter(backend_name), GetTensorFullName);
  std::transform(info.begin(), info.end(), std::back_inserter(slot), GetTensorSlot);
  std::transform(info.begin(), info.end(), std::back_inserter(rank_id), GetTensorRankId);
  std::transform(info.begin(), info.end(), std::back_inserter(root_graph_id), GetTensorRootGraphId);
  std::transform(info.begin(), info.end(), std::back_inserter(iteration), GetTensorIteration);
  std::transform(info.begin(), info.end(), std::back_inserter(is_output), GetTensorIsOutput);

  MS_LOG(INFO) << "cpp before";
  std::vector<std::string> file_paths;
  auto t1 = std::chrono::high_resolution_clock::now();
  // Convert the dumped data to npy format if it's async mode.
  if (!debug_services_->GetSyncMode()) {
    debug_services_->ConvertReadTensors(backend_name, slot, rank_id, iteration, root_graph_id, &file_paths);
  }
  debug_services_->ReadDumpedTensor(backend_name, slot, rank_id, iteration, root_graph_id, is_output, file_paths,
                                    &result_list);
  auto t2 = std::chrono::high_resolution_clock::now();
  /* Getting number of milliseconds as a double. */
  std::chrono::duration<double, std::milli> ms_double = t2 - t1;

  MS_LOG(INFO) << "ReadTensors Took: " << ms_double.count() / 1000 << "s";
  MS_LOG(INFO) << "cpp after";

  for (auto result : result_list) {
    tensor_data_t tensor_data_item(result->GetDataPtr(), result->GetByteSize(), result->GetType(), result->GetShape());
    tensors_read.push_back(tensor_data_item);
  }
  MS_LOG(INFO) << "cpp end";
  return tensors_read;
}
