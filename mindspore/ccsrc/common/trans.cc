/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
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
#include "common/trans.h"
#include <functional>
#include <numeric>
#include <utility>
#include <algorithm>
#include "utils/ms_utils.h"
#include "abstract/utils.h"
#include "backend/kernel_compiler/kernel.h"
#include "backend/kernel_compiler/tbe/tbe_dynaminc_shape_util.h"
#include "runtime/device/convert_tensor_utils.h"
#include "utils/convert_utils.h"
#include "utils/log_adapter.h"
#include "utils/utils.h"

using mindspore::abstract::Shape;
namespace mindspore {
namespace trans {
const int b1 = 1;
const int b2 = 2;
const int b4 = 4;
const int b8 = 8;
inline void SetData(size_t size, bool pad_zero, size_t src_idx, size_t dst_idx, const FormatArgs &args, void *result) {
  switch (size) {
    case b1:
      static_cast<uint8_t *>(result)[dst_idx] = pad_zero ? 0 : static_cast<const uint8_t *>(args.data)[src_idx];
      break;
    case b2:
      static_cast<uint16_t *>(result)[dst_idx] = pad_zero ? 0 : static_cast<const uint16_t *>(args.data)[src_idx];
      break;
    case b4:
      static_cast<uint32_t *>(result)[dst_idx] = pad_zero ? 0 : static_cast<const uint32_t *>(args.data)[src_idx];
      break;
    case b8:
      static_cast<uint64_t *>(result)[dst_idx] = pad_zero ? 0 : static_cast<const uint64_t *>(args.data)[src_idx];
      break;
    default:
      MS_LOG(EXCEPTION) << "Trans data not support size " << size;
  }
}

// greatest common divsor
size_t Gcd(size_t a, size_t b) {
  if (b == 0) {
    return 0;
  }
  size_t c = b;
  while (a % b != 0) {
    c = a % b;
    a = b;
    b = c;
  }
  return c;
}

// least common  multiple
size_t Lcm(size_t a, size_t b) {
  if (b == 0) {
    return 0;
  }
  size_t ret = (a * b) / (Gcd(a, b));
  return ret;
}

template <typename T>
T DivCeil(T n1, T n2) {
  if (n2 != 0) {
    return (n1 + n2 - 1) / n2;
  }
  return 0;
}

size_t GetShapeSize(const std::vector<size_t> &shape) {
  return std::accumulate(shape.begin(), shape.end(), size_t(1), std::multiplies<size_t>());
}

enum DataTypeTransMode {
  FROM_FLOAT_TO_FLOAT16,
  FROM_FLOAT_TO_INT32,
  FROM_FLOAT16_TO_FLOAT,
  FROM_FLOAT16_TO_INT32,
  FROM_FLOAT16_TO_UINT8,
  FROM_INT32_TO_FLOAT,
  FROM_INT32_TO_FLOAT16,
  FROM_INT32_TO_UINT8,
  FROM_INT32_TO_INT8,
  FROM_INT32_TO_INT64,
  FROM_INT32_TO_BOOL,
  FROM_UINT8_TO_FLOAT,
  FROM_UINT8_TO_INT32,
  FROM_UINT8_TO_FLOAT16,
  FROM_INT8_TO_FLOAT,
  FROM_INT8_TO_FLOAT16,
  FROM_INT8_TO_INT32,
  FROM_INT64_TO_INT32,
  FROM_UINT16_TO_INT32,
  FROM_BOOL_TO_FLOAT,
  FROM_BOOL_TO_INT32,
  FROM_BOOL_TO_UINT8,
  FROM_BOOL_TO_FLOAT16,
  FROM_FLOAT64_TO_FLOAT32,
  FROM_FLOAT32_TO_FLOAT64
};

const std::map<std::pair<TypeId, TypeId>, DataTypeTransMode> mode_map{
  {std::pair<TypeId, TypeId>(kNumberTypeFloat64, kNumberTypeFloat32), FROM_FLOAT64_TO_FLOAT32},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat32, kNumberTypeFloat64), FROM_FLOAT32_TO_FLOAT64},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat32, kNumberTypeFloat16), FROM_FLOAT_TO_FLOAT16},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat32, kNumberTypeInt32), FROM_FLOAT_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat16, kNumberTypeFloat32), FROM_FLOAT16_TO_FLOAT},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat16, kNumberTypeInt32), FROM_FLOAT16_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeFloat16, kNumberTypeUInt8), FROM_FLOAT16_TO_UINT8},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeFloat32), FROM_INT32_TO_FLOAT},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeFloat16), FROM_INT32_TO_FLOAT16},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeUInt8), FROM_INT32_TO_UINT8},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeInt8), FROM_INT32_TO_INT8},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeInt64), FROM_INT32_TO_INT64},
  {std::pair<TypeId, TypeId>(kNumberTypeInt32, kNumberTypeBool), FROM_INT32_TO_BOOL},
  {std::pair<TypeId, TypeId>(kNumberTypeUInt8, kNumberTypeFloat32), FROM_UINT8_TO_FLOAT},
  {std::pair<TypeId, TypeId>(kNumberTypeUInt8, kNumberTypeInt32), FROM_UINT8_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeUInt8, kNumberTypeFloat16), FROM_UINT8_TO_FLOAT16},
  {std::pair<TypeId, TypeId>(kNumberTypeInt8, kNumberTypeFloat32), FROM_INT8_TO_FLOAT},
  {std::pair<TypeId, TypeId>(kNumberTypeInt8, kNumberTypeFloat16), FROM_INT8_TO_FLOAT16},
  {std::pair<TypeId, TypeId>(kNumberTypeInt8, kNumberTypeInt32), FROM_INT8_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeInt64, kNumberTypeInt32), FROM_INT64_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeUInt16, kNumberTypeInt32), FROM_UINT16_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeBool, kNumberTypeInt32), FROM_BOOL_TO_INT32},
  {std::pair<TypeId, TypeId>(kNumberTypeBool, kNumberTypeFloat), FROM_BOOL_TO_FLOAT},
  {std::pair<TypeId, TypeId>(kNumberTypeBool, kNumberTypeUInt8), FROM_BOOL_TO_UINT8},
  {std::pair<TypeId, TypeId>(kNumberTypeBool, kNumberTypeFloat16), FROM_BOOL_TO_FLOAT16}};

void CheckMemSize(const TypeIdArgs &args) {
  auto src_type_size = abstract::TypeIdSize(args.host_data_type);
  auto dst_type_size = abstract::TypeIdSize(args.device_data_type);
  if (src_type_size < 1 || dst_type_size < 1) {
    MS_LOG(EXCEPTION) << "Invalid src or dst data type.";
  }
  if (args.data_size / src_type_size != args.host_shape_size) {
    MS_LOG(EXCEPTION) << "Invalid src or dst data size.";
  }
}

template <typename SrcT, typename DstT>
void TransDataSrc2Dst(const TypeIdArgs &args, void *dst, const size_t data_size) {
  CheckMemSize(args);
  for (size_t idx = 0; idx != data_size; idx++) {
    SrcT src_data = static_cast<const SrcT *>(args.data)[idx];
    static_cast<DstT *>(dst)[idx] = static_cast<DstT>(src_data);
  }
}

template <typename SrcT>
void TransDataSrc2Fp16(const TypeIdArgs &args, void *dst, const size_t data_size) {
  CheckMemSize(args);
  auto src_data = static_cast<const SrcT *>(args.data);
  auto half_data = static_cast<float16 *>(dst);
  for (size_t i = 0; i < data_size; i++) {
    half_data[i] = float16(src_data[i]);
  }
}

