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

#include "thread/core_affinity.h"
#include <string.h>
#include <stdlib.h>
#include <string>
#include <algorithm>
#ifdef MS_COMPILE_IOS
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/machine.h>
#endif  // MS_COMPILE_IOS
#include "thread/threadpool.h"

namespace mindspore {

#define MAX_PATH_SIZE (256)

enum Arch {
  UnKnown_Arch = 0,
  Cortex_A5,
  Cortex_A7,
  Cortex_A8,
  Cortex_A9,
  Cortex_A12,
  Cortex_A15,
  Cortex_A17,
  Cortex_A32,
  Cortex_A34,
  Cortex_A35,
  Cortex_A53,
  Cortex_A55,
  Cortex_A57,
  Cortex_A65,
  Cortex_A72,
  Cortex_A73,
  Cortex_A75,
  Cortex_A76,
  Cortex_A77,
  Cortex_A78,
  Cortex_X1
};

typedef struct {
  int core_id;
  int max_freq;
  enum Arch arch;
} CpuInfo;

enum Arch GetArch(int cpu_part) {
  typedef struct {
    int part;
    enum Arch arch;
  } ArchSet;
  // https://en.wikipedia.org/wiki/Comparison_of_ARMv7-A_cores
  // https://en.wikipedia.org/wiki/Comparison_of_ARMv8-A_cores
  std::vector<ArchSet> arch_set = {
    {0x800, Cortex_A73},  // High-performance Kryo 260 (r10p2) / Kryo 280 (r10p1) "Gold" -> Cortex-A73
    {0x801, Cortex_A53},  // Low-power Kryo 260 / 280 "Silver" -> Cortex-A53
    {0x802, Cortex_A75},  // High-performance Kryo 385 "Gold" -> Cortex-A75
    {0x803, Cortex_A55},  // Low-power Kryo 385 "Silver" -> Cortex-A55r0
    {0x804, Cortex_A76},  // High-performance Kryo 485 "Gold" / "Gold Prime" -> Cortex-A76
    {0x805, Cortex_A55},  // Low-performance Kryo 485 "Silver" -> Cortex-A55
    {0xC05, Cortex_A5},
    {0xC07, Cortex_A7},
    {0xC08, Cortex_A8},
    {0xC09, Cortex_A9},
    {0xC0C, Cortex_A12},
    {0xC0D, Cortex_A12},
    {0xC0E, Cortex_A17},
    {0xC0F, Cortex_A15},
    {0xD01, Cortex_A32},  // also Huawei Kunpeng 920
                          // series taishan_v110 when not
                          // on android
    {0xD02, Cortex_A34},
    {0xD03, Cortex_A53},
    {0xD04, Cortex_A35},
    {0xD05, Cortex_A55},
    {0xD06, Cortex_A65},
    {0xD07, Cortex_A57},
    {0xD08, Cortex_A72},
    {0xD09, Cortex_A73},
    {0xD0A, Cortex_A75},
    {0xD0B, Cortex_A76},
    {0xD0D, Cortex_A77},
    {0xD0E, Cortex_A76},  // Cortex-A76AE
    {0xD40, Cortex_A76},  // Kirin 980 Big/Medium cores -> Cortex-A76
    {0xD41, Cortex_A78},
    {0xD43, Cortex_A65},  // Cortex-A65AE
    {0xD44, Cortex_X1}};
  auto item =
    std::find_if(arch_set.begin(), arch_set.end(), [&cpu_part](const ArchSet &a) { return a.part == cpu_part; });
  return item != arch_set.end() ? item->arch : UnKnown_Arch;
}

int ParseCpuPart(const char *line, int start, int size) {
  int cpu_part = 0;
  for (int i = start; i < size && i < start + 3; i++) {
    char c = line[i];
    int d;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if ((c - 'A') < 6) {
      d = 10 + (c - 'A');
    } else if ((c - 'a') < 6) {
      d = 10 + (c - 'a');
    } else {
      THREAD_ERROR("CPU part in /proc/cpuinfo is ignored due to unexpected non-hex character");
      break;
    }
    cpu_part = cpu_part * 16 + d;
  }
  return cpu_part;
}

int SetArch(std::vector<CpuInfo> *freq_set, int core_num) {
  if (core_num <= 0) {
    THREAD_ERROR("core_num must be greater than 0.");
    return THREAD_ERROR;
  }
  FILE *fp = fopen("/proc/cpuinfo", "r");
  if (fp == nullptr) {
    THREAD_ERROR("read /proc/cpuinfo error.");
    return THREAD_ERROR;
  }
  std::vector<Arch> archs;
  archs.resize(core_num);
  const int max_line_size = 1024;
  char line[max_line_size] = {0};
  int count = 0;
  while (!feof(fp)) {
    if (fgets(line, max_line_size, fp)) {
      // line start with "CPU part"
      if (0 == memcmp(line, "CPU part", 8)) {
        // get number like 0xD03
        for (int i = 0; i < max_line_size - 4; ++i) {
          if (line[i] == '0' && line[i + 1] == 'x') {
            int cpu_part = ParseCpuPart(line, i + 2, max_line_size);
            enum Arch arch = GetArch(cpu_part);
            if (arch == UnKnown_Arch) {
              THREAD_ERROR("cpu's architecture is unknown.");
              fclose(fp);
              return THREAD_ERROR;
            }
            count++;
            if (count > core_num) {
              THREAD_ERROR("number of cpu_part in /proc/cpuinfo is more than core_num.");
              fclose(fp);
              return THREAD_ERROR;
            }
            archs[count - 1] = arch;
          }
        }
      }
    }
  }
  if (count < core_num) {
    THREAD_ERROR("number of cpu_part in /proc/cpuinfo is less than core_num.");
    fclose(fp);
    return THREAD_ERROR;
  }
  for (int i = 0; i < core_num; ++i) {
    (*freq_set)[i].arch = archs[i];
  }
  fclose(fp);
  return THREAD_OK;
}

int GetMaxFrequency(int core_id) {
  FILE *fp;
  std::vector<std::string> paths = {"/sys/devices/system/cpu/cpufreq/stats/cpu",
                                    "/sys/devices/system/cpu/cpufreq/stats/cpu", "/sys/devices/system/cpu/cpu"};
  std::vector<std::string> files = {"/time_in_state", "/cpufreq/stats/time_in_state", "/cpufreq/cpuinfo_max_freq"};
  for (size_t i = 0; i < paths.size(); ++i) {
    std::string file = paths[i] + std::to_string(core_id) + files[i];
    fp = fopen(file.c_str(), "rb");
    if (fp != nullptr) {
      break;
    }
  }
  int max_freq = -1;
  if (fp == nullptr) {
    THREAD_ERROR("open system file failed");
    return max_freq;
  }
  while (feof(fp) == 0) {
    int freq = 0;
    int tmp = fscanf(fp, "%d", &freq);
    if (tmp != 1) {
      break;
    }
    if (freq > max_freq) {
      max_freq = freq;
    }
  }
  fclose(fp);
  return max_freq;
}

int CoreAffinity::SortCPUProcessors() {
  core_num_ = std::thread::hardware_concurrency();
  std::vector<CpuInfo> freq_set;
  freq_set.resize(core_num_);
  for (size_t i = 0; i < core_num_; ++i) {
    int max_freq = GetMaxFrequency(i);
    freq_set[i].core_id = i;
    freq_set[i].max_freq = max_freq;
    freq_set[i].arch = UnKnown_Arch;
  }
  int err_code = SetArch(&freq_set, core_num_);
  if (err_code != THREAD_OK) {
    THREAD_INFO("set arch failed, ignoring arch.");
  }
  // sort core id by frequency into descending order
  for (size_t i = 0; i < core_num_; ++i) {
    for (size_t j = i + 1; j < core_num_; ++j) {
      if (freq_set[i].max_freq < freq_set[j].max_freq ||
          (freq_set[i].max_freq == freq_set[j].max_freq && freq_set[i].arch <= freq_set[j].arch)) {
        CpuInfo temp = freq_set[i];
        freq_set[i] = freq_set[j];
        freq_set[j] = temp;
      }
    }
  }
  higher_num_ = 0;
  sorted_id_.clear();
  int max_freq = freq_set.front().max_freq;
  for (const auto &info : freq_set) {
    THREAD_INFO("sorted core id: %d, max frequency: %d, arch: %d", info.core_id, info.max_freq, info.arch);
    sorted_id_.push_back(info.core_id);
    higher_num_ += info.max_freq == max_freq ? 1 : 0;
  }
  return THREAD_OK;
}

int CoreAffinity::InitBindCoreId(size_t thread_num, BindMode bind_mode) {
  int ret = SortCPUProcessors();
  if (ret != THREAD_OK) {
    return THREAD_ERROR;
  }
  if (core_num_ != sorted_id_.size()) {
    THREAD_ERROR("init sorted core id failed");
    return THREAD_ERROR;
  }
  thread_num_ = thread_num;
  bind_id_.clear();
  if (bind_mode == Power_Higher || bind_mode == Power_NoBind) {
    for (size_t i = 0; i < thread_num_; ++i) {
      bind_id_.push_back(sorted_id_[i % core_num_]);
    }
  } else if (bind_mode == Power_Middle) {
    for (size_t i = 0; i < thread_num_; ++i) {
      bind_id_.push_back(sorted_id_[(i + higher_num_) % core_num_]);
    }
  } else {
    return THREAD_ERROR;
  }
  return THREAD_OK;
}

#ifdef BIND_CORE
int CoreAffinity::SetAffinity(const pthread_t &thread_id, cpu_set_t *cpu_set) const {
#ifdef __ANDROID__
#if __ANDROID_API__ >= 21
  THREAD_INFO("thread: %d, mask: %lu", pthread_gettid_np(thread_id), cpu_set->__bits[0]);
  int ret = sched_setaffinity(pthread_gettid_np(thread_id), sizeof(cpu_set_t), cpu_set);
  if (ret != THREAD_OK) {
    THREAD_ERROR("bind thread %d to cpu failed. ERROR %d", pthread_gettid_np(thread_id), ret);
    return THREAD_ERROR;
  }
#endif
#else
#if defined(__APPLE__)
  THREAD_ERROR("not bind thread to apple's cpu.");
  return THREAD_ERROR;
#else
  int ret = pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), cpu_set);
  if (ret != THREAD_OK) {
    THREAD_ERROR("set thread: %lu to cpu failed", thread_id);
    return THREAD_ERROR;
  }
