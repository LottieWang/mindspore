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

#include "ps/core/node.h"

namespace mindspore {
namespace ps {
namespace core {
std::string Node::node_id() const { return node_info_.node_id_; }

uint32_t Node::rank_id() const { return node_info_.rank_id_; }

NodeRole Node::role() const { return node_info_.node_role_; }

uint16_t Node::BoundPort() const { return node_info_.port_; }

std::string Node::BoundIp() const { return node_info_.ip_; }

bool Node::WaitForStart(const uint32_t &timeout) {
  std::unique_lock<std::mutex> lock(wait_start_mutex_);
  bool res = wait_start_cond_.wait_for(lock, std::chrono::seconds(timeout), [this] {
    bool result = this->is_ready_.load();
    if (result) {
      MS_LOG(INFO) << "The node id:" << node_info_.node_id_ << " is success start!";
    }
    return result;
  });
  return res;
}

bool Node::SendMessageSync(const std::shared_ptr<TcpClient> &client, const CommMessage &message,
                           const uint32_t &timeout) {
  uint64_t request_id = AddMessageTrack(1);
  const_cast<CommMessage &>(message).mutable_pb_meta()->set_request_id(request_id);
  client->SendMessage(message);
  MS_LOG(DEBUG) << "The node role is:" << CommUtil::NodeRoleToString(node_info_.node_role_)
                << ", the node id is:" << node_info_.node_id_ << " send the request id is:" << request_id;
  return Wait(request_id, timeout);
}

uint64_t Node::SendMessageAsync(const std::shared_ptr<TcpClient> &client, const std::shared_ptr<MessageMeta> &meta,
                                const Protos &protos, const void *data, size_t size) {
  MS_EXCEPTION_IF_NULL(client);
  MS_EXCEPTION_IF_NULL(meta);
  MS_EXCEPTION_IF_NULL(data);
  uint64_t request_id = AddMessageTrack(1);
  meta->set_request_id(request_id);
  client->SendMessage(meta, protos, data, size);
  MS_LOG(DEBUG) << "The node role is:" << CommUtil::NodeRoleToString(node_info_.node_role_)
                << ", the node id is:" << node_info_.node_id_ << " send the request id is:" << request_id;
  return request_id;
}

bool Node::SendMessageSync(const std::shared_ptr<TcpClient> &client, std::shared_ptr<MessageMeta> meta,
                           const Protos &protos, const void *data, size_t size, const uint32_t &timeout) {
  MS_EXCEPTION_IF_NULL(client);
  MS_EXCEPTION_IF_NULL(meta);
  MS_EXCEPTION_IF_NULL(data);
  uint64_t request_id = AddMessageTrack(1);
  meta->set_request_id(request_id);
  client->SendMessage(meta, protos, data, size);
  MS_LOG(DEBUG) << "The node role is:" << CommUtil::NodeRoleToString(node_info_.node_role_)
                << ", the node id is:" << node_info_.node_id_ << " send the request id is:" << request_id;
  return Wait(request_id, timeout);
}

bool Node::Wait(uint64_t request_id, const uint32_t &timeout) {
  std::unique_lock<std::mutex> lock(message_tracker_mutex_);
  bool res = message_tracker_cond_.wait_for(lock, std::chrono::seconds(timeout), [&] {
    bool ret = message_tracker_[request_id].first == message_tracker_[request_id].second;
    return ret;
  });
  message_tracker_.erase(request_id);
  return res;
}

uint64_t Node::AddMessageTrack(const uint32_t &expected_response) {
  std::lock_guard<std::mutex> lock(message_tracker_mutex_);
  uint64_t request_id = ++next_request_id_;
  message_tracker_[request_id] = std::make_pair(expected_response, 0);
  return request_id;
}

bool Node::CheckMessageTrack(const uint64_t &request_id) {
  std::lock_guard<std::mutex> lock(message_tracker_mutex_);
  return message_tracker_[request_id].first == message_tracker_[request_id].second + 1;
}

void Node::NotifyMessageArrival(const std::shared_ptr<MessageMeta> &meta) {
  std::lock_guard<std::mutex> lock(message_tracker_mutex_);
  uint64_t request_id = meta->request_id();

  if (message_tracker_.count(request_id)) {
    message_tracker_[request_id].second++;
  } else {
    MS_LOG(WARNING) << "The request id:" << request_id << " is removed.";
  }

  message_tracker_cond_.notify_all();
}
}  // namespace core
}  // namespace ps
}  // namespace mindspore