bool CastKernel(const TypeIdArgs &args, void *dst, const size_t data_size, const DataTypeTransMode mode) {
  using DtypeKernel = std::function<void(const TypeIdArgs &, void *, const size_t)>;
  const std::map<DataTypeTransMode, DtypeKernel> cast_kernel_map{
    {FROM_FLOAT_TO_INT32, TransDataSrc2Dst<float, int32_t>},
    {FROM_FLOAT64_TO_FLOAT32, TransDataSrc2Dst<double, float>},
    {FROM_FLOAT32_TO_FLOAT64, TransDataSrc2Dst<float, double>},
    {FROM_FLOAT16_TO_INT32, TransDataSrc2Dst<float16, int32_t>},
    {FROM_FLOAT16_TO_UINT8, TransDataSrc2Dst<float16, uint8_t>},
    {FROM_INT32_TO_FLOAT, TransDataSrc2Dst<int32_t, float>},
    {FROM_INT32_TO_INT8, TransDataSrc2Dst<int32_t, int8_t>},
    {FROM_INT32_TO_INT64, TransDataSrc2Dst<int32_t, int64_t>},
    {FROM_INT32_TO_UINT8, TransDataSrc2Dst<int32_t, uint8_t>},
    {FROM_INT32_TO_BOOL, TransDataSrc2Dst<int32_t, int8_t>},
    {FROM_INT32_TO_FLOAT16, TransDataSrc2Fp16<int32_t>},
    {FROM_UINT8_TO_FLOAT, TransDataSrc2Dst<uint8_t, float>},
    {FROM_UINT8_TO_INT32, TransDataSrc2Dst<uint8_t, int32_t>},
    {FROM_UINT8_TO_FLOAT16, TransDataSrc2Fp16<uint8_t>},
    {FROM_INT8_TO_FLOAT, TransDataSrc2Dst<int8_t, float>},
    {FROM_INT8_TO_FLOAT16, TransDataSrc2Fp16<int8_t>},
    {FROM_INT8_TO_INT32, TransDataSrc2Dst<int8_t, int32_t>},
    {FROM_INT64_TO_INT32, TransDataSrc2Dst<int64_t, int32_t>},
    {FROM_UINT16_TO_INT32, TransDataSrc2Dst<uint16_t, int32_t>},
    {FROM_BOOL_TO_INT32, TransDataSrc2Dst<int8_t, int32_t>},
    {FROM_BOOL_TO_FLOAT, TransDataSrc2Dst<int8_t, float>},
    {FROM_BOOL_TO_UINT8, TransDataSrc2Dst<int8_t, uint8_t>},
    {FROM_BOOL_TO_FLOAT16, TransDataSrc2Fp16<int8_t>}};

  if (mode == FROM_FLOAT_TO_FLOAT16) {
    device::FloatToHalf(dst, args.data, data_size);
    return true;
  } else if (mode == FROM_FLOAT16_TO_FLOAT) {
    device::HalfToFloat(dst, args.data, data_size);
    return true;
  }
  auto iter = cast_kernel_map.find(mode);
  if (iter != cast_kernel_map.end()) {
    iter->second(args, dst, data_size);
    return true;
  } else {
    MS_LOG(ERROR) << "Unsupported datatype trans";
    return false;
  }
}

namespace {
bool HasShapeDynamic(const std::vector<int64_t> &shape_list) {
  return std::any_of(shape_list.begin(), shape_list.end(), [](int64_t shape) { return shape == Shape::SHP_ANY; });
}

template <typename T>
bool CheckDims(const std::vector<T> &shape) {
  if (shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Host shape dims should be 4";
    return false;
  }
  return true;
}

std::vector<size_t> NchwDeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  return shape;
}

std::vector<int64_t> NchwDeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  return shape;
}

std::vector<size_t> NhwcDeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Ccheck dims failed.";
  }
  std::vector<size_t> device_shape;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kC]);
  return device_shape;
}

std::vector<int64_t> NhwcDeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Ccheck dims failed.";
  }
  std::vector<int64_t> device_shape;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kC]);
  return device_shape;
}

std::vector<size_t> HwchDeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kC]);
  device_shape.push_back(shape[kN]);
  return device_shape;
}

std::vector<int64_t> HwchDeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kC]);
  device_shape.push_back(shape[kN]);
  return device_shape;
}