#endif  // __APPLE__
#endif
  return THREAD_OK;
}
#endif  // BIND_CORE

int CoreAffinity::FreeScheduleThreads(const std::vector<Worker *> &workers) const {
#ifdef BIND_CORE
  if (thread_num_ != workers.size()) {
    return THREAD_ERROR;
  }
  cpu_set_t mask;
  CPU_ZERO(&mask);
  for (size_t i = 0; i < thread_num_; ++i) {
    CPU_SET(sorted_id_[i], &mask);
  }
  for (size_t i = 0; i < thread_num_; ++i) {
    int ret = SetAffinity(workers[i]->thread.native_handle(), &mask);
    if (ret != THREAD_OK) {
      THREAD_ERROR("set thread[%zu] affinity failed", i);
      return THREAD_ERROR;
    }
  }
#endif  // BIND_CORE
  return THREAD_OK;
}

int CoreAffinity::BindThreadsToCoreList(const std::vector<Worker *> &workers) const {
#ifdef BIND_CORE
  if (thread_num_ != workers.size()) {
    THREAD_ERROR("invalid core list");
    return THREAD_ERROR;
  }
  for (size_t i = 0; i < thread_num_; ++i) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(bind_id_[i], &mask);
    // affinity mask determines the CPU core which it is eligible to run
    int ret = SetAffinity(workers[i]->thread.native_handle(), &mask);
    if (ret != THREAD_OK) {
      THREAD_ERROR("set thread[%zu] affinity to core[%d] failed", i, bind_id_[i]);
      return THREAD_ERROR;
    }
    THREAD_ERROR("set thread[%zu] affinity to core[%d] success", i, bind_id_[i]);
  }
#endif  // BIND_CORE
  return THREAD_OK;
}

int CoreAffinity::BindProcess(BindMode bind_mode) const {
#ifdef BIND_CORE
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (bind_mode != Power_NoBind) {
    CPU_SET(bind_id_.front(), &mask);
  } else {
    for (int id : bind_id_) {
      CPU_SET(id, &mask);
    }
  }
  return SetAffinity(pthread_self(), &mask);
#else
  return THREAD_OK;
#endif  // BIND_CORE
}

int CoreAffinity::BindThreads(const std::vector<Worker *> &workers, BindMode bind_mode) const {
  if (bind_mode == Power_NoBind) {
    return FreeScheduleThreads(workers);
  } else {
    return BindThreadsToCoreList(workers);
  }
}

int CoreAffinity::BindThreads(const std::vector<Worker *> &workers, const std::vector<int> &core_list) {
  bind_id_ = core_list;
  return BindThreadsToCoreList(workers);
}
}  // namespace mindspore