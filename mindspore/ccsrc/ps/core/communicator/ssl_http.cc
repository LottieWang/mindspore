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

#include "ps/core/communicator/ssl_http.h"

#include <sys/time.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <iomanip>
#include <sstream>

namespace mindspore {
namespace ps {
namespace core {
SSLHTTP::SSLHTTP() : ssl_ctx_(nullptr) { InitSSL(); }

SSLHTTP::~SSLHTTP() { CleanSSL(); }

void SSLHTTP::InitSSL() {
  CommUtil::InitOpenSSLEnv();
  ssl_ctx_ = SSL_CTX_new(SSLv23_server_method());
  if (!ssl_ctx_) {
    MS_LOG(EXCEPTION) << "SSL_CTX_new failed";
  }
  X509_STORE *store = SSL_CTX_get_cert_store(ssl_ctx_);
  MS_EXCEPTION_IF_NULL(store);
  if (X509_STORE_set_default_paths(store) != 1) {
    MS_LOG(EXCEPTION) << "X509_STORE_set_default_paths failed";
  }

  std::unique_ptr<Configuration> config_ =
    std::make_unique<FileConfiguration>(PSContext::instance()->config_file_path());
  MS_EXCEPTION_IF_NULL(config_);
  if (!config_->Initialize()) {
    MS_LOG(EXCEPTION) << "The config file is empty.";
  }

  // 1.Parse the server's certificate and the ciphertext of key.
  std::string server_cert = kCertificateChain;
  std::string path = CommUtil::ParseConfig(*(config_), kServerCertPath);
  if (!CommUtil::IsFileExists(path)) {
    MS_LOG(EXCEPTION) << "The key:" << kServerCertPath << "'s value is not exist.";
  }
  server_cert = path;

  // 2. Parse the server password.
  std::string server_password = CommUtil::ParseConfig(*(config_), kServerPassword);
  if (server_password.empty()) {
    MS_LOG(EXCEPTION) << "The client password's value is empty.";
  }

  EVP_PKEY *pkey = nullptr;
  X509 *cert = nullptr;
  STACK_OF(X509) *ca_stack = nullptr;
  BIO *bio = BIO_new_file(server_cert.c_str(), "rb");
  MS_EXCEPTION_IF_NULL(bio);
  PKCS12 *p12 = d2i_PKCS12_bio(bio, nullptr);
  MS_EXCEPTION_IF_NULL(p12);
  BIO_free_all(bio);
  if (!PKCS12_parse(p12, server_password.c_str(), &pkey, &cert, &ca_stack)) {
    MS_LOG(EXCEPTION) << "PKCS12_parse failed.";
  }
  PKCS12_free(p12);
  std::string default_cipher_list = CommUtil::ParseConfig(*config_, kCipherList);
  if (!SSL_CTX_set_cipher_list(ssl_ctx_, default_cipher_list.c_str())) {
    MS_LOG(EXCEPTION) << "SSL use set cipher list failed!";
  }

  if (!SSL_CTX_use_certificate(ssl_ctx_, cert)) {
    MS_LOG(EXCEPTION) << "SSL use certificate chain file failed!";
  }
  if (!SSL_CTX_use_PrivateKey(ssl_ctx_, pkey)) {
    MS_LOG(EXCEPTION) << "SSL use private key file failed!";
  }
  if (!SSL_CTX_check_private_key(ssl_ctx_)) {
    MS_LOG(EXCEPTION) << "SSL check private key file failed!";
  }
}

void SSLHTTP::CleanSSL() {
  if (ssl_ctx_ != nullptr) {
    SSL_CTX_free(ssl_ctx_);
  }
  ERR_free_strings();
  EVP_cleanup();
  ERR_remove_thread_state(nullptr);
  CRYPTO_cleanup_all_ex_data();
}

SSL_CTX *SSLHTTP::GetSSLCtx() const { return ssl_ctx_; }
}  // namespace core
}  // namespace ps
}  // namespace mindspore