std::vector<size_t> FracZDeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  const size_t cout16 = ((shape[kN] + kCubeSize - 1) / kCubeSize) * kCubeSize;
  const size_t cin16 = ((shape[kC] + kCubeSize - 1) / kCubeSize) * kCubeSize;
  device_shape.push_back(shape[kH] * shape[kW] * cin16 / kCubeSize);
  device_shape.push_back(cout16 / kCubeSize);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> FracZDeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  if (HasShapeDynamic({shape[kC], shape[kH], shape[kW]})) {
    device_shape.push_back(Shape::SHP_ANY);
  } else {
    const int64_t cin16 = ((shape[kC] + kCubeSize - 1) / kCubeSize) * kCubeSize;
    device_shape.push_back(shape[kH] * shape[kW] * cin16 / kCubeSize);
  }
  if (shape[kN] == Shape::SHP_ANY) {
    device_shape.push_back(Shape::SHP_ANY);
  } else {
    const int64_t cout16 = ((shape[kN] + kCubeSize - 1) / kCubeSize) * kCubeSize;
    device_shape.push_back(cout16 / kCubeSize);
  }
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<size_t> Nc1hwc0DeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  const size_t C1 = (shape[kC] + kCubeSize - 1) / kCubeSize;
  const size_t C0 = kCubeSize;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<int64_t> Nc1hwc0DeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  const int64_t C1 = (shape[kC] == Shape::SHP_ANY) ? Shape::SHP_ANY : (shape[kC] + kCubeSize - 1) / kCubeSize;
  const int64_t C0 = kCubeSize;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<size_t> Ndc1hwc0DeviceShape(const std::vector<size_t> &shape) {
  // NCDHW
  if (shape.size() != kNcdhw) {
    MS_LOG(EXCEPTION) << "Check dims failed, expect shape dim 5, but got shape dim : " << shape.size();
  }
  std::vector<size_t> device_shape;
  const size_t C1 = (shape[1] + kCubeSize - 1) / kCubeSize;
  const size_t C0 = kCubeSize;
  device_shape.push_back(shape[N_ncdhw]);
  device_shape.push_back(shape[D_ncdhw]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[H_ncdhw]);
  device_shape.push_back(shape[W_ncdhw]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<int64_t> Ndc1hwc0DeviceDynamicShape(const std::vector<int64_t> &shape) {
  // NCDHW
  if (shape.size() != kNcdhw) {
    MS_LOG(EXCEPTION) << "Check dims failed, expect shape dim 5, but got shape dim : " << shape.size();
  }
  std::vector<int64_t> device_shape;
  const int64_t C1 = (shape[1] == Shape::SHP_ANY) ? Shape::SHP_ANY : (shape[1] + kCubeSize - 1) / kCubeSize;
  const int64_t C0 = kCubeSize;
  device_shape.push_back(shape[N_ncdhw]);
  device_shape.push_back(shape[D_ncdhw]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[H_ncdhw]);
  device_shape.push_back(shape[W_ncdhw]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<size_t> Fracz3DDeviceShape(const std::vector<size_t> &shape) {
  // NCDHW -> Frac_Z_3D
  if (shape.size() != kNcdhw) {
    MS_LOG(EXCEPTION) << "Check dims failed, expect shape dim 5, but got shape dim : " << shape.size();
  }
  std::vector<size_t> device_shape;
  const size_t C1 = (shape[1] + kCubeSize - 1) / kCubeSize;
  const size_t N1 = (shape[0] + kCubeSize - 1) / kCubeSize;
  device_shape.push_back(shape[D_ncdhw] * C1 * shape[H_ncdhw] * shape[W_ncdhw]);
  device_shape.push_back(N1);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> Fracz3DDeviceDynamicShape(const std::vector<int64_t> &shape) {
  // NCDHW -> Frac_Z_3D
  if (shape.size() != kNcdhw) {
    MS_LOG(EXCEPTION) << "Check dims failed, expect shape dim 5, but got shape dim : " << shape.size();
  }
  std::vector<int64_t> device_shape;
  if (HasShapeDynamic({shape[C_ncdhw], shape[D_ncdhw], shape[H_ncdhw], shape[W_ncdhw]})) {
    device_shape.push_back(Shape::SHP_ANY);
  } else {
    const int64_t C1 = (shape[1] + kCubeSize - 1) / kCubeSize;
    device_shape.push_back(shape[D_ncdhw] * C1 * shape[H_ncdhw] * shape[W_ncdhw]);
  }

  const int64_t N1 = (shape[0] == Shape::SHP_ANY) ? Shape::SHP_ANY : (shape[0] + kCubeSize - 1) / kCubeSize;
  device_shape.push_back(N1);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<size_t> C1hwncoc0DeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  device_shape.push_back((shape[kC] - 1) / kCubeSize + 1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kN]);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> C1hwncoc0DeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  shape[kC] == Shape::SHP_ANY ? device_shape.push_back(Shape::SHP_ANY)
                              : device_shape.push_back((shape[kC] - 1) / kCubeSize + 1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(shape[kN]);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<size_t> FracZc04DeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  const size_t c0 = 4;
  auto first_dim = DivCeil(c0 * shape[kH] * shape[kW], kCubeSize);
  auto no = DivCeil(shape.at(kN), kCubeSize);
  device_shape.push_back(first_dim);
  device_shape.push_back(no);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> FracZc04DeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  const int64_t c0 = 4;

  int64_t first_dim;
  if (HasShapeDynamic({shape[kH], shape[kW]})) {
    first_dim = Shape::SHP_ANY;
  } else {
    first_dim = DivCeil(c0 * shape[kH] * shape[kW], SizeToLong(kCubeSize));
  }
  auto shape_kN = shape.at(kN);
  int64_t no = (shape_kN == Shape::SHP_ANY) ? Shape::SHP_ANY : DivCeil(shape.at(kN), SizeToLong(kCubeSize));
  device_shape.push_back(first_dim);
  device_shape.push_back(no);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<size_t> Nc1hwc04DeviceShape(const std::vector<size_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<size_t> device_shape;
  const size_t C1 = 1;
  const size_t C0 = 4;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<int64_t> Nc1hwc04DeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  std::vector<int64_t> device_shape;
  const int64_t C1 = 1;
  const int64_t C0 = 4;
  device_shape.push_back(shape[kN]);
  device_shape.push_back(C1);
  device_shape.push_back(shape[kH]);
  device_shape.push_back(shape[kW]);
  device_shape.push_back(C0);
  return device_shape;
}

std::vector<size_t> NcdhwDeviceShape(const std::vector<size_t> &shape) {
  if (shape.size() < kNcdhw) {
    MS_LOG(EXCEPTION) << "Shape dims must be 5 when format is ndhwc.";
  }
  return shape;
}

std::vector<int64_t> NcdhwDeviceDynamicShape(const std::vector<int64_t> &shape) {
  if (shape.size() < kNcdhw) {
    MS_LOG(EXCEPTION) << "Shape dims must be 5 when format is ndhwc.";
  }
  return shape;
}

// change channel-first shape to channel-last shape.
// eg. [2,3,4] => [2,4,3]; [2,3,4,5] => [2,4,5,3]
std::vector<size_t> ChannelLastDeviceShape(const std::vector<size_t> &shape) {
  auto dim = shape.size();
  std::vector<int64_t> axis;
  axis.resize(dim);
  std::iota(axis.begin() + 1, axis.end(), 2);
  axis[dim - 1] = 1;

  std::vector<size_t> device_shape;
  std::transform(axis.begin(), axis.end(), std::back_inserter(device_shape), [&shape](int n) { return shape[n]; });

  return device_shape;
}

// change channel-first shape to channel-last shape.
// eg. [2,3,4] => [2,4,3]; [2,3,4,5] => [2,4,5,3]
std::vector<int64_t> ChannelLastDeviceDynamicShape(const std::vector<int64_t> &shape) {
  auto dim = shape.size();
  std::vector<int64_t> axis;
  axis.resize(dim);
  std::iota(axis.begin() + 1, axis.end(), 2);
  axis[dim - 1] = 1;

  std::vector<int64_t> device_shape;
  std::transform(axis.begin(), axis.end(), std::back_inserter(device_shape), [&shape](int n) { return shape[n]; });

  return device_shape;
}

std::vector<size_t> FracZDeviceShapeWithGroups(const std::vector<size_t> &shape, const int64_t groups = 1) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }
  size_t group_size = LongToSize(groups);
  size_t cin_ori = shape[kC];
  size_t cout_ori = shape[kN] / group_size;
  size_t e_mult = std::min(Lcm(Lcm(cin_ori, kCubeSize) / cin_ori, Lcm(cout_ori, kCubeSize) / cout_ori), group_size);
  size_t cin_opt = DivCeil(e_mult * cin_ori, kCubeSize) * kCubeSize;
  size_t c1_dim = cin_opt / kCubeSize;
  size_t g_dim = DivCeil(group_size, e_mult);
  size_t n1 = DivCeil(cout_ori * e_mult, kCubeSize);
  std::vector<size_t> device_shape;
  device_shape.push_back(g_dim * c1_dim * shape[kH] * shape[kW]);
  device_shape.push_back(n1);
  device_shape.push_back(kNiSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> FracZDeviceShapeWithGroups(const std::vector<int64_t> &shape, const int64_t groups = 1) {
  if (!CheckDims(shape)) {
    MS_LOG(EXCEPTION) << "Check dims failed.";
  }

  int64_t c1_dim = Shape::SHP_ANY;
  int64_t g_dim = Shape::SHP_ANY;
  int64_t n1 = Shape::SHP_ANY;
  if (!HasShapeDynamic({shape[kC], shape[kN]})) {
    size_t group_size = LongToSize(groups);
    size_t cin_ori_tmp = LongToSize(shape[kC]);
    size_t cout_ori_tmp = LongToSize(shape[kN]) / group_size;
    size_t e_mult =
      std::min(Lcm(Lcm(cin_ori_tmp, kCubeSize) / cin_ori_tmp, Lcm(cout_ori_tmp, kCubeSize) / cout_ori_tmp), group_size);
    int64_t cin_opt = DivCeil(e_mult * cin_ori_tmp, kCubeSize) * kCubeSize;
    c1_dim = cin_opt / kCubeSize;
    g_dim = DivCeil(group_size, e_mult);
    n1 = DivCeil(cout_ori_tmp * e_mult, kCubeSize);
  }

  std::vector<int64_t> device_shape;
  if (!HasShapeDynamic({shape[kC], shape[kN], shape[kH], shape[kW]})) {
    device_shape.push_back(g_dim * c1_dim * shape[kH] * shape[kW]);
  } else {
    device_shape.push_back(Shape::SHP_ANY);
  }
  device_shape.push_back(n1);
  device_shape.push_back(kNiSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> TransShapeToFracNZ(const std::vector<int64_t> &shape) {
  std::vector<int64_t> device_shape;
  if (shape.size() == 1 && (shape[0] == 1 || shape[0] % kCubeSize == 0)) {
    // For [1] and [1024] shape we can trait it as NZ shape
    return shape;
  }
  if (shape.size() < 2) {
    MS_LOG(EXCEPTION) << "Format FRACTAL_NZ is not support shape " << shape.size();
  } else {
    (void)std::copy(shape.begin(), shape.end() - 2, std::back_inserter(device_shape));
  }
  int64_t h_shape = shape[shape.size() - 2];
  int64_t w_shape = shape[shape.size() - 1];
  int64_t h1 = (h_shape == Shape::SHP_ANY) ? Shape::SHP_ANY : (h_shape - 1) / kCubeSize + 1;
  int64_t w1 = (w_shape == Shape::SHP_ANY) ? Shape::SHP_ANY : (w_shape - 1) / kCubeSize + 1;
  device_shape.push_back(w1);
  device_shape.push_back(h1);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}

std::vector<int64_t> TransShapeToFracNZLSTM(const std::vector<int64_t> &shape) {
  std::vector<int64_t> device_shape;
  const int64_t c0 = 4;
  const int64_t h_shape = shape.at(kN);
  const int64_t i_shape = shape.at(kC);
  const int64_t h = (h_shape == Shape::SHP_ANY) ? Shape::SHP_ANY : h_shape / c0;

  int64_t first = Shape::SHP_ANY;
  if (h_shape != Shape::SHP_ANY && i_shape != Shape::SHP_ANY) {
    int64_t i = i_shape - h;
    first = DivCeil(i, SizeToLong(kCubeSize)) + DivCeil(h, SizeToLong(kCubeSize));
  }
  const int64_t second = (h == Shape::SHP_ANY) ? Shape::SHP_ANY : c0 * DivCeil(h, SizeToLong(kCubeSize));
  device_shape.push_back(first);
  device_shape.push_back(second);
  device_shape.push_back(kCubeSize);
  device_shape.push_back(kCubeSize);
  return device_shape;
}
}  // namespace

int64_t GetAttrGroups(const AnfNodePtr &node, const size_t index) {
  if (node == nullptr) {
    return 1;
  }
  if (node->isa<CNode>()) {
    auto cnode = node->cast<CNodePtr>();
    if (AnfAlgo::HasNodeAttr(kAttrFracZGroup, cnode)) {
      if (AnfAlgo::GetCNodeName(cnode) == kAllReduceOpName) {
        // if index not exists in fracz_group_idx, return default value 1
        auto fz_group_idx = AnfAlgo::GetNodeAttr<std::vector<int64_t>>(cnode, kAttrFracZGroupIdx);
        int64_t out_index = SizeToLong(index);
        auto fz_iter = std::find(std::begin(fz_group_idx), std::end(fz_group_idx), out_index);
        if (fz_iter == std::end(fz_group_idx)) {
          return 1;
        }
      }
      return AnfAlgo::GetNodeAttr<int64_t>(cnode, kAttrFracZGroup);
    }
  } else if (node->isa<Parameter>()) {
    auto param = node->cast<ParameterPtr>();
    MS_EXCEPTION_IF_NULL(param);
    return param->fracz_group();
  }
  return 1;
}

bool IsNeedPadding(const std::string &format, const size_t shape_size) {
  if (shape_size == 0) {
    return false;
  }
  if (format == kOpFormat_DEFAULT || format == kOpFormat_FRAC_NZ || format == kOpFormat_ChannelLast ||
      format == kOpFormat_NCHW) {
    return false;
  } else if (shape_size < kNchwDims) {
    return true;
  }
  return false;
}

ShapeVector GetRuntimePaddingShape(const AnfNodePtr &node, size_t index) {
  MS_EXCEPTION_IF_NULL(node);
  ShapeVector shape;
  std::vector<size_t> host_shape;
  if (node->isa<ValueNode>()) {
    auto value_node = node->cast<ValueNodePtr>();
    MS_EXCEPTION_IF_NULL(value_node);
    auto node_value = value_node->value();
    MS_EXCEPTION_IF_NULL(node_value);
    auto tensor = node_value->cast<tensor::TensorPtr>();
    if (tensor == nullptr) {
      MS_LOG(EXCEPTION) << " The node[ " << node->DebugString() << "]'s cannot convert ";
    }
    auto shape_temp = tensor->shape();
    (void)std::transform(shape_temp.begin(), shape_temp.end(), std::back_inserter(host_shape), LongToSize);
    if (host_shape.empty()) {
      host_shape.push_back(1);
    }
  } else {
    host_shape = AnfAlgo::GetOutputInferShape(node, index);
  }
  auto format = AnfAlgo::GetOutputFormat(node, index);
  if (trans::IsNeedPadding(format, host_shape.size())) {
    host_shape = trans::PaddingShape(host_shape, format, AnfAlgo::GetOutputReshapeType(node, index));
  }
  std::transform(host_shape.begin(), host_shape.end(), std::back_inserter(shape), SizeToLong);
  return shape;
}

void StringToAxisVector4D(const std::string &reshape_type_str, std::vector<Axis> *reshape_type_vec) {
  MS_EXCEPTION_IF_NULL(reshape_type_vec);
  if (reshape_type_str.empty()) {
    MS_LOG(DEBUG) << "Reshape type str is empty, no need padding.";
    return;
  }
  for (const auto &c : reshape_type_str) {
    switch (c) {
      case 'N':
        reshape_type_vec->push_back(N);
        break;
      case 'C':
        reshape_type_vec->push_back(C);
        break;
      case 'H':
        reshape_type_vec->push_back(H);
        break;
      case 'W':
        reshape_type_vec->push_back(W);
        break;
      default:
        MS_LOG(EXCEPTION) << "Unknown axis " << c << "in reshape type.";
    }
  }
}
void StringToAxisVector5D(const std::string &reshape_type_str, std::vector<Axis5D> *reshape_type_vec) {
  MS_EXCEPTION_IF_NULL(reshape_type_vec);
  if (reshape_type_str.empty()) {
    MS_LOG(DEBUG) << "Reshape type str is empty, no need padding.";
    return;
  }
  for (const auto &c : reshape_type_str) {
    switch (c) {
      case 'N':
        reshape_type_vec->push_back(N_ncdhw);
        break;
      case 'C':
        reshape_type_vec->push_back(C_ncdhw);
        break;
      case 'D':
        reshape_type_vec->push_back(D_ncdhw);
        break;
      case 'H':
        reshape_type_vec->push_back(H_ncdhw);
        break;
      case 'W':
        reshape_type_vec->push_back(W_ncdhw);
        break;
      default:
        MS_LOG(EXCEPTION) << "Unknown axis " << c << "in reshape type.";
    }
  }
}

std::vector<size_t> TransShapeToDevice(const std::vector<size_t> &shape, const std::string &format,
                                       const int64_t groups) {
  using DeviceShapeTransfer = std::function<std::vector<size_t>(const std::vector<size_t> &)>;
  const std::map<std::string, DeviceShapeTransfer> device_shape_map{{kOpFormat_NCHW, NchwDeviceShape},
                                                                    {kOpFormat_NHWC, NhwcDeviceShape},
                                                                    {kOpFormat_HWCN, HwchDeviceShape},
                                                                    {kOpFormat_FRAC_Z, FracZDeviceShape},
                                                                    {kOpFormat_NC1HWC0, Nc1hwc0DeviceShape},
                                                                    {kOpFormat_C1HWNCoC0, C1hwncoc0DeviceShape},
                                                                    {kOpFormat_FRACTAL_Z_C04, FracZc04DeviceShape},
                                                                    {kOpFormat_NC1HWC0_C04, Nc1hwc04DeviceShape},
                                                                    {kOpFormat_NCDHW, NcdhwDeviceShape},
                                                                    {kOpFormat_ChannelLast, ChannelLastDeviceShape},
                                                                    {kOpFormat_NDC1HWC0, Ndc1hwc0DeviceShape},
                                                                    {kOpFormat_FRACTAL_Z_3D, Fracz3DDeviceShape}};

  if (format == kOpFormat_ND || format == kOpFormat_DEFAULT) {
    return shape;
  }
  if (groups > 1 && format == kOpFormat_FRAC_Z) {
    return FracZDeviceShapeWithGroups(shape, groups);
  }
  auto temp_shape = shape;
  std::vector<size_t> device_shape;
  if (format == kOpFormat_FRAC_NZ) {
    if (shape.size() == 1 && (shape[0] == 1 || shape[0] % kCubeSize == 0)) {
      // For [1] and [1024] shape we can trait it as NZ shape
      return shape;
    }
    if (shape.size() < 2) {
      MS_LOG(EXCEPTION) << "Format" << format << " is not support shape " << shape.size();
    } else {
      (void)std::copy(shape.begin(), shape.end() - 2, std::back_inserter(device_shape));
    }
    auto h1 = (shape[shape.size() - 2] - 1) / kCubeSize + 1;
    auto w1 = (shape[shape.size() - 1] - 1) / kCubeSize + 1;
    device_shape.push_back(w1);
    device_shape.push_back(h1);
    device_shape.push_back(kCubeSize);
    device_shape.push_back(kCubeSize);
    return device_shape;
  } else if (format == kOpFormat_FRACTAL_ZN_LSTM) {
    const size_t c0 = 4;
    const size_t h = shape.at(kN) / c0;
    const size_t i = shape.at(kC) - h;
    const size_t first = DivCeil(i, kCubeSize) + DivCeil(h, kCubeSize);
    const size_t second = c0 * DivCeil(h, kCubeSize);
    device_shape.push_back(first);
    device_shape.push_back(second);
    device_shape.push_back(kCubeSize);
    device_shape.push_back(kCubeSize);
    return device_shape;
  }
  if (format != kOpFormat_ChannelLast && shape.size() != kNchwDims && k3DFormatSet.find(format) == k3DFormatSet.end()) {
    MS_LOG(WARNING) << "Get Device Shape using a shape size is less than 4 ,should be Padding shape by Default firstly";
    temp_shape = PaddingShapeTo4dDefault(shape);
  }
  if (shape.size() != kNcdhw && k3DFormatSet.find(format) != k3DFormatSet.end()) {
    temp_shape = PaddingShapeTo5dDefault(shape);
  }
  auto iter = device_shape_map.find(format);
  if (iter == device_shape_map.end()) {
    MS_LOG(EXCEPTION) << "Unexpected format[" << format << "]";
  }
  return iter->second(temp_shape);
}

std::vector<int64_t> TransShapeToDevice(const std::vector<int64_t> &shape, const std::string &format,
                                        const int64_t groups) {
  using DeviceShapeTransfer = std::function<std::vector<int64_t>(const std::vector<int64_t> &)>;
  const std::map<std::string, DeviceShapeTransfer> device_shape_map{
    {kOpFormat_NCHW, NchwDeviceDynamicShape},
    {kOpFormat_NHWC, NhwcDeviceDynamicShape},
    {kOpFormat_HWCN, HwchDeviceDynamicShape},
    {kOpFormat_FRAC_Z, FracZDeviceDynamicShape},
    {kOpFormat_NC1HWC0, Nc1hwc0DeviceDynamicShape},
    {kOpFormat_C1HWNCoC0, C1hwncoc0DeviceDynamicShape},
    {kOpFormat_FRACTAL_Z_C04, FracZc04DeviceDynamicShape},
    {kOpFormat_NC1HWC0_C04, Nc1hwc04DeviceDynamicShape},
    {kOpFormat_NCDHW, NcdhwDeviceDynamicShape},
    {kOpFormat_ChannelLast, ChannelLastDeviceDynamicShape},
    {kOpFormat_NDC1HWC0, Ndc1hwc0DeviceDynamicShape},
    {kOpFormat_FRACTAL_Z_3D, Fracz3DDeviceDynamicShape}};

  if (format == kOpFormat_ND || format == kOpFormat_DEFAULT || format == kOpFormat_NCHW) {
    return shape;
  }
  if (groups > 1 && format == kOpFormat_FRAC_Z) {
    return FracZDeviceShapeWithGroups(shape, groups);
  }
  auto temp_shape = shape;
  if (format == kOpFormat_FRAC_NZ) {
    return TransShapeToFracNZ(shape);
  } else if (format == kOpFormat_FRACTAL_ZN_LSTM) {
    return TransShapeToFracNZLSTM(shape);
  }
  if (format != kOpFormat_ChannelLast && shape.size() != kNchwDims && k3DFormatSet.find(format) == k3DFormatSet.end()) {
    MS_LOG(WARNING) << "Get Device Shape using a shape size is less than 4 ,should be Padding shape by Default firstly";
    temp_shape = PaddingShapeTo4dDefault(shape);
  }
  if (shape.size() != kNcdhw && k3DFormatSet.find(format) != k3DFormatSet.end()) {
    temp_shape = PaddingShapeTo5dDefault(shape);
  }
  auto iter = device_shape_map.find(format);
  if (iter == device_shape_map.end()) {
    MS_LOG(EXCEPTION) << "Unexpected format[" << format << "]";
  }
  return iter->second(temp_shape);
}

bool CheckArgs(const FormatArgs &args, size_t *size, size_t *total_size) {
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  MS_EXCEPTION_IF_NULL(size);
  MS_EXCEPTION_IF_NULL(total_size);
  *size = abstract::TypeIdSize(args.src_data_type);
  if (*size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  *total_size = abstract::ShapeSize(args.device_shape) * (*size);
  if (*total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << *total_size << ", device_size:" << args.device_size;
    return false;
  }
  return true;
}

bool TransDataType(const TypeIdArgs &args, void *result) {
  MS_LOG(DEBUG) << "Begin trans datatype from " << TypeIdLabel(args.host_data_type) << " to "
                << TypeIdLabel(args.device_data_type);
  MS_EXCEPTION_IF_NULL(result);
  std::pair<TypeId, TypeId> type_info(args.host_data_type, args.device_data_type);
  auto iter = mode_map.find(type_info);
  if (iter == mode_map.end()) {
    MS_LOG(ERROR) << "Unsupported datatype trans. src_type :" << TypeIdLabel(args.host_data_type)
                  << ", dst_type:" << TypeIdLabel(args.device_data_type);
    return false;
  }
  auto trans_mode = iter->second;
  if (!CastKernel(args, result, args.host_shape_size, trans_mode)) {
    MS_LOG(ERROR) << "Failed to trans datatype..";
    return false;
  }
  return true;
}

bool TransFormat(const FormatArgs &args, void *result, int64_t groups) {
  MS_LOG(DEBUG) << "Start trans format.";
  if (abstract::TypeIdSize(args.src_data_type) < 1) {
    MS_LOG(ERROR) << "Invalid datatype..";
    return false;
  }
  if (args.device_format == kOpFormat_HWCN || args.device_format == kOpFormat_NHWC) {
    return NchwTo4D(args, result);
  }
  if (groups > 1 && args.device_format == kOpFormat_FRAC_Z) {
    return NchwToFracZWithGroups(args, result, groups);
  }
  auto iter = kTransFormatMapOfHostToDevice.find(args.device_format);
  if (iter == kTransFormatMapOfHostToDevice.end()) {
    MS_LOG(EXCEPTION) << "Unexpected format[" << args.device_format << "]";
  }
  return iter->second(args, result);
}

bool TransFormat(const FormatArgs &args, void *result, const AnfNodePtr &node, const size_t index) {
  int64_t groups = 1;
  if (args.device_format == kOpFormat_FRAC_Z) {
    groups = GetAttrGroups(node, index);
  }
  return TransFormat(args, result, groups);
}

bool TransFormatFromDeviceToHost(const FormatArgs &args, void *result, int64_t groups) {
  const std::map<std::string, FormatTransfer> format_trans_map{
    {kOpFormat_FRAC_Z, FracZToNchw},         {kOpFormat_FRAC_NZ, FracNzToNchw},
    {kOpFormat_NC1HWC0, Nc1hwc0ToNchw},      {kOpFormat_C1HWNCoC0, C1hwncoc0ToNchw},
    {kOpFormat_NC1HWC0_C04, Nc1hwc04ToNchw}, {kOpFormat_NDC1HWC0, Ndc1hwc0ToNcdhw},
    {kOpFormat_FRACTAL_Z_3D, FracZ3DToNcdhw}};

  MS_LOG(DEBUG) << "Start trans format.";
  if (abstract::TypeIdSize(args.src_data_type) < 1) {
    MS_LOG(ERROR) << "Invalid datatype..";
    return false;
  }
  if (args.device_format == kOpFormat_HWCN || args.device_format == kOpFormat_NHWC) {
    return ToNchw(args, result);
  }
  if (groups > 1 && args.device_format == kOpFormat_FRAC_Z) {
    return FracZToNchwWithGroups(args, result, groups);
  }
  auto iter = format_trans_map.find(args.device_format);
  if (iter == format_trans_map.end()) {
    MS_LOG(EXCEPTION) << "Unexpected format[" << args.device_format << "]";
  }
  return iter->second(args, result);
}

bool TransFormatFromDeviceToHost(const FormatArgs &args, void *result, const AnfNodePtr &node, const size_t index) {
  int64_t groups = 1;
  if (args.device_format == kOpFormat_FRAC_Z) {
    groups = GetAttrGroups(node, index);
  }
  return TransFormatFromDeviceToHost(args, result, groups);
}

bool NchwTo4D(const FormatArgs &args, void *result) {
  // trans nchw to 4d
  MS_LOG(DEBUG) << "Trans format from nchw to 4d.";
  MS_EXCEPTION_IF_NULL(result);
  size_t size = 0;
  size_t total_size = 0;
  if (!CheckArgs(args, &size, &total_size)) {
    MS_LOG(ERROR) << "Check args failed.";
    return false;
  }
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  for (size_t ni = 0; ni < n; ni++) {
    for (size_t ci = 0; ci < c; ci++) {
      for (size_t hi = 0; hi < h; hi++) {
        for (size_t wi = 0; wi < w; wi++) {
          auto src_idx = ni * c * h * w + ci * h * w + hi * w + wi;
          size_t dst_idx = 0;
          if (args.device_format == kOpFormat_NHWC) {
            dst_idx = ni * h * w * c + hi * w * c + wi * c + ci;
          } else if (args.device_format == kOpFormat_HWCN) {
            dst_idx = hi * w * c * n + wi * c * n + ci * n + ni;
          }
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool ToNchw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format to nchw from 4d.";
  MS_EXCEPTION_IF_NULL(result);
  size_t size = 0;
  size_t total_size = 0;
  if (!CheckArgs(args, &size, &total_size)) {
    MS_LOG(ERROR) << "Check args failed.";
    return false;
  }
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  for (size_t ni = 0; ni < n; ni++) {
    for (size_t ci = 0; ci < c; ci++) {
      for (size_t hi = 0; hi < h; hi++) {
        for (size_t wi = 0; wi < w; wi++) {
          auto dst_idx = ni * c * h * w + ci * h * w + hi * w + wi;
          size_t src_idx = 0;
          if (args.device_format == kOpFormat_NHWC) {
            src_idx = ni * h * w * c + hi * w * c + wi * c + ci;
          } else if (args.device_format == kOpFormat_HWCN) {
            src_idx = hi * w * c * n + wi * c * n + ci * n + ni;
          }
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool NchwToFracZ(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from nchw to frac_z";
  MS_EXCEPTION_IF_NULL(result);
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  const size_t c0 = 16;
  auto c1 = DivCeil(c, c0);
  auto hw = h * w;
  auto chw = c * hw;
  auto hwc0 = hw * c0;
  auto nchw = n * chw;

  auto hf_cnt = DivCeil(n, kCubeSize);
  auto vf_cnt = c1 * hw;
  auto fractal_ele_cnt = c0 * kCubeSize;
  auto total_ele_cnt = hf_cnt * vf_cnt * fractal_ele_cnt;
  auto dst_size = total_ele_cnt * size;
  if (dst_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size."
                  << "dst size is :" << dst_size << "device size is :" << args.device_size;
    return false;
  }

  for (size_t vfi = 0; vfi < vf_cnt; vfi++) {
    auto vf_base_i = vfi * hf_cnt;  // vertical fractal matrix base index
    for (size_t hfi = 0; hfi < hf_cnt; hfi++) {
      auto gfi = vf_base_i + hfi;  // global fractal matrix index
      auto src_n_offset = hfi * chw * kCubeSize;
      auto src_f_offset = src_n_offset + vfi % hw + vfi / hw * hwc0;
      for (size_t row = 0; row < c0; row++) {
        auto src_ci = vfi / hw * c0 + row;
        auto src_row_offset = src_f_offset + row * hw;
        for (size_t col = 0; col < kCubeSize; col++) {
          auto src_ni = hfi * kCubeSize + col;
          auto src_idx = src_row_offset + chw * col;
          auto dst_idx = gfi * fractal_ele_cnt + col * c0 + row;
          auto pad_zero = src_ni >= n || src_idx >= nchw || src_ci >= c;
          SetData(size, pad_zero, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool FracZToNchw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from frac_z to nchw";
  MS_EXCEPTION_IF_NULL(result);
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }

  auto n0 = args.device_shape.at(1);
  auto ni = args.device_shape.at(2);
  auto c0 = args.device_shape.at(3);
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  auto nc = ni * n0;
  auto ncc0 = nc * c0;
  auto wncc0 = w * ncc0;
  auto hwncc0 = h * wncc0;
  auto hw = h * w;
  auto chw = c * hw;

  for (size_t n_idx = 0; n_idx < n; n_idx++) {
    size_t n_head_addr = n_idx * chw;
    for (size_t c_idx = 0; c_idx < c; c_idx++) {
      size_t c_head_addr = n_head_addr + c_idx * hw;
      for (size_t h_idx = 0; h_idx < h; h_idx++) {
        size_t h_head_addr = c_head_addr + h_idx * w;
        for (size_t w_idx = 0; w_idx < w; w_idx++) {
          size_t dst_idx = h_head_addr + w_idx;
          size_t c1_idx = c_idx / c0;
          size_t c0_idx = c_idx % c0;
          size_t nc_idx = n_idx;
          size_t src_idx = c1_idx * hwncc0 + h_idx * wncc0 + w_idx * ncc0 + nc_idx * c0 + c0_idx;
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool NchwToFracZc04(const FormatArgs &args, void *result) {
  // trans nchw to FracZc04
  MS_LOG(DEBUG) << "Trans format from nchw to FracZc04.";
  MS_EXCEPTION_IF_NULL(result);
  size_t size = 0;
  size_t total_size = 0;
  if (!CheckArgs(args, &size, &total_size)) {
    MS_LOG(ERROR) << "Check args failed.";
    return false;
  }
  auto cube = kCubeSize;
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  const size_t c0 = 4;
  auto c1 = DivCeil(c, c0);
  auto hwc0 = h * w * c0;
  auto hwc = h * w * c;
  auto nhwc = n * h * w * c;
  auto n_cnt = DivCeil(n, cube);
  auto v_cnt = DivCeil(h * w * c0 * c1, cube);
  size_t dst_idx = 0;

  for (size_t vi = 0; vi < v_cnt; vi++) {
    for (size_t ni = 0; ni < n_cnt; ni++) {
      for (size_t col = 0; col < cube; col++) {
        for (size_t row = 0; row < cube; row++) {
          size_t cur_cube_n = cube * ni + col;
          size_t cur_cube_c1hwc0 = cube * vi + row;
          auto desc_g = cur_cube_n / n;
          auto desc_n = cur_cube_n % n;
          auto desc_c1 = cur_cube_c1hwc0 / hwc0;
          auto desc_c0 = cur_cube_c1hwc0 % c0;
          auto desc_h = (cur_cube_c1hwc0 - hwc0 * desc_c1) / (w * c0);
          auto desc_w = (cur_cube_c1hwc0 - hwc0 * desc_c1 - w * c0 * desc_h) / c0;
          auto c_idx = desc_c1 * c0 + desc_c0;
          auto src_idx = desc_g * nhwc + desc_n * hwc + c_idx * h * w + desc_h * w + desc_w;
          auto pad_zero = desc_g >= 1 || desc_n >= n || c_idx >= c;
          SetData(size, pad_zero, src_idx, dst_idx, args, result);
          dst_idx++;
        }
      }
    }
  }
  return true;
}

bool NchwToNc1hwc04(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from nchw to Nc1hwc04.";
  return NchwToNc1hwc0(args, result);
}
bool Nc1hwc04ToNchw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from Nc1hwc04 to nchw.";
  return Nc1hwc0ToNchw(args, result);
}

bool TransShapeToNz(const std::vector<size_t> &host_shape, std::vector<size_t> *hw_shape) {
  MS_EXCEPTION_IF_NULL(hw_shape);
  if (host_shape.empty()) {
    MS_LOG(ERROR) << "Size of vector is 0.";
    return false;
  }
  switch (host_shape.size()) {
    case 1:
      hw_shape->push_back(1);
      hw_shape->push_back(1);
      hw_shape->push_back(host_shape[0]);
      return true;
    default:
      auto size = host_shape.size();
      if (size < 2) {
        MS_LOG(ERROR) << "Illegal size.";
        return false;
      }
      size_t times = 1;
      for (size_t i = 0; i != size - 2; i++) {
        times *= host_shape[i];
      }
      hw_shape->push_back(times);
      hw_shape->push_back(host_shape[size - 2]);
      hw_shape->push_back(host_shape[size - 1]);
      return true;
  }
}

bool NchwToFracNz(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from nchw to frac_nz.";
  MS_EXCEPTION_IF_NULL(result);
  std::vector<size_t> hw_shape;
  if (!TransShapeToNz(args.host_shape, &hw_shape)) {
    MS_LOG(ERROR) << "Trans shape failed..";
    return false;
  }
  if (hw_shape.size() < 3 || args.device_shape.size() < 4) {
    MS_LOG(ERROR) << "Invalid shape size.";
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype";
    return false;
  }

  auto dst_size = abstract::ShapeSize(args.device_shape) * size;
  if (dst_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << dst_size << ", device_size:" << args.device_size;
    return false;
  }
  auto times = hw_shape.at(0);
  auto h = hw_shape.at(1);
  auto w = hw_shape.at(2);
  auto hw = h * w;

  auto shape_size = args.device_shape.size();
  auto w1 = args.device_shape[shape_size - 4];
  auto h1 = args.device_shape[shape_size - 3];
  auto h0 = args.device_shape[shape_size - 2];
  auto w0 = args.device_shape[shape_size - 1];
  auto h1h0w0 = h1 * h0 * w0;
  auto w1h1h0w0 = w1 * h1h0w0;
  auto num_w1 = w / w0;

  for (size_t times_idx = 0; times_idx < times; times_idx++) {
    auto times_head = times_idx * w1h1h0w0;
    auto src_times_head = times_idx * hw;
    for (size_t h1h0_idx = 0; h1h0_idx < h; h1h0_idx++) {
      auto h1h0_head = times_head + h1h0_idx * w0;
      auto src_h_head = src_times_head + h1h0_idx * w;
      for (size_t w1_idx = 0; w1_idx < num_w1; w1_idx++) {
        for (size_t i = 0; i < w0; ++i) {
          size_t src_idx = src_h_head + w1_idx * w0 + i;
          size_t dst_idx = h1h0_head + w1_idx * h1h0w0 + i;
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
      auto w1_head = num_w1 * w0;
      for (size_t w0_idx = 0; w1_head + w0_idx < w; w0_idx++) {
        auto src_w_idx = w1_head + w0_idx;
        size_t dst_idx = h1h0_head + num_w1 * h1h0w0 + w0_idx;
        size_t src_idx = src_h_head + src_w_idx;
        SetData(size, false, src_idx, dst_idx, args, result);
      }
    }
  }
  return true;
}

bool FracNzToNchw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from frac_nz to nchw";
  MS_EXCEPTION_IF_NULL(result);
  std::vector<size_t> hw_shape;
  if (!TransShapeToNz(args.host_shape, &hw_shape)) {
    MS_LOG(ERROR) << "Trans shape failed..";
    return false;
  }
  if (hw_shape.size() < 3 || args.device_shape.size() < 4) {
    MS_LOG(ERROR) << "Invalid shape size.";
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype";
    return false;
  }

  auto dst_size = abstract::ShapeSize(args.device_shape) * size;
  if (dst_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << dst_size << ", device_size:" << args.device_size;
    return false;
  }
  auto times = hw_shape.at(0);
  auto h = hw_shape.at(1);
  auto w = hw_shape.at(2);
  auto hw = h * w;

  auto shape_size = args.device_shape.size();
  auto w1 = args.device_shape[shape_size - 4];
  auto h1 = args.device_shape[shape_size - 3];
  auto h0 = args.device_shape[shape_size - 2];
  auto w0 = args.device_shape[shape_size - 1];
  auto h1h0w0 = h1 * h0 * w0;
  auto w1h1h0w0 = w1 * h1h0w0;
  auto num_w1 = w / w0;

  for (size_t times_idx = 0; times_idx < times; times_idx++) {
    auto times_head = times_idx * w1h1h0w0;
    auto src_times_head = times_idx * hw;
    for (size_t h1h0_idx = 0; h1h0_idx < h; h1h0_idx++) {
      auto h1h0_head = times_head + h1h0_idx * w0;
      auto src_h_head = src_times_head + h1h0_idx * w;
      for (size_t w1_idx = 0; w1_idx < num_w1; w1_idx++) {
        for (size_t i = 0; i < w0; ++i) {
          size_t src_idx = h1h0_head + w1_idx * h1h0w0 + i;
          size_t dst_idx = src_h_head + w1_idx * w0 + i;
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
      auto w1_head = num_w1 * w0;
      for (size_t w0_idx = 0; w1_head + w0_idx < w; w0_idx++) {
        auto src_w_idx = w1_head + w0_idx;
        size_t src_idx = h1h0_head + num_w1 * h1h0w0 + w0_idx;
        size_t dst_idx = src_h_head + src_w_idx;
        SetData(size, false, src_idx, dst_idx, args, result);
      }
    }
  }
  return true;
}

bool NchwToNc1hwc0(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from nchw to Nc1h1wc0";
  MS_EXCEPTION_IF_NULL(result);
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }

  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  size_t c0 = 16;
  if (args.device_format == kOpFormat_NC1HWC0_C04) {
    c0 = 4;
  }
  auto c1 = DivCeil(c, c0);
  auto hw = h * w;
  auto chw = c * hw;
  auto c1hwc0 = c1 * hw * c0;
  auto wc0 = w * c0;

  for (size_t n_idx = 0; n_idx < n; n_idx++) {
    size_t n_head_addr = n_idx * c1hwc0;
    for (size_t c1_idx = 0; c1_idx < c1; c1_idx++) {
      size_t c1_head_addr = n_head_addr + c1_idx * hw * c0;
      for (size_t h_idx = 0; h_idx < h; h_idx++) {
        size_t h_head_addr = c1_head_addr + h_idx * wc0;
        for (size_t w_idx = 0; w_idx < w; w_idx++) {
          size_t w_head_addr = h_head_addr + w_idx * c0;
          for (size_t c0_idx = 0; c0_idx < c0; c0_idx++) {
            size_t dst_idx = c0_idx + w_head_addr;
            size_t c_idx = c0_idx + c1_idx * c0;
            size_t src_idx = n_idx * chw + c_idx * hw + h_idx * w + w_idx;
            auto pad_zero = c_idx >= c;
            SetData(size, pad_zero, src_idx, dst_idx, args, result);
          }
        }
      }
    }
  }
  return true;
}

bool Nc1hwc0ToNchw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans format from nc1h1wc0 to nchw";
  MS_EXCEPTION_IF_NULL(result);
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }

  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  auto c1 = args.device_shape[1];
  auto c0 = args.device_shape[4];

  auto hw = h * w;
  auto chw = c * hw;
  auto wc0 = w * c0;
  auto hwc0 = h * wc0;
  auto c1hwc0 = c1 * hwc0;

  for (size_t n_idx = 0; n_idx < n; n_idx++) {
    size_t n_head_addr = n_idx * chw;
    for (size_t c_idx = 0; c_idx < c; c_idx++) {
      size_t c_head_addr = n_head_addr + c_idx * hw;
      for (size_t h_idx = 0; h_idx < h; h_idx++) {
        size_t h_head_addr = c_head_addr + h_idx * w;
        for (size_t w_idx = 0; w_idx < w; w_idx++) {
          size_t dst_idx = h_head_addr + w_idx;
          size_t c1_idx = c_idx / c0;
          size_t c0_idx = c_idx % c0;
          size_t src_idx = n_idx * c1hwc0 + c1_idx * hwc0 + h_idx * wc0 + w_idx * c0 + c0_idx;
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool NchwToC1hwncoc0(const FormatArgs &args, void *result) {
  // trans nchw to c1hwncoc0
  MS_LOG(DEBUG) << "Trans format from nchw to c1hwncoc0.";
  MS_EXCEPTION_IF_NULL(result);
  size_t size = 0;
  size_t total_size = 0;
  if (!CheckArgs(args, &size, &total_size)) {
    MS_LOG(ERROR) << "Check args failed.";
    return false;
  }
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  const int co_idx = 4;
  const int c0_idx = 5;
  auto c1 = args.device_shape[0];
  auto co = args.device_shape[co_idx];
  auto c0 = args.device_shape[c0_idx];

  for (size_t c1_i = 0; c1_i < c1; c1_i++) {
    for (size_t h_i = 0; h_i < h; h_i++) {
      for (size_t w_i = 0; w_i < w; w_i++) {
        for (size_t n_i = 0; n_i < n; n_i++) {
          for (size_t co_i = 0; co_i < co; co_i++) {
            for (size_t c0_i = 0; c0_i < c0; c0_i++) {
              size_t dst_idx = c1_i * h * w * n * co * c0 + h_i * w * n * co * c0 + w_i * n * co * c0 + n_i * co * c0 +
                               co_i * c0 + c0_i;
              size_t c_i = c0_i + c1_i * c0;
              size_t src_idx = n_i * c * h * w + c_i * h * w + h_i * w + w_i;
              auto pad_zero = !(c_i < c && c0_i == co_i);
              SetData(size, pad_zero, src_idx, dst_idx, args, result);
            }
          }
        }
      }
    }
  }
  return true;
}

bool C1hwncoc0ToNchw(const FormatArgs &args, void *result) {
  // trans c1hwncoc0 to nchw
  MS_LOG(DEBUG) << "Trans format from c1hwncoc0 to nchw";
  MS_EXCEPTION_IF_NULL(result);
  size_t size = 0;
  size_t total_size = 0;
  if (!CheckArgs(args, &size, &total_size)) {
    MS_LOG(ERROR) << "Check args failed.";
    return false;
  }
  auto n = args.host_shape[kN];
  auto c = args.host_shape[kC];
  auto h = args.host_shape[kH];
  auto w = args.host_shape[kW];
  const int co_idx = 4;
  const int c0_idx = 5;
  auto co = args.device_shape[co_idx];
  auto c0 = args.device_shape[c0_idx];
  for (size_t n_i = 0; n_i < n; n_i++) {
    for (size_t c_i = 0; c_i < c; c_i++) {
      for (size_t h_i = 0; h_i < h; h_i++) {
        for (size_t w_i = 0; w_i < w; w_i++) {
          size_t dst_idx = n_i * c * h * w + c_i * h * w + h_i * w + w_i;
          size_t c1_i = c_i / kCubeSize;
          size_t c0_i = c_i % kCubeSize;
          size_t co_i = c0_i;
          size_t src_idx =
            c1_i * h * w * n * co * c0 + h_i * w * n * co * c0 + w_i * n * co * c0 + n_i * co * c0 + co_i * c0 + c0_i;
          SetData(size, false, src_idx, dst_idx, args, result);
        }
      }
    }
  }
  return true;
}

bool Ndc1hwc0ToNcdhw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans from ndc1hwc0 to ncdhw";
  MS_EXCEPTION_IF_NULL(result);

  if (args.host_shape.size() != kNcdhw) {
    MS_LOG(ERROR) << "Illegal host shape dim, expect dim: 5, but got " << args.host_shape.size();
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }
  auto n = args.host_shape[N_ncdhw];
  auto c = args.host_shape[C_ncdhw];
  auto d = args.host_shape[D_ncdhw];
  auto h = args.host_shape[H_ncdhw];
  auto w = args.host_shape[W_ncdhw];
  auto c1 = args.device_shape[C1_ndc1hwc0];
  auto c0 = args.device_shape[C0_ndc1hwc0];
  const size_t cdhw = c * d * h * w;
  const size_t dhw = d * h * w;
  const size_t hw = h * w;
  const size_t dc1hwc0 = d * c1 * h * w * c0;
  const size_t c1hwc0 = c1 * h * w * c0;
  const size_t hwc0 = h * w * c0;
  const size_t wc0 = w * c0;

  for (size_t n_i = 0; n_i < n; n_i++) {
    size_t n_head = n_i * cdhw;
    for (size_t c_i = 0; c_i < c; c_i++) {
      size_t c_head = n_head + c_i * dhw;
      for (size_t d_i = 0; d_i < d; d_i++) {
        size_t d_head = c_head + d_i * hw;
        for (size_t h_i = 0; h_i < h; h_i++) {
          size_t h_head = d_head + h_i * w;
          for (size_t w_i = 0; w_i < w; w_i++) {
            size_t dst_i = h_head + w_i;
            size_t c1_i = c_i / c0;
            size_t c0_i = c_i % c0;
            auto src_idx = n_i * dc1hwc0 + d_i * c1hwc0 + c1_i * hwc0 + h_i * wc0 + w_i * c0 + c0_i;
            SetData(size, false, src_idx, dst_i, args, result);
          }
        }
      }
    }
  }
  return true;
}

bool NcdhwToNdc1hwc0(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans from ncdhw to ndc1hwc0";
  MS_EXCEPTION_IF_NULL(result);

  if (args.host_shape.size() != kNcdhw) {
    MS_LOG(ERROR) << "Illegal host shape dim, expect dim: 5, but got " << args.host_shape.size();
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }

  auto n = args.host_shape[N_ncdhw];
  auto c = args.host_shape[C_ncdhw];
  auto d = args.host_shape[D_ncdhw];
  auto h = args.host_shape[H_ncdhw];
  auto w = args.host_shape[W_ncdhw];
  auto c0 = kCubeSize;
  auto c1 = DivCeil(c, c0);
  const size_t cdhw = c * d * h * w;
  const size_t dhw = d * h * w;
  const size_t hw = h * w;
  const size_t dc1hwc0 = d * c1 * h * w * c0;
  const size_t c1hwc0 = c1 * h * w * c0;
  const size_t hwc0 = h * w * c0;
  const size_t wc0 = w * c0;

  for (size_t n_i = 0; n_i < n; n_i++) {
    size_t n_head = n_i * dc1hwc0;
    for (size_t d_i = 0; d_i < d; d_i++) {
      size_t d_head = n_head + d_i * c1hwc0;
      for (size_t c1_i = 0; c1_i < c1; c1_i++) {
        size_t c1_head = d_head + c1_i * hwc0;
        for (size_t h_i = 0; h_i < h; h_i++) {
          size_t h_head = c1_head + h_i * wc0;
          for (size_t w_i = 0; w_i < w; w_i++) {
            size_t w_head = h_head + w_i * c0;
            for (size_t c0_i = 0; c0_i < c0; c0_i++) {
              size_t dst_i = c0_i + w_head;
              size_t c_i = c0_i + c1_i * c0;
              size_t src_i = n_i * cdhw + c_i * dhw + d_i * hw + h_i * w + w_i;
              auto pad_zero = c_i >= c;
              SetData(size, pad_zero, src_i, dst_i, args, result);
            }
          }
        }
      }
    }
  }
  return true;
}

bool NcdhwToFracZ3D(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans from ncdhw to frac_z_3d";
  MS_EXCEPTION_IF_NULL(result);

  if (args.host_shape.size() != kNcdhw) {
    MS_LOG(ERROR) << "Illegal host shape dim, expect dim: 5, but got " << args.host_shape.size();
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }

  auto n = args.host_shape[N_ncdhw];
  auto c = args.host_shape[C_ncdhw];
  auto d = args.host_shape[D_ncdhw];
  auto h = args.host_shape[H_ncdhw];
  auto w = args.host_shape[W_ncdhw];

  auto n1n0 = DivCeil(n, kCubeSize) * kCubeSize;
  const size_t c0 = 16;
  auto c1 = DivCeil(c, c0);
  auto hw = h * w;
  auto dhw = d * hw;
  auto cdhw = c * dhw;
  auto n1n0c0 = n1n0 * c0;
  auto wn1n0c0 = w * n1n0c0;
  auto hwn1n0c0 = h * wn1n0c0;
  auto c1hwn1n0c0 = c1 * hwn1n0c0;

  for (size_t d_i = 0; d_i < d; d_i++) {
    for (size_t c1_i = 0; c1_i < c1; c1_i++) {
      for (size_t h_i = 0; h_i < h; h_i++) {
        for (size_t w_i = 0; w_i < w; w_i++) {
          for (size_t n1n0_i = 0; n1n0_i < n1n0; n1n0_i++) {
            for (size_t c0_i = 0; c0_i < c0; c0_i++) {
              auto dst_i = d_i * c1hwn1n0c0 + c1_i * hwn1n0c0 + h_i * wn1n0c0 + w_i * n1n0c0 + n1n0_i * c0 + c0_i;
              // ncdhw
              size_t src_i = n1n0_i * cdhw + (c1_i * c0 + c0_i) * dhw + d_i * hw + h_i * w + w_i;
              auto pad_zero = ((c1_i * c0 + c0_i) >= c) || (n1n0_i >= n);
              SetData(size, pad_zero, src_i, dst_i, args, result);
            }
          }
        }
      }
    }
  }
  return true;
}

bool FracZ3DToNcdhw(const FormatArgs &args, void *result) {
  MS_LOG(DEBUG) << "Trans from frac_z_3d to ncdhw";
  MS_EXCEPTION_IF_NULL(result);

  if (args.host_shape.size() != kNcdhw) {
    MS_LOG(ERROR) << "Illegal host shape dim, expect dim: 5, but got " << args.host_shape.size();
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype.";
    return false;
  }
  auto total_size = abstract::ShapeSize(args.device_shape) * size;
  if (total_size != args.device_size) {
    MS_LOG(ERROR) << "Illegal total data size, total_size:" << total_size << ", device_size:" << args.device_size;
    return false;
  }
  auto n = args.host_shape[N_ncdhw];
  auto c = args.host_shape[C_ncdhw];
  auto d = args.host_shape[D_ncdhw];
  auto h = args.host_shape[H_ncdhw];
  auto w = args.host_shape[W_ncdhw];
  const int kFZ3D_C0 = 3;
  auto c0 = args.device_shape[kFZ3D_C0];
  auto c1 = DivCeil(c, kCubeSize);
  auto n1n0 = DivCeil(n, kCubeSize) * kCubeSize;
  auto n1n0c0 = n1n0 * c0;
  auto wn1n0c0 = w * n1n0c0;
  auto hwn1n0c0 = h * wn1n0c0;
  auto c1hwn1n0c0 = c1 * hwn1n0c0;
  auto hw = h * w;
  auto dhw = d * hw;
  auto cdhw = c * dhw;

  for (size_t n_i = 0; n_i < n; n_i++) {
    size_t n_head = n_i * cdhw;
    for (size_t c_i = 0; c_i < c; c_i++) {
      size_t c_head = n_head + c_i * dhw;
      for (size_t d_i = 0; d_i < d; d_i++) {
        size_t d_head = c_head + d_i * hw;
        for (size_t h_i = 0; h_i < h; h_i++) {
          size_t h_head = d_head + h_i * w;
          for (size_t w_i = 0; w_i < w; w_i++) {
            size_t dst_i = h_head + w_i;
            size_t c1_i = c_i / c0;
            size_t c0_i = c_i % c0;
            size_t nc_i = n_i;
            size_t src_i = d_i * c1hwn1n0c0 + c1_i * hwn1n0c0 + h_i * wn1n0c0 + w_i * n1n0c0 + nc_i * c0 + c0_i;
            SetData(size, false, src_i, dst_i, args, result);
          }
        }
      }
    }
  }
  return true;
}

bool NchwFracZTransWithGroups(const FormatArgs &args, void *result, bool to_device, int64_t groups) {
  MS_EXCEPTION_IF_NULL(result);
  if (args.host_shape.size() != kNchwDims) {
    MS_LOG(ERROR) << "Invalid host shape, host shape dims:" << args.host_shape.size() << ", expect dims:" << kNchwDims;
    return false;
  }
  auto size = abstract::TypeIdSize(args.src_data_type);
  if (size < 1) {
    MS_LOG(ERROR) << "Illegal dtype";
    return false;
  }
  auto n_dim = args.host_shape[kN];
  auto c_dim = args.host_shape[kC];
  auto h_dim = args.host_shape[kH];
  auto w_dim = args.host_shape[kW];
  const size_t d_dim = 1;
  size_t group_size = LongToSize(groups);
  auto cin_ori = c_dim;
  auto cout_ori = n_dim / group_size;
  if (cin_ori == 0 || cout_ori == 0) {
    MS_LOG(ERROR) << "cin_ori, cout_ori must not equal to 0";
    return false;
  }
  size_t e_mult = std::min(Lcm(Lcm(cin_ori, kCubeSize) / cin_ori, Lcm(cout_ori, kCubeSize) / cout_ori), group_size);
  size_t cin_opt = DivCeil(e_mult * cin_ori, kCubeSize) * kCubeSize;
  size_t cout_opt = DivCeil(e_mult * cout_ori, kCubeSize) * kCubeSize;
  size_t c1_dim = cin_opt / kCubeSize;
  size_t dst_size = to_device ? GetShapeSize(args.device_shape) * size : GetShapeSize(args.host_shape) * size;
  if (dst_size == 0) {
    return true;
  }
  auto ret = memset_s(result, dst_size, 0, dst_size);
  if (ret != EOK) {
    MS_LOG(ERROR) << "memset failed";
    return false;
  }
  for (size_t g = 0; g < group_size; ++g) {
    for (size_t d = 0; d < d_dim; ++d) {
      for (size_t c = 0; c < c_dim; ++c) {
        for (size_t h = 0; h < h_dim; ++h) {
          for (size_t w = 0; w < w_dim; ++w) {
            for (size_t n = 0; n < cout_ori; ++n) {
              size_t e_val = g % e_mult;
              size_t dst_ci = e_val * cin_ori + c;
              size_t dst_co = e_val * cout_ori + n;
              size_t src_co = g * cout_ori + n;
              size_t temporary = dst_ci % kCubeSize;
              size_t dev_idx = (g / e_mult) * d_dim * c1_dim * h_dim * w_dim * cout_opt * kCubeSize +
                               d * c1_dim * h_dim * w_dim * cout_opt * kCubeSize +
                               (dst_ci / kCubeSize) * h_dim * w_dim * cout_opt * kCubeSize +
                               h * w_dim * cout_opt * kCubeSize + w * cout_opt * kCubeSize + dst_co * kCubeSize +
                               temporary;
              size_t hst_idx =
                src_co * c_dim * d_dim * h_dim * w_dim + c * d_dim * h_dim * w_dim + d * h_dim * w_dim + h * w_dim + w;
              if (to_device) {
                SetData(size, false, hst_idx, dev_idx, args, result);
              } else {
                SetData(size, false, dev_idx, hst_idx, args, result);
              }
            }
          }
        }
      }
    }
  }
  return true;
}

bool NchwToFracZWithGroups(const FormatArgs &args, void *result, int64_t groups) {
  MS_LOG(DEBUG) << "Trans format from nchw to frac_z with groups=" << groups;
  return NchwFracZTransWithGroups(args, result, true, groups);
}

bool FracZToNchwWithGroups(const FormatArgs &args, void *result, int64_t groups) {
  MS_LOG(DEBUG) << "Trans format from frac_z to nchw with groups=" << groups;
  return NchwFracZTransWithGroups(args, result, false, groups);
}
}  // namespace trans
}  // namespace mindspore
