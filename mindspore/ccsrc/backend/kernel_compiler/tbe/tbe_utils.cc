/**
 * Copyright 2019 Huawei Technologies Co., Ltd
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

#include "backend/kernel_compiler/tbe/tbe_utils.h"

#include <dirent.h>
#include <string>
#include <map>
#include <set>
#include <list>
#include <functional>
#include <iostream>
#include <fstream>

#include "runtime/kernel.h"
#include "utils/utils.h"
#include "utils/ms_utils.h"
#include "utils/ms_context.h"
#include "ir/dtype/type.h"
#include "runtime/dev.h"
#include "runtime/device/ascend/lic_manager.h"
#include "backend/session/anf_runtime_algorithm.h"
#include "backend/kernel_compiler/tbe/tbe_convert_utils.h"
#include "mindspore/ccsrc/backend/kernel_compiler/tbe/tbe_json/tbe_json_creator.h"
#include "mindspore/ccsrc/backend/kernel_compiler/tbe/tbe_json/single_tbe_json_creator.h"
#include "securec/include/securec.h"
#include "utils/json_operation_utils.h"
#include "mindspore/ccsrc/debug/common.h"

namespace mindspore {
namespace kernel {
namespace tbe {
constexpr auto kCceKernelMeta = "kernel_meta/";
constexpr auto kJsonSuffix = ".json";
constexpr auto kInfoSuffix = ".info";
constexpr auto kSOC_VERSION = "SOC_VERSION";
constexpr auto kBuildRes = "build_result";
constexpr auto kTUNE_BANK_PATH = "TUNE_BANK_PATH";
constexpr auto kTUNE_DUMP_PATH = "TUNE_DUMP_PATH";
constexpr auto kJRlTuneSwitch = "rl_tune_switch";
constexpr auto kJRlTuneList = "rl_tune_list";
constexpr auto kJOpTuneSwitch = "op_tune_switch";
constexpr auto kJOpTuneList = "op_tune_list";
constexpr auto kJPassList = "pass_list";
constexpr auto kRankID = "RANK_ID";
constexpr auto kCOMPILER_OP_LEVEL = "MS_COMPILER_OP_LEVEL";
constexpr auto kCOMPILER_CACHE_PATH = "MS_COMPILER_CACHE_PATH";

uintptr_t KernelManager::kernel_stub_gen_ = 0;
std::unordered_map<string, KernelMetaPtr> KernelManager::info_table_ = {};

void TbeUtils::GenLicInfo(nlohmann::json *lic_info_json) {
  MS_EXCEPTION_IF_NULL(lic_info_json);
  (*lic_info_json)[kJRlTuneSwitch] = LicManager::GetInstance().GetRlTuneSwitch();
  (*lic_info_json)[kJRlTuneList] = LicManager::GetInstance().GetRlTuneList();
  (*lic_info_json)[kJOpTuneSwitch] = LicManager::GetInstance().GetOpTuneSwitch();
  (*lic_info_json)[kJOpTuneList] = LicManager::GetInstance().GetOpTuneList();
  (*lic_info_json)[kJPassList] = LicManager::GetInstance().GetPassSwitch();
}

std::string TbeUtils::GetBankPath() {
  // tune bank path
  auto save_path = common::GetEnv(kTUNE_BANK_PATH);
  char real_path[PATH_MAX] = {0};
  if (!save_path.empty()) {
    if (realpath(save_path.c_str(), real_path)) {
      save_path = real_path;
      return save_path;
    }
    MS_LOG(EXCEPTION) << "Invalid env TUNE_BANK_PATH, path : " << save_path;
  }
  return "";
}

std::string TbeUtils::GetTuneDumpPath() {
  // tune dump path
  auto save_path = common::GetEnv(kTUNE_DUMP_PATH);
  char real_path[PATH_MAX] = {0};
  if (!save_path.empty()) {
    if (realpath(save_path.c_str(), real_path)) {
      save_path = real_path;
      return save_path;
    }
    MS_LOG(EXCEPTION) << "Invalid env kTUNE_DUMP_PATH, path : " << save_path;
  }
  return "";
}

std::string TbeUtils::GetOpDebugPath() {
  auto old_build = common::GetEnv("MS_OLD_BUILD_PROCESS");
  auto config_path = Common::CommonFuncForConfigPath("./", common::GetEnv(kCOMPILER_CACHE_PATH));
  if (!old_build.empty()) {
    if (config_path[config_path.length() - 1] == '/') {
      return config_path;
    }
    return config_path + "/";
  } else {
    std::string rank_id_str = common::GetEnv(kRankID);
    if (rank_id_str.empty()) {
      MS_LOG(DEBUG) << "Using the default value: 0";
      rank_id_str = "0";
    }
    if (config_path[config_path.length() - 1] == '/') {
      return config_path + "rank_" + rank_id_str + "/";
    }
    return config_path + "/" + "rank_" + rank_id_str + "/";
  }
}

std::string GetOpDebugLevel() {
  const std::set<std::string> exp = {"0", "1"};
  std::string op_debug_level = "0";
  auto env_level = common::GetEnv(kCOMPILER_OP_LEVEL);
  if (!env_level.empty()) {
    if (exp.find(env_level) == exp.end()) {
      MS_LOG(WARNING) << "Invalid COMPILER_OP_LEVEL env:" << env_level
                      << ", the value should be 0 or 1, now using the default value 0";
    } else {
      op_debug_level = env_level;
    }
  }
  return op_debug_level;
}

void TbeUtils::GenSocInfo(nlohmann::json *soc_info_json) {
  auto context_ptr = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(context_ptr);
  MS_EXCEPTION_IF_NULL(soc_info_json);
  std::list<int64_t> list;
  (*soc_info_json)["coreNum"] = "";
  (*soc_info_json)["coreType"] = "";
  (*soc_info_json)["op_impl_mode"] = "";
  (*soc_info_json)["vector_fp_ceiling"] = "";
  (*soc_info_json)["op_impl_mode_list"] = list;
  (*soc_info_json)["l2Mode"] = "2";
  (*soc_info_json)["l1Fusion"] = "false";
  (*soc_info_json)["l2Fusion"] = "false";
  (*soc_info_json)["op_bank_update"] = false;
  (*soc_info_json)["socVersion"] = GetSocVersion();
  (*soc_info_json)["offlineTune"] = CheckOfflineTune();
  (*soc_info_json)["op_debug_dir"] = GetOpDebugPath();
  (*soc_info_json)["op_debug_level"] = GetOpDebugLevel();
  (*soc_info_json)["autoTilingMode"] = context_ptr->get_param<std::string>(MS_CTX_TUNE_MODE);
  (*soc_info_json)["deviceId"] = std::to_string(context_ptr->get_param<uint32_t>(MS_CTX_DEVICE_ID));
  (*soc_info_json)["op_bank_path"] = Common::CommonFuncForConfigPath("", common::GetEnv("OP_BANK_PATH"));
  (*soc_info_json)["mdl_bank_path"] = Common::CommonFuncForConfigPath("", common::GetEnv("MDL_BANK_PATH"));
}

void TbeUtils::SaveJsonInfo(const std::string &json_name, const std::string &info) {
  auto config_path = TbeUtils::GetOpDebugPath();
  std::string path = config_path + kCceKernelMeta + json_name + kInfoSuffix;
  auto realpath = Common::GetRealPath(path);
  if (!realpath.has_value()) {
    MS_LOG(WARNING) << "Get real path failed, invalid path: " << realpath.value();
    return;
  }
  ChangeFileMode(realpath.value(), S_IWUSR);
  std::ofstream file_write(realpath.value());
  if (!file_write.is_open()) {
    MS_LOG(WARNING) << "Create info file failed(" << realpath.value() << ").";
    return;
  }
  file_write << info << std::endl;
  file_write.close();
  file_write.clear();
  ChangeFileMode(realpath.value(), S_IRUSR);
}

void TbeUtils::LoadCache() {
  static bool has_load = false;
  if (!has_load) {
    auto bin_map = KernelMeta::GetInstance();
    auto config_path = TbeUtils::GetOpDebugPath();
    auto path = config_path + kCceKernelMeta;
    if (!bin_map->ReadIndex(path)) {
      MS_LOG(INFO) << "Cache initialize failed[" << path << "]";
    }
    has_load = true;
  }
}

KernelPackPtr TbeUtils::SearchCache(const std::string &kernel_name, const bool is_akg) {
  // search cache.
  KernelMeta *bin_map = KernelMeta::GetInstance();
  if (bin_map == nullptr) {
    MS_LOG(INFO) << "kernel cache is invalid.";
    return nullptr;
  }
  return bin_map->GetKernelPack(kernel_name, is_akg);
}

KernelPackPtr TbeUtils::InsertCache(const std::string &kernel_name, const std::string &processor, const bool is_akg) {
  MS_LOG(INFO) << "kernel name:  " << kernel_name << ", processr:" << processor;
  if (processor != kProcessorAiCore) {
    MS_LOG(EXCEPTION) << "process type should be aicore, actually is: " << processor;
  }
  return SearchCache(kernel_name, is_akg);
}

int KernelManager::BinaryRegister(const mindspore::kernel::FlexArray &kernel_buffer, void **module, const string &magic,
                                  const bool dynamic_flag) {
  static std::map<string, uint32_t> magic_maps = {{"RT_DEV_BINARY_MAGIC_ELF", RT_DEV_BINARY_MAGIC_ELF},
                                                  {"RT_DEV_BINARY_MAGIC_PLAIN", RT_DEV_BINARY_MAGIC_PLAIN},
                                                  {"RT_DEV_BINARY_MAGIC_PLAIN_AICPU", RT_DEV_BINARY_MAGIC_PLAIN_AICPU},
                                                  {"RT_DEV_BINARY_MAGIC_ELF_AICPU", RT_DEV_BINARY_MAGIC_ELF_AICPU}};
  // object for device register.
  rtDevBinary_t dev_bin;
  dev_bin.data = kernel_buffer.contents;
  auto iter = magic_maps.find(magic);
  if (iter == magic_maps.end()) {
    MS_LOG(INFO) << "Invalid magic number: " << magic;
    return -1;
  }
  dev_bin.magic = iter->second;
  dev_bin.length = kernel_buffer.len;
  dev_bin.version = 0;
  auto ret = dynamic_flag ? rtRegisterAllKernel(&dev_bin, module) : rtDevBinaryRegister(&dev_bin, module);
  if (RT_ERROR_NONE != ret) {
    MS_LOG(INFO) << "Call runtime rtDevBinaryRegister error.";
    return -1;
  }
  return 0;
}

uintptr_t KernelManager::GenFuncStub(const mindspore::kernel::KernelPack &kernel_pack, bool force_reload,
                                     uint32_t *block_dim, const bool dynamic_flag, void **handle,
                                     std::string *origin_key) {
  auto kernel = kernel_pack.GetKernel();
  if (kernel == nullptr) {
    MS_LOG(EXCEPTION) << "Invalid kernel pack, json or kernel is nullptr.";
  }
  auto kernel_contents = kernel->contents;
  if (kernel_contents == nullptr) {
    MS_LOG(EXCEPTION) << "Invalid kernel context, json or kernel is nullptr.";
  }
  auto kernel_json_info = kernel_pack.kernel_json_info();

  *block_dim = kernel_json_info.block_dim;
  string func_name = kernel_json_info.kernel_name;
  string magic = kernel_json_info.magic;

  if (!force_reload) {
    // use the cached object.
    auto iter = info_table_.find(func_name);
    if (iter != info_table_.end()) {
      auto kernelmeta = iter->second;
      *block_dim = kernelmeta->block_dim_;
      if (!dynamic_flag) {
        return kernelmeta->func_stub_;
      }
    }
  }
  void *module = nullptr;
  if (BinaryRegister((*kernel_pack.GetKernel()), &module, magic, dynamic_flag) != 0) {
    MS_LOG(INFO) << "Call runtime BinaryRegister error.";
    if (module != nullptr) {
      (void)rtDevBinaryUnRegister(module);
    }
    return 0;
  }
  if (dynamic_flag) {
    *handle = module;
    *origin_key = func_name;
    return 1;
  }
  // to diff different funcs.
  uintptr_t func_stub = ++kernel_stub_gen_;
  if (RT_ERROR_NONE !=
      rtFunctionRegister(module, reinterpret_cast<void *>(func_stub), func_name.c_str(), func_name.c_str(), 0)) {
    MS_LOG(INFO) << "Call runtime rtFunctionRegister error.";
    return 0;
  }
  // cache the registered kernelmeta.
  info_table_[func_name] = std::make_shared<KernelMetaInfo>(KernelMetaInfo{func_stub, *block_dim});
  return func_stub;
}

std::string KernelManager::GetStubFuncName(const KernelPackPtr &kernel_pack) {
  MS_EXCEPTION_IF_NULL(kernel_pack);
  auto kernel_json_info = kernel_pack->kernel_json_info();
  return kernel_json_info.kernel_name;
}

KernelMeta *KernelMeta::GetInstance() {
  static KernelMeta inst;
  return &inst;
}

bool KernelMeta::ReadIndex(const std::string &bin_dir) {
  DIR *dir = opendir(bin_dir.c_str());
  if (dir == nullptr) {
    auto ret = mkdir(bin_dir.c_str(), S_IRWXG | S_IRWXU);
    if (ret != 0) {
      MS_LOG(INFO) << "kernel dir: " << bin_dir << "not exist";
      return false;
    }
    dir = opendir(bin_dir.c_str());
  }
  struct dirent *entry;
  constexpr size_t SUFFIX_LENS = 5;
  while ((entry = readdir(dir)) != nullptr) {
    string bin_dir_tmp = bin_dir;
    std::string cce_json = entry->d_name;
    if (cce_json.length() <= SUFFIX_LENS) {
      continue;
    }
    std::string suffix = cce_json.substr(cce_json.length() - SUFFIX_LENS);
    if (suffix != kJsonSuffix) {
      continue;
    }
    auto sp = cce_json.rfind('/');
    if (sp != std::string::npos) {
      continue;
    }
    sp = cce_json.rfind('.');
    if (sp == std::string::npos) {
      continue;
    }
    auto kernel_name = cce_json.substr(0, sp);
    (void)bin_dir_tmp.append("/");
    (void)bin_dir_tmp.append(cce_json);
    kernel_index_map_[kernel_name] = bin_dir_tmp;
  }
  (void)closedir(dir);

  return true;
}

void TbeUtils::GetCompileInfo(const AnfNodePtr &node, std::string *compile_info, bool *get_flag) {
  MS_EXCEPTION_IF_NULL(node);
  MS_LOG(INFO) << "Get compile info from json file start. [" << node->fullname_with_scope() << "]";
  auto json_creator = std::make_shared<kernel::BuildTbeJsonCreator>();
  MS_EXCEPTION_IF_NULL(json_creator);
  nlohmann::json kernel_json;
  if (!json_creator->GenJson(node, &kernel_json)) {
    MS_LOG(WARNING) << "Gen kernel json failed [" << node->fullname_with_scope() << "]";
    *get_flag = false;
    return;
  }
  auto json_name = json_creator->GetJsonName();
  auto config_path = TbeUtils::GetOpDebugPath();
  std::string path = config_path + kCceKernelMeta + json_name + kJsonSuffix;
  if (path.size() > PATH_MAX) {
    MS_LOG(WARNING) << "File path: " << path << "is too long.";
    *get_flag = false;
    return;
  }
  nlohmann::json read_new_json;
  std::ifstream file(path.c_str());
  std::string ori_file = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!ParseJson(ori_file, &read_new_json)) {
    MS_LOG(EXCEPTION) << "Parse compile info error.";
  }
  *compile_info = read_new_json[kBuildRes].dump();
  file.close();
  file.clear();
  MS_LOG(INFO) << "Get compile info from json file success";
}

void TbeUtils::SaveCompileInfo(const std::string &json_name, const std::string &build_res, bool *save_flag) {
  MS_LOG(INFO) << "Save compile info to json file start. [" << json_name << "], value: " << build_res;
  auto config_path = TbeUtils::GetOpDebugPath();
  std::string path = config_path + kCceKernelMeta + json_name + kJsonSuffix;
  if (path.size() > PATH_MAX) {
    MS_LOG(WARNING) << "File path: " << path << "is too long.";
    *save_flag = false;
    return;
  }
  nlohmann::json save_new_json;
  std::ifstream file(path.c_str());
  std::string ori_file = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!ParseJson(ori_file, &save_new_json)) {
    MS_LOG(EXCEPTION) << "Parse compile info error.";
  }
  file.close();
  file.clear();
  if (build_res.empty()) {
    save_new_json[kBuildRes] = build_res;
  } else {
    save_new_json[kBuildRes] = nlohmann::json::parse(build_res);
  }
  std::ofstream file_write;
  file_write.open(path);
  if (!file_write.is_open()) {
    MS_LOG(WARNING) << "Create info file failed. [" << path << "]";
    *save_flag = false;
    return;
  }
  const int indent = 4;
  auto info = save_new_json.dump(indent);
  file_write << info << std::endl;
  file_write.close();
  file_write.clear();
  MS_LOG(INFO) << "Save compile info to json file success";
}

bool TbeUtils::CheckOfflineTune() {
  bool offline = false;
  std::string offline_tune = common::GetEnv("ENABLE_TUNE_DUMP");
  if (!offline_tune.empty()) {
    for (size_t j = 0; j < offline_tune.length(); j++) {
      offline_tune[j] = tolower(offline_tune[j]);
    }
    if (!(offline_tune == "true" || offline_tune == "false")) {
      MS_LOG(EXCEPTION) << "The value of ENABLE_TUNE_DUMP must be 'true' or 'false'";
    }
    offline = (offline_tune == "true");
  }
  return offline;
}

std::string TbeUtils::GetSocVersion() {
  // Get default soc version.
  static std::string version;
  if (version.empty()) {
    const int kSocVersionLen = 50;
    char soc_version[kSocVersionLen] = {0};
    auto ret = rtGetSocVersion(soc_version, kSocVersionLen);
    if (ret != RT_ERROR_NONE) {
      MS_LOG(EXCEPTION) << "GetSocVersion failed.";
    }
    // Get soc version from env value.
    const char *soc_version_env = nullptr;
    std::string str_soc_version_env = common::GetEnv(kSOC_VERSION);
    if (!str_soc_version_env.empty()) {
      soc_version_env = common::SafeCStr(str_soc_version_env);
    }
    if (soc_version_env != nullptr) {
      if (std::strcmp(soc_version, soc_version_env) != 0) {
        MS_LOG(DEBUG) << "Detected the env SOC_VERSION, so the SocVersion will be changed to " << str_soc_version_env
                      << ".";
        ret = rtSetSocVersion(soc_version_env);
        if (ret != RT_ERROR_NONE) {
          MS_LOG(EXCEPTION) << "SetSocVersion failed, errorno: " << ret;
        }
        version = soc_version_env;
        return soc_version_env;
      }
    }
    version = soc_version;
  }
  return version;
}

KernelPackPtr KernelMeta::GetKernelPack(const std::string &kernel_name, const bool is_akg) {
  KernelPackPtr ret = nullptr;
  // 1. pack has been created
  auto kernel_pack_iter = kernel_pack_map_.find(kernel_name);
  if (kernel_pack_iter != kernel_pack_map_.end()) {
    ret = kernel_pack_iter->second;
  } else {
    // 2. kernel file has been create, but pack does not been created.
    auto config_path = TbeUtils::GetOpDebugPath();
    std::string cce_json = is_akg ? ("./kernel_meta/" + kernel_name + kJsonSuffix)
                                  : (config_path + kCceKernelMeta + kernel_name + kJsonSuffix);
    ret = std::make_shared<KernelPack>();
    if (!ret->LoadKernelMeta(cce_json)) {
      MS_LOG(INFO) << "Read cache json and bin file failed[" << cce_json << "]";
      return nullptr;
    }
    kernel_pack_map_[kernel_name] = ret;
    auto iter = kernel_index_map_.find(kernel_name);
    if (iter == kernel_index_map_.end()) {
      MS_LOG(INFO) << "kernel name [" << kernel_name << "] has been created first.";
      kernel_index_map_[kernel_name] = cce_json;
    }
  }
  return ret;
}
}  // namespace tbe
}  // namespace kernel
}  // namespace mindspore
