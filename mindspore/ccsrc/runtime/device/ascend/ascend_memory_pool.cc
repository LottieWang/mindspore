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

#include <algorithm>
#include "runtime/device/ascend/ascend_memory_pool.h"
#include "runtime/mem.h"
#include "runtime/device/ascend/ascend_kernel_runtime.h"
#include "utils/log_adapter.h"

namespace mindspore {
namespace device {
namespace ascend {
// The minimum unit size (256MB) of memory block used for dynamic extend.
static const size_t ASCEND_DYNAMIC_MEM_ALLOC_UNIT_SIZE = 256 << 20;
// The minimum unit size (8MB) of memory block used for dynamic extend in graph mode.
static const size_t ASCEND_DYNAMIC_MEM_ALLOC_UNIT_SIZE_FOR_GRAPH = 8 << 20;

void AscendMemoryPool::Init(uint8_t *device_mem_base, uint64_t device_mem_size, uint64_t dynamic_mem_offset) {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  MS_EXCEPTION_IF_NULL(device_mem_base);
  set_device_mem_pool_base(device_mem_base);

  if (dynamic_mem_offset > device_mem_size) {
    MS_LOG(EXCEPTION) << "Dynamic memory offset: " << dynamic_mem_offset
                      << " exceed the device memory size: " << device_mem_size;
  }
  set_device_mem_size(device_mem_size);
  set_device_mem_pool_offset(device_mem_size);
  set_graph_dynamic_mem_offset(dynamic_mem_offset);
  initialized = true;
}

size_t AscendMemoryPool::CalMemBlockAllocSize(size_t size) {
  auto device_free_mem_size = free_mem_size();
  if (device_free_mem_size < size) {
    MS_LOG(EXCEPTION) << "Memory not enough: current free memory size[" << device_free_mem_size
                      << "] is smaller than required size[" << size << "], dynamic offset ["
                      << graph_dynamic_mem_offset_ << "] memory pool offset["
                      << device_mem_size_ - device_mem_pool_offset_ << "])";
    return 0;
  }
  auto alloc_mem_size = ASCEND_DYNAMIC_MEM_ALLOC_UNIT_SIZE;
  auto ms_context = MsContext::GetInstance();
  MS_EXCEPTION_IF_NULL(ms_context);
  const bool pynative_mode = (ms_context->get_param<int>(MS_CTX_EXECUTION_MODE) == kPynativeMode);
  if (pynative_mode) {
    // Growing at twice of alloc size
    constexpr size_t kDouble = 2;
    while (alloc_mem_size < size) {
      alloc_mem_size = alloc_mem_size * kDouble;
    }
  } else {
    alloc_mem_size = ASCEND_DYNAMIC_MEM_ALLOC_UNIT_SIZE_FOR_GRAPH;
    while (alloc_mem_size < size) {
      alloc_mem_size = alloc_mem_size + ASCEND_DYNAMIC_MEM_ALLOC_UNIT_SIZE_FOR_GRAPH;
    }
  }
  alloc_mem_size = std::min(alloc_mem_size, device_free_mem_size);
  return alloc_mem_size;
}

size_t AscendMemoryPool::AllocDeviceMem(size_t size, DeviceMemPtr *addr) {
  MS_LOG(INFO) << "Malloc Memory: Pool, total[" << device_mem_size_ << "] (dynamic[" << graph_dynamic_mem_offset_
               << "] memory pool[" << device_mem_size_ - device_mem_pool_offset_ << "])"
               << " malloc [" << size << "]";

  if (size == 0) {
    MS_LOG(EXCEPTION) << "Failed to alloc memory pool resource, the size is zero!";
  }

  if (device_mem_pool_offset_ - size < graph_dynamic_mem_offset_) {
    MS_LOG(EXCEPTION) << "Failed to alloc memory pool memory, the current device_mem_pool_offset_ ["
                      << device_mem_pool_offset_ << "], current graph_dynamic_mem_offset_ " << graph_dynamic_mem_offset_
                      << "], need memory size [" << size << "]";
  }
  device_mem_pool_offset_ -= size;
  *addr = device_mem_pool_base_ + device_mem_pool_offset_;
  if (*addr == nullptr) {
    MS_LOG(EXCEPTION) << "Alloc device memory pool address is nullptr, failed to alloc memory pool resource!";
  }
  return size;
}

bool AscendMemoryPool::FreeDeviceMem(const DeviceMemPtr &addr) {
  MS_EXCEPTION_IF_NULL(addr);
  return true;
}

void AscendMemoryPool::ResetIdleMemBuf() {
  auto idle_mem_buf_map = DynamicMemPoolBestFit::global_idle_mem_buf_map();
  for (auto &it : idle_mem_buf_map) {
    (void)rtMemset(it.second->device_addr_, it.first, 0, it.first);
  }
}

size_t AscendMemoryPool::AlignMemorySize(size_t size) const {
  if (size == 0) {
    MS_LOG(EXCEPTION) << "The align memory size is a zero !";
  }
  return size;
}

void AscendMemoryPool::set_device_mem_pool_base(uint8_t *device_mem_pool_base) {
  MS_EXCEPTION_IF_NULL(device_mem_pool_base);
  device_mem_pool_base_ = device_mem_pool_base;
}

void AscendMemoryPool::set_device_mem_size(uint64_t device_mem_size) { device_mem_size_ = device_mem_size; }

void AscendMemoryPool::set_device_mem_pool_offset(uint64_t device_mem_pool_offset) {
  device_mem_pool_offset_ = device_mem_pool_offset;
}

void AscendMemoryPool::set_graph_dynamic_mem_offset(uint64_t graph_dynamic_mem_offset) {
  graph_dynamic_mem_offset_ = graph_dynamic_mem_offset;
}

uint64_t AscendMemoryPool::device_mem_pool_offset() const { return device_mem_pool_offset_; }

size_t AscendMemoryPool::free_mem_size() {
  if (graph_dynamic_mem_offset_ >= device_mem_pool_offset_) {
    MS_LOG(EXCEPTION) << "graph dynamic mem offset [" << graph_dynamic_mem_offset_
                      << "] less than or equal to device mem pool offset [" << device_mem_pool_offset_ << "]!";
  }
  return device_mem_pool_offset_ - graph_dynamic_mem_offset_;
}

size_t AscendMemoryPool::total_mem_size() { return device_mem_size_ - graph_dynamic_mem_offset_; }
}  // namespace ascend
}  // namespace device
}  // namespace mindspore
