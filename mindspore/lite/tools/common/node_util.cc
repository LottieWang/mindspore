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

#include "tools/common/node_util.h"
#include <memory>
#include <set>
#include <vector>
#include "src/ops/populate/populate_register.h"
#include "src/common/common.h"
#include "src/common/log_adapter.h"
#include "tools/common/graph_util.h"
#include "tools/common/tensor_util.h"
#include "src/runtime/infer_manager.h"

namespace mindspore {
namespace lite {
constexpr size_t kInitialSize = 1024;
std::vector<CNodePtr> GetInputCNode(const CNodePtr &cnode) {
  if (cnode == nullptr) {
    return {};
  }
  std::vector<CNodePtr> inputs;
  for (const auto &input : cnode->inputs()) {
    if (input == nullptr || !utils::isa<CNodePtr>(input)) {
      continue;
    }
    inputs.emplace_back(utils::cast<CNodePtr>(input));
  }
  return inputs;
}

const schema::Primitive *ConvertToPrimitive(schema::PrimitiveT *primitive_t, flatbuffers::FlatBufferBuilder *fbb) {
  if (primitive_t == nullptr || fbb == nullptr) {
    MS_LOG(ERROR) << "primitiveT or fbb is nullptr.";
    return nullptr;
  }
  auto prim_offset = schema::CreatePrimitive(*fbb, primitive_t);
  fbb->Finish(prim_offset);
  auto prim_buf = fbb->GetBufferPointer();
  return flatbuffers::GetRoot<schema::Primitive>(prim_buf);
}

STATUS NodeUtils::ConvertDims(mindspore::schema::Format src_format, const std::vector<int32_t> &src_dims,
                              mindspore::schema::Format dst_format, std::vector<int32_t> *dst_dims) {
  MS_ASSERT(nullptr != dst_dims);
  if ((src_dims.size() != DIM_DEFAULT_SIZE && src_dims.size() != 3) || src_format == dst_format) {
    MS_LOG(ERROR) << "Convert format , src size " << src_dims.size()
                  << " <3 or src format is equal to dst format,not need convert";
    *dst_dims = src_dims;
    return RET_PARAM_INVALID;
  }

  std::vector<int32_t> nchw_dim;
  switch (src_format) {
    case schema::Format::Format_NCHW:
      nchw_dim = src_dims;
      break;
    case schema::Format::Format_NHWC:
      if (src_dims.size() == DIM_DEFAULT_SIZE) {
        nchw_dim.push_back(src_dims[NHWC_N]);
        nchw_dim.push_back(src_dims[NHWC_C]);
        nchw_dim.push_back(src_dims[NHWC_H]);
        nchw_dim.push_back(src_dims[NHWC_W]);
      } else {
        nchw_dim.push_back(src_dims[HWC_C]);
        nchw_dim.push_back(src_dims[HWC_H]);
        nchw_dim.push_back(src_dims[HWC_W]);
      }
      break;
    default:
      MS_LOG(ERROR) << "Not support src format: " << EnumNameFormat(src_format);
      return RET_ERROR;
  }

  if (nchw_dim.empty()) {
    MS_LOG(ERROR) << "Param nchw_dim is empty!";
    return RET_ERROR;
  }

  switch (dst_format) {
    case schema::Format::Format_NCHW:
      *dst_dims = nchw_dim;
      break;
    case schema::Format::Format_NHWC:
      if (src_dims.size() == DIM_DEFAULT_SIZE) {
        dst_dims->push_back(nchw_dim[NCHW_N]);
        dst_dims->push_back(nchw_dim[NCHW_H]);
        dst_dims->push_back(nchw_dim[NCHW_W]);
        dst_dims->push_back(nchw_dim[NCHW_C]);
      }
      break;
    default:
      MS_LOG(ERROR) << "Not support dst format: " << dst_format;
      return RET_ERROR;
  }
  return RET_OK;
}

static bool IsKCHWSource(kTransFilterType type) {
  return (type == kKCHW2HWCK || type == kKCHW2HWKC || type == kKCHW2KHWC || type == kKCHW2CKHW);
}

static bool IsCKHWSource(kTransFilterType type) {
  return (type == kCKHW2HWCK || type == kCKHW2HWKC || type == kCKHW2KHWC);
}

static bool IsHWCKSource(kTransFilterType type) { return (type == kHWCK2KCHW || type == kHWCK2CKHW); }

static bool IsHWKCSource(kTransFilterType type) { return (type == kHWKC2KCHW || type == kHWKC2CKHW); }

static bool IsNHWCSource(kTransFilterType type) {
  return (type == kNHWC2KCHW || type == kNHWC2HWCK || type == kNHWC2CKHW);
}

static bool IsCHWKSource(kTransFilterType type) { return (type == kCHWK2HWCK || type == kCHWK2KHWC); }

static bool IsKHWCSource(kTransFilterType type) { return (type == kKHWC2HWCK || type == kKHWC2CHWK); }

STATUS GetFilterDim(const std::vector<int32_t> &oriDims, kTransFilterType type, int32_t *filterK, int32_t *filterC,
                    int32_t *filterH, int32_t *filterW) {
  if (filterK == nullptr || filterC == nullptr || filterH == nullptr || filterW == nullptr) {
    MS_LOG(ERROR) << "null input";
    return RET_NULL_PTR;
  }
  MS_ASSERT(oriDims.size() == 4);
  if (IsKCHWSource(type)) {
    *filterK = oriDims.at(KCHW_K);
    *filterC = oriDims.at(KCHW_C);
    *filterH = oriDims.at(KCHW_H);
    *filterW = oriDims.at(KCHW_W);
  } else if (IsCKHWSource(type)) {
    *filterC = oriDims.at(CKHW_C);
    *filterK = oriDims.at(CKHW_K);
    *filterH = oriDims.at(CKHW_H);
    *filterW = oriDims.at(CKHW_W);
  } else if (IsHWCKSource(type)) {
    *filterH = oriDims.at(HWCK_H);
    *filterW = oriDims.at(HWCK_W);
    *filterC = oriDims.at(HWCK_C);
    *filterK = oriDims.at(HWCK_K);
  } else if (IsHWKCSource(type)) {
    *filterH = oriDims.at(HWKC_H);
    *filterW = oriDims.at(HWKC_W);
    *filterK = oriDims.at(HWKC_K);
    *filterC = oriDims.at(HWKC_C);
  } else if (IsNHWCSource(type)) {
    *filterK = oriDims.at(NHWC_N);
    *filterH = oriDims.at(NHWC_H);
    *filterW = oriDims.at(NHWC_W);
    *filterC = oriDims.at(NHWC_C);
  } else if (IsCHWKSource(type)) {
    *filterC = oriDims.at(CHWK_C);
    *filterH = oriDims.at(CHWK_H);
    *filterW = oriDims.at(CHWK_W);
    *filterK = oriDims.at(CHWK_K);
  } else if (IsKHWCSource(type)) {
    *filterK = oriDims.at(KHWC_K);
    *filterH = oriDims.at(KHWC_H);
    *filterW = oriDims.at(KHWC_W);
    *filterC = oriDims.at(KHWC_C);
  } else {
    MS_LOG(ERROR) << "Unsupported transFilterType: " << type;
    return RET_ERROR;
  }
  return RET_OK;
}

STATUS SetFilterDim(schema::TensorT *tensor, kTransFilterType type, int32_t filterK, int32_t filterC, int32_t filterH,
                    int32_t filterW) {
  MS_ASSERT(tensor != nullptr);
  if (type == kKCHW2HWCK || type == kCKHW2HWCK || type == kNHWC2HWCK || type == kKHWC2HWCK || type == kCHWK2HWCK) {
    tensor->dims = {filterH, filterW, filterC, filterK};
  } else if (type == kKCHW2HWKC || type == kCKHW2HWKC) {
    tensor->dims = {filterH, filterW, filterK, filterC};
  } else if (type == kHWCK2KCHW || type == kHWKC2KCHW || type == kNHWC2KCHW) {
    tensor->dims = {filterK, filterC, filterH, filterW};
  } else if (type == kHWCK2CKHW || type == kHWKC2CKHW || type == kNHWC2CKHW || type == kKCHW2CKHW) {
    tensor->dims = {filterC, filterK, filterH, filterW};
  } else if (type == kKHWC2CHWK) {
    tensor->dims = {filterC, filterH, filterW, filterK};
  } else if (type == kKCHW2KHWC || type == kCKHW2KHWC || type == kCHWK2KHWC) {
    tensor->dims = {filterK, filterH, filterW, filterC};
  } else {
    MS_LOG(ERROR) << "Unsupported transFilterType: " << type;
    return RET_ERROR;
  }
  return RET_OK;
}

static int Convert2KHWC(int srcFormat) {
  if (srcFormat == schema::Format::Format_KCHW) return kKCHW2KHWC;
  if (srcFormat == schema::Format::Format_CKHW) return kCKHW2KHWC;
  if (srcFormat == schema::Format::Format_CHWK) return kCHWK2KHWC;
  return -1;
}

static int Convert2HWCK(int srcFormat) {
  if (srcFormat == schema::Format::Format_KCHW) return kKCHW2HWCK;
  if (srcFormat == schema::Format::Format_KHWC) return kKHWC2HWCK;
  if (srcFormat == schema::Format::Format_CKHW) return kCKHW2HWCK;
  if (srcFormat == schema::Format::Format_CHWK) return kCHWK2HWCK;
  return -1;
}

static int Convert2KCHW(int srcFormat) {
  if (srcFormat == schema::Format::Format_HWCK) return kHWCK2KCHW;
  if (srcFormat == schema::Format::Format_HWKC) return kHWKC2KCHW;
  if (srcFormat == schema::Format::Format_KHWC) return kKHWC2KCHW;
  if (srcFormat == schema::Format::Format_CKHW) return kCKHW2KCHW;
  if (srcFormat == schema::Format::Format_CHWK) return kCHWK2KCHW;
  return -1;
}

static int Convert2CKHW(int srcFormat) {
  if (srcFormat == schema::Format::Format_HWCK) return kHWCK2CKHW;
  if (srcFormat == schema::Format::Format_HWKC) return kHWKC2CKHW;
  if (srcFormat == schema::Format::Format_KCHW) return kKCHW2CKHW;
  return -1;
}

STATUS NodeInferShpae(const schema::CNodeT &node, const std::vector<Tensor *> &inputs, std::vector<Tensor *> *outputs) {
  flatbuffers::FlatBufferBuilder fbb(kInitialSize);
  auto prim = ConvertToPrimitive(node.primitive.get(), &fbb);
  if (prim == nullptr) {
    MS_LOG(ERROR) << "get primitive failed.";
    fbb.Clear();
    return RET_ERROR;
  }
  auto parameter_gen = lite::PopulateRegistry::GetInstance()->GetParameterCreator(prim->value_type(), SCHEMA_CUR);
  if (parameter_gen == nullptr) {
    fbb.Clear();
    MS_LOG(ERROR) << "PopulateParameter return nullptr, type: " << schema::EnumNamePrimitiveType(prim->value_type());
    return RET_ERROR;
  }
  auto parameter = parameter_gen(prim);
  if (parameter == nullptr) {
    fbb.Clear();
    MS_LOG(ERROR) << "parameter is nullptr.";
    return RET_ERROR;
  }
  auto ret = KernelInferShape(inputs, *outputs, parameter);
  fbb.Clear();
  free(parameter);
  return ret;
}

size_t GetTensorInputIndexInCNode(const uint32_t &tensor_index, const schema::CNodeT &cnode) {
  size_t ret = -1;
  for (size_t i = 0; i < cnode.inputIndex.size(); i++) {
    if (cnode.inputIndex.at(i) == tensor_index) {
      ret = i;
    }
  }
  return ret;
}

STATUS TransFilterFormat(schema::TensorT *tensor, schema::Format dstFormat) {
  if (tensor == nullptr) {
    MS_LOG(ERROR) << "tensor is null";
    return RET_NULL_PTR;
  }
  std::vector<int32_t> oriDims = tensor->dims;
  if (oriDims.size() != (size_t)DIM_DEFAULT_SIZE) {
    MS_LOG(ERROR) << "Filter dim-num is not supported, dim-num: " << oriDims.size();
    return RET_ERROR;
  }
  auto srcFormat = tensor->format;
  auto dataType = tensor->dataType;
  STATUS status;
  int convert = -1;

  if (dstFormat == srcFormat) return RET_OK;

  switch (dstFormat) {
    case schema::Format::Format_KHWC:
      convert = Convert2KHWC(srcFormat);
      break;
    case schema::Format::Format_HWCK:
      convert = Convert2HWCK(srcFormat);
      break;
    case schema::Format::Format_KCHW:
      convert = Convert2KCHW(srcFormat);
      break;
    case schema::Format::Format_CKHW:
      convert = Convert2CKHW(srcFormat);
      break;
    default:
      convert = -1;
  }
  if (convert == -1) {
    MS_LOG(ERROR) << "Unsupported transform from " << EnumNameFormat(srcFormat) << " to " << EnumNameFormat(dstFormat);
    return RET_ERROR;
  }

  if (dataType == kNumberTypeFloat32) {
    status = TransFilterFormat<float>(tensor, static_cast<kTransFilterType>(convert));
  } else if (dataType == kNumberTypeUInt8) {
    status = TransFilterFormat<uint8_t>(tensor, static_cast<kTransFilterType>(convert));
  } else if (dataType == kNumberTypeInt8) {
    status = TransFilterFormat<int8_t>(tensor, static_cast<kTransFilterType>(convert));
  } else {
    MS_LOG(ERROR) << "Unsupported dataType: " << dataType;
    return RET_ERROR;
  }
  if (status != RET_OK) {
    MS_LOG(ERROR) << "TransFilterData failed: " << status;
    return status;
  }
  return RET_OK;
}

size_t GetCNodeOutputsSize(const std::shared_ptr<AnfNode> &anf_node, bool train_flag) {
  auto cnode = anf_node->cast<CNodePtr>();
  if (train_flag &&
      (opt::CheckPrimitiveType(cnode, prim::kPrimConv2DFusion) || opt::CheckPrimitiveType(cnode, prim::kPrimAdam))) {
    return 1;
  }
  if (utils::isa<abstract::AbstractTuple>(cnode->abstract())) {
    auto tuple = std::reinterpret_pointer_cast<abstract::AbstractTuple>(cnode->abstract());
    return tuple->elements().size();
  } else {
    return 1;
  }
}
}  // namespace lite
}  // namespace mindspore
