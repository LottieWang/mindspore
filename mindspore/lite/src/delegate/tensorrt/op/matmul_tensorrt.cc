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

#include "src/delegate/tensorrt/op/matmul_tensorrt.h"
#include "src/delegate/tensorrt/tensorrt_utils.h"
namespace mindspore::lite {
constexpr int BIAS_INDEX = 2;

int MatMulTensorRT::IsSupport(const mindspore::schema::Primitive *primitive,
                              const std::vector<mindspore::MSTensor> &in_tensors,
                              const std::vector<mindspore::MSTensor> &out_tensors) {
  if (!IsShapeKnown()) {
    MS_LOG(ERROR) << "Unsupported input tensor unknown shape: " << op_name_;
    return RET_ERROR;
  }
  if (in_tensors.size() != 2 && in_tensors.size() != 3) {
    MS_LOG(ERROR) << "Unsupported input tensor size, size is " << in_tensors.size();
    return RET_ERROR;
  }
  if (out_tensors.size() != 1) {
    MS_LOG(ERROR) << "Unsupported output tensor size, size is " << out_tensors.size();
    return RET_ERROR;
  }
  return RET_OK;
}

int MatMulTensorRT::AddInnerOp(nvinfer1::INetworkDefinition *network) {
  auto primitive = this->GetPrimitive()->value_as_MatMul();
  transpose_a_ = primitive->transpose_a() ? nvinfer1::MatrixOperation::kTRANSPOSE : nvinfer1::MatrixOperation::kNONE;
  transpose_b_ = primitive->transpose_b() ? nvinfer1::MatrixOperation::kTRANSPOSE : nvinfer1::MatrixOperation::kNONE;
  auto weight = ConvertTensorWithExpandDims(network, in_tensors_[1], in_tensors_[0].Shape().size());

  auto matmul_layer = network->addMatrixMultiply(*tensorrt_in_tensors_[0], transpose_a_, *weight, transpose_b_);
  matmul_layer->setName(op_name_.c_str());
  nvinfer1::ITensor *out_tensor = matmul_layer->getOutput(0);

  if (in_tensors_.size() == BIAS_INDEX + 1) {
    auto bias = ConvertTensorWithExpandDims(network, in_tensors_[BIAS_INDEX], in_tensors_[0].Shape().size());
    auto bias_layer = network->addElementWise(*matmul_layer->getOutput(0), *bias, nvinfer1::ElementWiseOperation::kSUM);
    auto bias_layer_name = op_name_ + "_bias";
    bias_layer->setName(bias_layer_name.c_str());
    out_tensor = bias_layer->getOutput(0);
  }

  out_tensor->setName(out_tensors_[0].Name().c_str());
  this->AddInnerOutTensors(out_tensor);
  return RET_OK;
}
}  // namespace mindspore::lite
