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

#ifndef MINDSPORE_CCSRC_KERNEL_GPU_CUDA_IMPL_SPONGE_VATOM_V0_COORDINATE_REFRESH_IMPL_H_
#define MINDSPORE_CCSRC_KERNEL_GPU_CUDA_IMPL_SPONGE_VATOM_V0_COORDINATE_REFRESH_IMPL_H_

#include <curand_kernel.h>
#include "runtime/device/gpu/cuda_common.h"

void v0CoordinateRefresh(int atom_numbers, int virtual_numbers, const float *v_info_f, const int *uint_crd_f,
                         const float *scaler_f, float *crd_f, cudaStream_t stream);
#endif  // MINDSPORE_CCSRC_KERNEL_GPU_CUDA_IMPL_SPONGE_VATOM_V0_COORDINATE_REFRESH_IMPL_H_
