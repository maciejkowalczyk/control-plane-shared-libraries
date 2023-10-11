/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cpio/client_providers/private_key_client_provider/src/private_key_client_utils.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/time_util.h>

#include "absl/strings/escaping.h"
#include "core/interface/http_types.h"
#include "core/test/utils/timestamp_test_utils.h"
#include "core/utils/src/base64.h"
#include "public/core/interface/execution_result.h"
#include "public/core/test/interface/execution_result_matchers.h"

using google::cmrt::sdk::kms_service::v1::DecryptRequest;
using google::cmrt::sdk::private_key_service::v1::PrivateKey;
using google::protobuf::util::TimeUtil;
using google::scp::core::ExecutionResult;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_CANNOT_READ_ENCRYPTED_KEY_SET;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_INVALID_KEY_DATA_COUNT;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_INVALID_KEY_RESOURCE_NAME;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_INVALID_VENDING_ENDPOINT_COUNT;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_KEY_DATA_NOT_FOUND;
using google::scp::core::errors::
    SC_PRIVATE_KEY_CLIENT_PROVIDER_SECRET_PIECE_SIZE_UNMATCHED;
using google::scp::core::test::ExpectTimestampEquals;
using google::scp::core::test::ResultIs;
using google::scp::core::utils::Base64Encode;
using google::scp::cpio::client_providers::KeyData;
using google::scp::cpio::client_providers::PrivateKeyFetchingResponse;
using std::make_pair;
using std::make_shared;
using std::move;
using std::shared_ptr;
using std::string;
using std::to_string;
using std::vector;

namespace {
constexpr char kTestKeyId[] = "name_test";
constexpr char kTestResourceName[] = "encryptionKeys/name_test";
constexpr char kTestPublicKeysetHandle[] = "publicKeysetHandle";
constexpr char kTestPublicKeyMaterial[] = "publicKeyMaterial";
constexpr int kTestExpirationTime = 123456;
constexpr int kTestCreationTime = 111111;
constexpr char kTestPublicKeySignature[] = "publicKeySignature";
constexpr char kTestKeyEncryptionKeyUriWithPrefix[] =
    "1234567890keyEncryptionKeyUri";
constexpr char kTestKeyEncryptionKeyUri[] = "keyEncryptionKeyUri";
constexpr char kTestKeyMaterial[] = "keyMaterial";
constexpr char kSinglePartyKeyMaterialJson[] =
    R"(
    {
    "keysetInfo": {
        "primaryKeyId": 1353288376,
        "keyInfo": [{
            "typeUrl": "type.googleapis.com/google.crypto.tink.EciesAeadHkdfPrivateKey",
            "outputPrefixType": "TINK",
            "keyId": 1353288376,
            "status": "ENABLED"
        }]
    },
    "encryptedKeyset": "singlepartykey"
    }
    )";
}  // namespace

