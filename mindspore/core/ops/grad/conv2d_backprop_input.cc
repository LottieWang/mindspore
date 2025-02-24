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

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <set>

#include "ops/grad/conv2d_backprop_input.h"

namespace mindspore {
namespace ops {
namespace {
void SetPadList(const PrimitivePtr &primitive, const std::vector<int64_t> &dout_shape_norm,
                const std::vector<int64_t> &x_size_v) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  // check
  auto kernel_size =
    CheckAndConvertUtils::CheckAttrIntOrTupleInt("kernel_size", primitive->GetAttr(kKernelSize), prim_name);
  auto stride = CheckAndConvertUtils::CheckAttrIntOrTupleInt("stride", primitive->GetAttr(kStride), prim_name);
  auto dilation = CheckAndConvertUtils::CheckAttrIntOrTupleInt("dilation", primitive->GetAttr(kDilation), prim_name);
  // default pad mode is valid
  auto attr_pad_list_prt = primitive->GetAttr(kPadList);
  int64_t pad_mode;
  (void)CheckAndConvertUtils::GetPadModEnumValue(primitive->GetAttr(kPadMode), &pad_mode, true);
  ShapeVector pad_list = {0, 0, 0, 0};
  if (!attr_pad_list_prt->isa<None>()) {
    pad_list = GetValue<ShapeVector>(attr_pad_list_prt);
  } else if (pad_mode == SAME) {
    auto stride_h = stride[2];
    auto stride_w = stride[3];
    auto kernel_h = kernel_size[0];
    auto kernel_w = kernel_size[1];
    auto dilation_h = dilation[2];
    auto dilation_w = dilation[3];
    auto pad_needed_h = (dout_shape_norm[2] - 1) * stride_h + dilation_h * (kernel_h - 1) + 1 - x_size_v[2];
    pad_needed_h = 0 > pad_needed_h ? 0 : pad_needed_h;
    auto pad_top = pad_needed_h / 2;
    auto pad_bottom = pad_needed_h - pad_top;
    auto pad_needed_w = (dout_shape_norm[3] - 1) * stride_w + dilation_w * (kernel_w - 1) + 1 - x_size_v[3];
    pad_needed_w = pad_needed_w > 0L ? pad_needed_w : 0L;
    auto pad_left = pad_needed_w / 2;
    auto pad_right = pad_needed_w - pad_left;
    pad_list = {pad_top, pad_bottom, pad_left, pad_right};
  } else if (pad_mode == PAD) {
    pad_list = GetValue<std::vector<int64_t>>(primitive->GetAttr(kPad));
  }
  (void)primitive->AddAttr(kPadList, MakeValue(pad_list));
}

abstract::ShapePtr Conv2DBackpropInputInferShape(const PrimitivePtr &primitive,
                                                 const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  auto x_size_v = input_args[2]->BuildValue();
  auto ret_shape = CheckAndConvertUtils::CheckAttrIntOrTupleInt("x_size", x_size_v, prim_name);
  auto dout_shape = CheckAndConvertUtils::ConvertShapePtrToShapeMap(input_args[0]->BuildShape())[kShape];
  auto format = CheckAndConvertUtils::GetAndCheckFormat(primitive->GetAttr(kFormat));
  ShapeVector tmp_shape = {dout_shape[0], dout_shape[2], dout_shape[3], dout_shape[1]};
  auto dout_shape_norm = format == Format::NCHW ? dout_shape : tmp_shape;
  SetPadList(primitive, dout_shape_norm, ret_shape);
  return std::make_shared<abstract::Shape>(ret_shape);
}

TypePtr Conv2DBackpropInputInferType(const PrimitivePtr &prim, const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(prim);
  auto prim_name = prim->name();
  // check
  std::map<std::string, TypePtr> types;
  types.emplace("doutput", input_args[0]->BuildType());
  types.emplace("w", input_args[1]->BuildType());
  std::set<TypePtr> valid_x_type = {kInt8, kInt32, kFloat16, kFloat32};
  return CheckAndConvertUtils::CheckTensorTypeSame(types, valid_x_type, prim_name);
}
}  // namespace
AbstractBasePtr Conv2DBackpropInputInfer(const abstract::AnalysisEnginePtr &, const PrimitivePtr &primitive,
                                         const std::vector<AbstractBasePtr> &input_args) {
  MS_EXCEPTION_IF_NULL(primitive);
  auto prim_name = primitive->name();
  // check
  (void)CheckAndConvertUtils::CheckInteger("input size", input_args.size(), kGreaterEqual, 3, prim_name);
  for (const auto &item : input_args) {
    MS_EXCEPTION_IF_NULL(item);
  }
  auto abs = std::make_shared<abstract::AbstractTensor>(Conv2DBackpropInputInferType(primitive, input_args),
                                                        Conv2DBackpropInputInferShape(primitive, input_args));
  return abs;
}

void Conv2DBackpropInput::Init(int64_t out_channel, const std::vector<int64_t> &kernel_size, int64_t mode,
                               const PadMode &pad_mode, const std::vector<int64_t> &pad,
                               const std::vector<int64_t> &stride, const std::vector<int64_t> &dilation, int64_t group,
                               const Format &format, const std::vector<int64_t> &pad_list) {
  set_out_channel(out_channel);
  set_kernel_size(kernel_size);
  set_mode(mode);
  set_pad_mode(pad_mode);
  set_pad(pad);
  set_stride(stride);
  set_dilation(dilation);
  set_group(group);
  set_format(format);
  set_pad_list(pad_list);
}

void Conv2DBackpropInput::set_out_channel(int64_t out_channel) {
  (void)AddAttr(kOutChannel,
                MakeValue(CheckAndConvertUtils::CheckInteger(kOutChannel, out_channel, kGreaterThan, 0, name())));
}

void Conv2DBackpropInput::set_kernel_size(const std::vector<int64_t> &kernel_size) {
  (void)AddAttr(kKernelSize, MakeValue(CheckAndConvertUtils::CheckPositiveVector(kKernelSize, kernel_size, name())));
}

void Conv2DBackpropInput::set_stride(const std::vector<int64_t> &stride) {
  (void)AddAttr(kStride, MakeValue(CheckAndConvertUtils::CheckPositiveVector(kStride, stride, name())));
}

void Conv2DBackpropInput::set_dilation(const std::vector<int64_t> &dilation) {
  (void)AddAttr(kDilation, MakeValue(CheckAndConvertUtils::CheckPositiveVector(kDilation, dilation, name())));
}

void Conv2DBackpropInput::set_pad_mode(const PadMode &pad_mode) {
  std::vector<int64_t> pad = get_pad();
  if (pad_mode == PAD) {
    for (auto item : pad) {
      (void)CheckAndConvertUtils::Check(kPadItem, item, kGreaterEqual, "zeros_list", 0, name());
    }
  } else {
    (void)CheckAndConvertUtils::Check(kPad, pad, kEqual, "zeros_list", {0, 0, 0, 0}, name());
  }
  int64_t swi = pad_mode;
  (void)AddAttr(kPadMode, MakeValue(swi));
}

void Conv2DBackpropInput::set_pad(const std::vector<int64_t> &pad) {
  (void)CheckAndConvertUtils::CheckInteger("pad_size", SizeToLong(pad.size()), kEqual, 4, name());
  (void)AddAttr(kPad, MakeValue(CheckAndConvertUtils::CheckPositiveVector(kPad, pad, name())));
}

void Conv2DBackpropInput::set_mode(int64_t mode) {
  (void)AddAttr(kMode, MakeValue(CheckAndConvertUtils::CheckInteger(kMode, mode, kEqual, 1, name())));
}

void Conv2DBackpropInput::set_group(int64_t group) {
  (void)AddAttr(kGroup, MakeValue(CheckAndConvertUtils::CheckInteger(kGroup, group, kGreaterThan, 0, name())));
}

void Conv2DBackpropInput::set_format(const Format &format) {
  int64_t f = format;
  (void)AddAttr(kFormat, MakeValue(f));
}

void Conv2DBackpropInput::set_pad_list(const std::vector<int64_t> &pad_list) {
  (void)this->AddAttr(kPadList, MakeValue(pad_list));
}

int64_t Conv2DBackpropInput::get_out_channel() const {
  auto value_ptr = GetAttr(kOutChannel);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<int64_t>(value_ptr);
}

std::vector<int64_t> Conv2DBackpropInput::get_kernel_size() const {
  auto value_ptr = GetAttr(kKernelSize);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<std::vector<int64_t>>(value_ptr);
}

std::vector<int64_t> Conv2DBackpropInput::get_stride() const {
  auto value_ptr = GetAttr(kStride);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<std::vector<int64_t>>(value_ptr);
}

std::vector<int64_t> Conv2DBackpropInput::get_dilation() const {
  auto value_ptr = GetAttr(kDilation);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<std::vector<int64_t>>(value_ptr);
}

PadMode Conv2DBackpropInput::get_pad_mode() const {
  auto value_ptr = GetAttr(kPadMode);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return PadMode(GetValue<int64_t>(value_ptr));
}

std::vector<int64_t> Conv2DBackpropInput::get_pad() const {
  auto value_ptr = GetAttr(kPad);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<std::vector<int64_t>>(value_ptr);
}

int64_t Conv2DBackpropInput::get_mode() const {
  auto value_ptr = GetAttr(kMode);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<int64_t>(value_ptr);
}

int64_t Conv2DBackpropInput::get_group() const {
  auto value_ptr = GetAttr(kGroup);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<int64_t>(value_ptr);
}

Format Conv2DBackpropInput::get_format() const {
  auto value_ptr = GetAttr(kFormat);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return Format(GetValue<int64_t>(value_ptr));
}

std::vector<int64_t> Conv2DBackpropInput::get_pad_list() const {
  auto value_ptr = GetAttr(kPadList);
  MS_EXCEPTION_IF_NULL(value_ptr);
  return GetValue<std::vector<int64_t>>(value_ptr);
}
REGISTER_PRIMITIVE_EVAL_IMPL(Conv2DBackpropInput, prim::kPrimConv2DBackpropInput, Conv2DBackpropInputInfer, nullptr,
                             true);
}  // namespace ops
}  // namespace mindspore
