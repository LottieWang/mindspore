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

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <memory>
#include <algorithm>
#include <fstream>
#include <iostream>

#include "load_mindir/load_model.h"
#include "load_mindir/anf_model_parser.h"
#include "proto/mind_ir.pb.h"
#include "utils/crypto.h"

using std::string;
using std::vector;

namespace mindspore {
std::shared_ptr<std::vector<char>> ReadProtoFile(const std::string &file) {
  if (file.empty()) {
    MS_LOG(ERROR) << "file is nullptr";
    return nullptr;
  }

  char real_path[PATH_MAX] = {0};
#if defined(_WIN32) || defined(_WIN64)
  if (_fullpath(real_path, file.c_str(), PATH_MAX) == nullptr) {
    MS_LOG(ERROR) << "Get realpath failed, mind ir file is" << file;
    return nullptr;
  }
#else
  if (realpath(file.c_str(), real_path) == nullptr) {
    MS_LOG(ERROR) << "Get realpath failed, mind ir file is" << file;
    return nullptr;
  }
#endif

  std::ifstream ifs(real_path);
  if (!ifs.good()) {
    MS_LOG(ERROR) << "file: " << real_path << " is not exist";
    return nullptr;
  }

  if (!ifs.is_open()) {
    MS_LOG(ERROR) << "file: " << real_path << "open failed";
    return nullptr;
  }

  ifs.seekg(0, std::ios::end);
  size_t size = ifs.tellg();
  std::shared_ptr<std::vector<char>> buf(new (std::nothrow) std::vector<char>(size));
  if (buf == nullptr) {
    MS_LOG(ERROR) << "malloc buf failed, file: " << real_path;
    ifs.close();
    return nullptr;
  }

  ifs.seekg(0, std::ios::beg);
  ifs.read(buf->data(), size);
  ifs.close();

  return buf;
}

bool get_all_files(const std::string &dir_in, std::vector<std::string> *files) {
  if (dir_in.empty()) {
    return false;
  }
  struct stat s;
  int ret = stat(dir_in.c_str(), &s);
  if (ret != 0) {
    MS_LOG(ERROR) << "stat error, ret is : " << ret;
    return false;
  }
  if (!S_ISDIR(s.st_mode)) {
    return false;
  }
  DIR *open_dir = opendir(dir_in.c_str());
  if (open_dir == NULL) {
    MS_LOG(EXCEPTION) << "open dir " << dir_in.c_str() << " failed";
  }
  dirent *p = nullptr;
  while ((p = readdir(open_dir)) != nullptr) {
    struct stat st;
    if (p->d_name[0] != '.') {
      std::string name = dir_in + std::string("/") + std::string(p->d_name);
      ret = stat(name.c_str(), &st);
      if (ret != 0) {
        MS_LOG(ERROR) << "stat error, ret is : " << ret;
        return false;
      }
      if (S_ISDIR(st.st_mode)) {
        ret = get_all_files(name, files);
        if (!ret) {
          MS_LOG(ERROR) << "Get files failed, ret is : " << ret;
          return false;
        }
      } else if (S_ISREG(st.st_mode)) {
        files->push_back(name);
      }
    }
  }
  closedir(open_dir);
  return true;
}

int endsWith(string s, string sub) { return s.rfind(sub) == (s.length() - sub.length()) ? 1 : 0; }

bool ParseModelProto(mind_ir::ModelProto *model, const std::string &path, const unsigned char *dec_key,
                     const size_t key_len, const std::string &dec_mode) {
  if (dec_key != nullptr) {
    size_t plain_len;
    auto plain_data = Decrypt(&plain_len, path, dec_key, key_len, dec_mode);
    if (plain_data == nullptr) {
      MS_LOG(ERROR) << "Decrypt MindIR file failed, please check the correctness of the dec_key or dec_mode.";
      return false;
    }
    if (!model->ParseFromArray(reinterpret_cast<char *>(plain_data.get()), static_cast<int32_t>(plain_len))) {
      MS_LOG(ERROR) << "Load MindIR file failed, please check the correctness of the file, dec_key or dec_mode.";
      return false;
    }
  } else {
    std::fstream input_graph(path, std::ios::in | std::ios::binary);
    if (!input_graph || !model->ParseFromIstream(&input_graph)) {
      MS_LOG(ERROR) << "Load MindIR file failed, please check the correctness of the file.";
      return false;
    }
  }
  return true;
}

bool ParseGraphProto(mind_ir::GraphProto *graph, const std::string &path, const unsigned char *dec_key,
                     const size_t key_len, const std::string &dec_mode) {
  if (dec_key != nullptr) {
    size_t plain_len;
    auto plain_data = Decrypt(&plain_len, path, dec_key, key_len, dec_mode);
    if (plain_data == nullptr) {
      MS_LOG(ERROR) << "Decrypt MindIR file failed, please check the correctness of the dec_key or dec_mode.";
      return false;
    }
    if (!graph->ParseFromArray(reinterpret_cast<char *>(plain_data.get()), static_cast<int32_t>(plain_len))) {
      MS_LOG(ERROR) << "Load variable file failed, please check the correctness of the mindir's variable file, "
                       "dec_key or dec_mode";
      return false;
    }
  } else {
    std::fstream input_param(path, std::ios::in | std::ios::binary);
    if (!input_param || !graph->ParseFromIstream(&input_param)) {
      MS_LOG(ERROR) << "Load variable file failed, please check the correctness of mindir's variable file.";
      return false;
    }
  }
  return true;
}

std::vector<std::shared_ptr<FuncGraph>> LoadMindIRs(std::vector<std::string> file_names, bool is_lite,
                                                    const unsigned char *dec_key, const size_t key_len,
                                                    const std::string &dec_mode, bool inc_load) {
  std::vector<std::shared_ptr<FuncGraph>> funcgraph_vec;
  MS_LOG(DEBUG) << "Load multiple MindIR files.";
  for (const auto &file_name : file_names) {
    MS_LOG(DEBUG) << "Load " << file_name;
    funcgraph_vec.push_back(LoadMindIR(file_name, is_lite, dec_key, key_len, dec_mode, inc_load));
  }
  return funcgraph_vec;
}

std::shared_ptr<FuncGraph> LoadMindIR(const std::string &file_name, bool is_lite, const unsigned char *dec_key,
                                      const size_t key_len, const std::string &dec_mode, bool inc_load) {
  if (file_name.length() > PATH_MAX) {
    MS_LOG(ERROR) << "The length of the file name exceeds the limit.";
    return nullptr;
  }
  const char *file_path = file_name.c_str();
  char abs_path_buff[PATH_MAX];
  char abs_path[PATH_MAX];
  vector<string> files;

#ifdef _WIN32
  _fullpath(abs_path_buff, file_path, PATH_MAX);
#else
  if (!realpath(file_path, abs_path_buff)) {
    MS_LOG(ERROR) << "Load MindIR get absolute path failed";
  }
#endif
  // Read graph
  mind_ir::ModelProto origin_model;
  if (!ParseModelProto(&origin_model, std::string(abs_path_buff), dec_key, key_len, dec_mode)) {
    return nullptr;
  }
  // Load parameter into graph
  if (endsWith(abs_path_buff, "_graph.mindir") && origin_model.graph().parameter_size() == 0) {
    if (strlen(abs_path_buff) < strlen("graph.mindir")) {
      MS_LOG(ERROR) << "The abs_path_buff length is less than 'graph.mindir'.";
      return nullptr;
    }
    int path_len = SizeToInt(strlen(abs_path_buff) - strlen("graph.mindir"));
    int ret = memcpy_s(abs_path, sizeof(abs_path), abs_path_buff, path_len);
    if (ret != 0) {
      MS_LOG(ERROR) << "Load MindIR occur memcpy_s error.";
      return nullptr;
    }
    abs_path[path_len] = '\0';
    snprintf(abs_path + path_len, sizeof(abs_path) - path_len, "variables");
    std::ifstream ifs(abs_path);
    if (ifs.good()) {
      MS_LOG(DEBUG) << "MindIR file has variables path, load parameter into graph.";
      string path = abs_path;
      get_all_files(path, &files);
    } else {
      MS_LOG(ERROR) << "Load graph's variable folder failed, please check the correctness of variable folder.";
      return nullptr;
    }

    size_t file_size = files.size();
    mind_ir::GraphProto *mod_graph = origin_model.mutable_graph();
    for (size_t file_index = 0; file_index < file_size; file_index++) {
      mind_ir::GraphProto param_graph;
      if (!ParseGraphProto(&param_graph, files[file_index], dec_key, key_len, dec_mode)) {
        return nullptr;
      }

      if (param_graph.parameter_size() < 0 || param_graph.parameter_size() > INT_MAX) {
        MS_LOG(ERROR) << "param_graph.parameter_size() is : " << param_graph.parameter_size();
        return nullptr;
      }
      for (int param_index = 0; param_index < param_graph.parameter_size(); param_index++) {
        mind_ir::TensorProto *param_proto = mod_graph->add_parameter();
        param_proto->set_name(param_graph.parameter(param_index).name());
        param_proto->set_data_type(param_graph.parameter(param_index).data_type());
        param_proto->set_raw_data(param_graph.parameter(param_index).raw_data());
        for (const auto &dim : param_graph.parameter(param_index).dims()) {
          param_proto->add_dims(dim);
        }
      }
    }
  }

  MSANFModelParser model_parser;
  if (is_lite) {
    model_parser.SetLite();
  }
  if (inc_load) {
    model_parser.SetIncLoad();
  }
  FuncGraphPtr dstgraph_ptr = model_parser.Parse(origin_model);
  return dstgraph_ptr;
}

std::shared_ptr<FuncGraph> ConvertStreamToFuncGraph(const char *buf, const size_t buf_size, bool is_lite) {
  MS_EXCEPTION_IF_NULL(buf);
  std::string str((const char *)buf, buf_size);
  mind_ir::ModelProto model_;
  if (!model_.ParseFromString(str)) {
    MS_LOG(ERROR) << "Parse model from buffer fail!";
  }
  MSANFModelParser model_parser;
  if (is_lite) {
    model_parser.SetLite();
  }
  FuncGraphPtr dstgraph_ptr = model_parser.Parse(model_);
  return dstgraph_ptr;
}
}  // namespace mindspore