namespace google::scp::cpio::client_providers::test {
shared_ptr<EncryptionKey> CreateEncryptionKeyBase() {
  auto encryption_key = make_shared<EncryptionKey>();
  encryption_key->key_id = make_shared<string>(kTestKeyId);
  encryption_key->resource_name = make_shared<string>(kTestResourceName);
  encryption_key->expiration_time_in_ms = kTestExpirationTime;
  encryption_key->creation_time_in_ms = kTestCreationTime;
  encryption_key->public_key_material =
      make_shared<string>(kTestPublicKeyMaterial);
  encryption_key->public_keyset_handle =
      make_shared<string>(kTestPublicKeysetHandle);
  return encryption_key;
}

shared_ptr<EncryptionKey> CreateEncryptionKey(
    const string& key_resource_name = kTestKeyEncryptionKeyUriWithPrefix) {
  auto encryption_key = CreateEncryptionKeyBase();
  encryption_key->encryption_key_type =
      EncryptionKeyType::kMultiPartyHybridEvenKeysplit;
  auto key_data = make_shared<KeyData>();
  key_data->key_encryption_key_uri = make_shared<string>(key_resource_name);
  key_data->key_material = make_shared<string>(kTestKeyMaterial);
  key_data->public_key_signature = make_shared<string>(kTestPublicKeySignature);
  encryption_key->key_data.emplace_back(key_data);
  return encryption_key;
}

TEST(PrivateKeyClientUtilsTest, GetKmsDecryptRequestSuccess) {
  auto encryption_key = CreateEncryptionKey();
  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_SUCCESS(result);
  EXPECT_EQ(kms_decrypt_request.ciphertext(), kTestKeyMaterial);
  EXPECT_EQ(kms_decrypt_request.key_resource_name(), kTestKeyEncryptionKeyUri);
}

TEST(PrivateKeyClientUtilsTest, GetKmsDecryptRequestFailed) {
  auto encryption_key = CreateEncryptionKey();

  auto key_data = make_shared<KeyData>();
  key_data->key_encryption_key_uri = make_shared<string>("");
  key_data->key_material = make_shared<string>("");
  key_data->public_key_signature = make_shared<string>("");
  encryption_key->key_data = vector<shared_ptr<KeyData>>({key_data});

  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_THAT(result, ResultIs(FailureExecutionResult(
                          SC_PRIVATE_KEY_CLIENT_PROVIDER_KEY_DATA_NOT_FOUND)));
}

TEST(PrivateKeyClientUtilsTest,
     GetKmsDecryptRequestWithInvalidKeyResourceNameFailed) {
  auto encryption_key = CreateEncryptionKey("invalid");
  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_THAT(result,
              ResultIs(FailureExecutionResult(
                  SC_PRIVATE_KEY_CLIENT_PROVIDER_INVALID_KEY_RESOURCE_NAME)));
}

shared_ptr<EncryptionKey> CreateSinglePartyEncryptionKey(
    int8_t key_data_count = 1,
    const string& key_material = kSinglePartyKeyMaterialJson) {
  auto encryption_key = CreateEncryptionKeyBase();
  encryption_key->encryption_key_type =
      EncryptionKeyType::kSinglePartyHybridKey;
  for (int i = 0; i < key_data_count; ++i) {
    auto key_data = make_shared<KeyData>();
    key_data->key_encryption_key_uri =
        make_shared<string>(kTestKeyEncryptionKeyUriWithPrefix);
    key_data->key_material = make_shared<string>(key_material);
    key_data->public_key_signature =
        make_shared<string>(kTestPublicKeySignature);
    encryption_key->key_data.emplace_back(key_data);
  }
  return encryption_key;
}

TEST(PrivateKeyClientUtilsTest, GetKmsDecryptRequestForSinglePartySucceeded) {
  auto encryption_key = CreateSinglePartyEncryptionKey();
  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_SUCCESS(result);
  // Fill the key with padding.
  string unescaped_key;
  absl::Base64Unescape("singlepartykey", &unescaped_key);
  string escaped_key;
  absl::Base64Escape(unescaped_key, &escaped_key);
  EXPECT_EQ(kms_decrypt_request.ciphertext(), escaped_key);
  EXPECT_EQ(kms_decrypt_request.key_resource_name(), kTestKeyEncryptionKeyUri);
}

TEST(PrivateKeyClientUtilsTest,
     GetKmsDecryptRequestForSinglePartyFailedForInvalidKeyDataCount) {
  auto encryption_key =
      CreateSinglePartyEncryptionKey(2, kSinglePartyKeyMaterialJson);
  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_THAT(result,
              ResultIs(FailureExecutionResult(
                  SC_PRIVATE_KEY_CLIENT_PROVIDER_INVALID_KEY_DATA_COUNT)));
}

TEST(PrivateKeyClientUtilsTest,
     GetKmsDecryptRequestForSinglePartyFailedForInvalidJsonKeyset) {
  auto encryption_key = CreateSinglePartyEncryptionKey(1, "invalidjson");
  DecryptRequest kms_decrypt_request;
  auto result = PrivateKeyClientUtils::GetKmsDecryptRequest(
      encryption_key, kms_decrypt_request);
  EXPECT_THAT(
      result,
      ResultIs(FailureExecutionResult(
          SC_PRIVATE_KEY_CLIENT_PROVIDER_CANNOT_READ_ENCRYPTED_KEY_SET)));
}

DecryptResult CreateDecryptResult(
    string plaintext, ExecutionResult decrypt_result = SuccessExecutionResult(),
    bool multi_party_key = true) {
  auto encryption_key = CreateEncryptionKey();
  if (!multi_party_key) {
    encryption_key = CreateSinglePartyEncryptionKey();
  }
  DecryptResult result;
  result.decrypt_result = decrypt_result;
  result.encryption_key = move(*encryption_key);
  result.plaintext = move(plaintext);
  return result;
}

TEST(PrivateKeyClientUtilsTest, ConsturctPrivateKeySuccess) {
  vector<DecryptResult> decrypt_results;
  decrypt_results.emplace_back(
      CreateDecryptResult("\270G\005\364$\253\273\331\353\336\216>"));
  decrypt_results.emplace_back(
      CreateDecryptResult("\327\002\204 \232\377\002\330\225DB\f"));
  decrypt_results.emplace_back(
      CreateDecryptResult("; \362\240\2369\334r\r\373\253W"));

  auto private_key_or =
      PrivateKeyClientUtils::ConstructPrivateKey(decrypt_results);
  EXPECT_SUCCESS(private_key_or);
  auto private_key = *private_key_or;
  EXPECT_EQ(private_key.key_id(), "name_test");
  EXPECT_EQ(private_key.public_key(), kTestPublicKeyMaterial);
  ExpectTimestampEquals(private_key.expiration_time(),
                        TimeUtil::MillisecondsToTimestamp(kTestExpirationTime));
  ExpectTimestampEquals(private_key.creation_time(),
                        TimeUtil::MillisecondsToTimestamp(kTestCreationTime));
  string encoded_key;
  Base64Encode("Test message", encoded_key);
  EXPECT_EQ(private_key.private_key(), encoded_key);
}

TEST(PrivateKeyClientUtilsTest,
     ConsturctPrivateKeyFailedWithUnmatchedPlaintextSize) {
  vector<DecryptResult> decrypt_results;
  decrypt_results.emplace_back(
      CreateDecryptResult("\270G\005\364$\253\273\331\353\336\216>"));
  decrypt_results.emplace_back(
      CreateDecryptResult("\327\002\204 \232\377\002\330"));
  decrypt_results.emplace_back(
      CreateDecryptResult("; \362\240\2369\334r\r\373\253W"));

  auto private_key_or =
      PrivateKeyClientUtils::ConstructPrivateKey(decrypt_results);
  EXPECT_THAT(private_key_or,
              ResultIs(FailureExecutionResult(
                  SC_PRIVATE_KEY_CLIENT_PROVIDER_SECRET_PIECE_SIZE_UNMATCHED)));
}

TEST(PrivateKeyClientUtilsTest,
     ConsturctPrivateKeyFailedWithEmptyDecryptResult) {
  vector<DecryptResult> decrypt_results;

  auto private_key_or =
      PrivateKeyClientUtils::ConstructPrivateKey(decrypt_results);
  EXPECT_THAT(private_key_or,
              ResultIs(FailureExecutionResult(
                  SC_PRIVATE_KEY_CLIENT_PROVIDER_KEY_DATA_NOT_FOUND)));
}

TEST(PrivateKeyClientUtilsTest, ExtractAnyFailureNoFailure) {
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"));
  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"));
}

TEST(PrivateKeyClientUtilsTest, ExtractAnyFailureReturnFetchFailure) {
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  keys_result_list[0].fetch_result = failure;

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"),
      ResultIs(failure));
  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"),
      ResultIs(failure));
}

