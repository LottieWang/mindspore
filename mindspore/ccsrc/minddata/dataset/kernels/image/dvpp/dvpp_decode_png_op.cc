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

#include <string>
#include <vector>
#include <iostream>
#include "include/api/context.h"
#include "minddata/dataset/core/cv_tensor.h"
#include "minddata/dataset/core/data_type.h"
#include "minddata/dataset/kernels/image/dvpp/dvpp_decode_resize_crop_jpeg_op.h"
#include "minddata/dataset/kernels/image/dvpp/dvpp_decode_png_op.h"
#include "minddata/dataset/kernels/image/dvpp/utils/CommonDataType.h"
#include "minddata/dataset/kernels/image/dvpp/utils/MDAclProcess.h"
#include "minddata/dataset/kernels/image/image_utils.h"

namespace mindspore {
namespace dataset {
Status DvppDecodePngOp::Compute(const std::shared_ptr<DeviceTensor> &input, std::shared_ptr<DeviceTensor> *output) {
  IO_CHECK(input, output);
  try {
    CHECK_FAIL_RETURN_UNEXPECTED(input->GetDeviceBuffer() != nullptr, "The input image buffer on device is empty");
    APP_ERROR ret = processor_->PNG_D();
    if (ret != APP_ERR_OK) {
      processor_->Release();
      std::string error = "Error in dvpp processing:" + std::to_string(ret);
      RETURN_STATUS_UNEXPECTED(error);
    }
    std::shared_ptr<DvppDataInfo> DecodeOut(processor_->Get_Decode_DeviceData());
    const TensorShape dvpp_shape({1, 1, 1});
    const DataType dvpp_data_type(DataType::DE_UINT8);
    mindspore::dataset::DeviceTensor::CreateEmpty(dvpp_shape, dvpp_data_type, output);
    (*output)->SetAttributes(DecodeOut->data, DecodeOut->dataSize, DecodeOut->width, DecodeOut->widthStride,
                             DecodeOut->height, DecodeOut->heightStride);
    if (!((*output)->HasDeviceData())) {
      std::string error = "[ERROR] Fail to get the Output result from memory!";
      RETURN_STATUS_UNEXPECTED(error);
    }
  } catch (const cv::Exception &e) {
    std::string error = "[ERROR] Fail in DvppDecodeJpegOp:" + std::string(e.what());
    RETURN_STATUS_UNEXPECTED(error);
  }
  return Status::OK();
}

Status DvppDecodePngOp::Compute(const std::shared_ptr<Tensor> &input, std::shared_ptr<Tensor> *output) {
  IO_CHECK(input, output);
  if (!IsNonEmptyPNG(input)) {
    RETURN_STATUS_UNEXPECTED("DvppDecodePngOp only support process PNG image.");
  }
  try {
    CHECK_FAIL_RETURN_UNEXPECTED(input->GetBuffer() != nullptr, "The input image buffer is empty.");
    unsigned char *buffer = const_cast<unsigned char *>(input->GetBuffer());
    RawData imageInfo;
    uint32_t filesize = input->SizeInBytes();
    imageInfo.lenOfByte = filesize;
    imageInfo.data = static_cast<void *>(buffer);
    ResourceInfo resource;
    resource.aclConfigPath = "";
    resource.deviceIds.insert(0);
    std::shared_ptr<ResourceManager> instance = ResourceManager::GetInstance();
    APP_ERROR ret = instance->InitResource(resource);
    if (ret != APP_ERR_OK) {
      instance->Release();
      std::string error = "Error in Init D-chip:" + std::to_string(ret);
      RETURN_STATUS_UNEXPECTED(error);
    }
    int deviceId = *(resource.deviceIds.begin());
    aclrtContext context = instance->GetContext(deviceId);
    // Second part end where we initialize the resource of D-chip and set up all configures
    MDAclProcess process(context, false);
    ret = process.InitResource();
    if (ret != APP_ERR_OK) {
      instance->Release();
      std::string error = "Error in Init resource:" + std::to_string(ret);
      RETURN_STATUS_UNEXPECTED(error);
    }

    ret = process.PNG_D(imageInfo);
    if (ret != APP_ERR_OK) {
      instance->Release();
      std::string error = "Error in dvpp processing:" + std::to_string(ret);
      RETURN_STATUS_UNEXPECTED(error);
    }

    // Third part end where we execute the core function of dvpp
    /* 测试Device内存
     */
    auto data = std::static_pointer_cast<unsigned char>(process.Get_Memory_Data());
    unsigned char *ret_ptr = data.get();
    std::shared_ptr<DvppDataInfo> DecodeOut(process.Get_Decode_DeviceData());
    dsize_t dvpp_length = DecodeOut->dataSize;

    const TensorShape dvpp_shape({dvpp_length, 1, 1});
    const DataType dvpp_data_type(DataType::DE_UINT8);
    mindspore::dataset::Tensor::CreateFromMemory(dvpp_shape, dvpp_data_type, ret_ptr, output);
    if (!((*output)->HasData())) {
      std::string error = "[ERROR] Fail to get the Output result from memory!";
      RETURN_STATUS_UNEXPECTED(error);
    }
    process.device_memory_release();
    process.Release();
    // Last part end where we transform the processed data into a tensor which can be applied in later units.
  } catch (const cv::Exception &e) {
    std::string error = "[ERROR] Fail in DvppDecodePngOp:" + std::string(e.what());
    RETURN_STATUS_UNEXPECTED(error);
  }
  return Status::OK();
}

Status DvppDecodePngOp::OutputShape(const std::vector<TensorShape> &inputs, std::vector<TensorShape> &outputs) {
  RETURN_IF_NOT_OK(TensorOp::OutputShape(inputs, outputs));
  outputs.clear();
  TensorShape out({-1, 1, 1});  // we don't know what is output image size, but we know it should be 3 channels
  if (inputs[0].Rank() == 1) outputs.emplace_back(out);
  if (!outputs.empty()) return Status::OK();
  return Status(StatusCode::kMDUnexpectedError, "Input has a wrong shape");
}

Status DvppDecodePngOp::SetAscendResource(const std::shared_ptr<DeviceResource> &resource) {
  processor_ = std::static_pointer_cast<MDAclProcess>(resource->GetInstance());
  if (!processor_) {
    RETURN_STATUS_UNEXPECTED("Resource initialize fail, please check your env");
  }
  return Status::OK();
}

}  // namespace dataset
}  // namespace mindspore
