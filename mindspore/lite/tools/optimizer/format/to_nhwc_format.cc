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

#include "tools/optimizer/format/to_nhwc_format.h"

namespace mindspore {
namespace opt {
STATUS ToNHWCFormat::GetTransNodeFormatType(const CNodePtr &cnode, opt::TransTypePair *trans_info) {
  MS_ASSERT(cnode != nullptr);
  auto prim_node = cnode->input(0);
  auto prim = GetValueNode<PrimitivePtr>(prim_node);
  MS_ASSERT(prim != nullptr);
  if (prim->GetAttr(ops::kFormat) != nullptr) {
    auto node_format = GetValue<int64_t>(prim->GetAttr(ops::kFormat));
    if (node_format == mindspore::NHWC) {
      MS_LOG(DEBUG) << "node's format has been nhwc, no need to transfer, " << cnode->fullname_with_scope();
      return lite::RET_OK;
    }
    if (node_format != mindspore::NCHW) {
      MS_LOG(ERROR) << "node's format is invalid, which must be nhwc or nchw, now is " << node_format
                    << ", node name is " << cnode->fullname_with_scope();
      return lite::RET_ERROR;
    }
  }
  if (sensitive_ops_.find(prim->name()) != sensitive_ops_.end()) {
    trans_info->pre_ = opt::kNCHW2NHWC;
    trans_info->post_ = opt::kNHWC2NCHW;
  }
  return lite::RET_OK;
}

STATUS ToNHWCFormat::DecideConvWeightSrcAndDstFormat(const CNodePtr &cnode, schema::Format *src_format,
                                                     schema::Format *dst_format) {
  MS_ASSERT(cnode != nullptr && src_format != nullptr && dst_format != nullptr);
  *src_format = schema::Format_KCHW;
  *dst_format = schema::Format_KHWC;
  return lite::RET_OK;
}
}  // namespace opt
}  // namespace mindspore