TEST(PrivateKeyClientUtilsTest, ExtractAnyFailureReturnFetchFailureForOneKey) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(make_pair("key1", failure),
                                                     fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"),
      ResultIs(failure));
  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"));
}

TEST(PrivateKeyClientUtilsTest,
     ExtractAnyFailureReturnFetchFailureForBothKeys) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(make_pair("key1", failure),
                                                     fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(make_pair("key2", failure),
                                                     fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"),
      ResultIs(failure));
  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"),
      ResultIs(failure));
}

TEST(PrivateKeyClientUtilsTest,
     ExtractAnyFailureReturnDecryptFailureForOneKey) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"),
      ResultIs(failure));
  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"));
}

TEST(PrivateKeyClientUtilsTest,
     ExtractAnyFailureReturnDecryptFailureForBothKeys) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key2", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("", failure)), decrypt_result_out);

  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key1"),
      ResultIs(failure));
  EXPECT_THAT(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key2"),
      ResultIs(failure));
}

TEST(PrivateKeyClientUtilsTest, ExtractAnyFailureFetchResultNotFound) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(make_pair("key1", failure),
                                                     fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(make_pair("key2", failure),
                                                     fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key3", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(make_pair("key1", failure),
                                                     fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(make_pair("key2", failure),
                                                     fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key3", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key3"));
}

TEST(PrivateKeyClientUtilsTest, ExtractAnyFailureDecryptResultNotFound) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  ExecutionResult fetch_result_out;
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[0].fetch_result_key_id_map.Insert(
      make_pair("key3", SuccessExecutionResult()), fetch_result_out);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("", failure)), decrypt_result_out);

  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key1", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].fetch_result_key_id_map.Insert(
      make_pair("key3", SuccessExecutionResult()), fetch_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("", failure)), decrypt_result_out);

  EXPECT_SUCCESS(
      PrivateKeyClientUtils::ExtractAnyFailure(keys_result_list, "key3"));
}

TEST(PrivateKeyClientUtilsTest, ExtractSinglePartyKeyReturnNoKey) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("")), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("")), decrypt_result_out);

  EXPECT_FALSE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key1")
          .has_value());
  EXPECT_FALSE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key2")
          .has_value());
  EXPECT_FALSE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key3")
          .has_value());
}

TEST(PrivateKeyClientUtilsTest, ExtractSinglePartyKeyReturnKey) {
  auto failure = FailureExecutionResult(SC_UNKNOWN);
  vector<KeysResultPerEndpoint> keys_result_list(2);
  DecryptResult decrypt_result_out;
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure, false)),
      decrypt_result_out);
  keys_result_list[0].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("", failure, false)),
      decrypt_result_out);

  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key1", CreateDecryptResult("", failure)), decrypt_result_out);
  keys_result_list[1].decrypt_result_key_id_map.Insert(
      make_pair("key2", CreateDecryptResult("", failure, false)),
      decrypt_result_out);

  EXPECT_TRUE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key1")
          .has_value());
  EXPECT_TRUE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key2")
          .has_value());
  EXPECT_FALSE(
      PrivateKeyClientUtils::ExtractSinglePartyKey(keys_result_list, "key3")
          .has_value());
}
}  // namespace google::scp::cpio::client_providers::test
