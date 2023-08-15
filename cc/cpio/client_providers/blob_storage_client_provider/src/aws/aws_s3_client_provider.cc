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

#include "aws_s3_client_provider.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/AbortMultipartUploadRequest.h>
#include <aws/s3/model/CompleteMultipartUploadRequest.h>
#include <aws/s3/model/CompletedMultipartUpload.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <google/protobuf/util/time_util.h>

#include "absl/strings/str_cat.h"
#include "core/async_executor/src/aws/aws_async_executor.h"
#include "core/utils/src/base64.h"
#include "core/utils/src/hashing.h"
#include "cpio/client_providers/blob_storage_client_provider/src/aws/aws_s3_utils.h"
#include "cpio/client_providers/blob_storage_client_provider/src/common/error_codes.h"
#include "cpio/client_providers/instance_client_provider/src/aws/aws_instance_client_utils.h"
#include "cpio/common/src/aws/aws_utils.h"
#include "public/cpio/interface/blob_storage_client/type_def.h"

using Aws::MakeShared;
using Aws::String;
using Aws::StringStream;
using Aws::Client::AsyncCallerContext;
using Aws::Client::ClientConfiguration;
using Aws::S3::S3Client;
using Aws::S3::Model::AbortMultipartUploadOutcome;
using Aws::S3::Model::AbortMultipartUploadRequest;
using Aws::S3::Model::CompletedMultipartUpload;
using Aws::S3::Model::CompletedPart;
using Aws::S3::Model::CompleteMultipartUploadOutcome;
using Aws::S3::Model::CompleteMultipartUploadRequest;
using Aws::S3::Model::CreateMultipartUploadOutcome;
using Aws::S3::Model::CreateMultipartUploadRequest;
using Aws::S3::Model::DeleteObjectOutcome;
using Aws::S3::Model::DeleteObjectRequest;
using Aws::S3::Model::DeleteObjectResult;
using Aws::S3::Model::GetObjectOutcome;
using Aws::S3::Model::GetObjectRequest;
using Aws::S3::Model::GetObjectResult;
using Aws::S3::Model::ListObjectsOutcome;
using Aws::S3::Model::ListObjectsRequest;
using Aws::S3::Model::ListObjectsResult;
using Aws::S3::Model::PutObjectOutcome;
using Aws::S3::Model::PutObjectRequest;
using Aws::S3::Model::PutObjectResult;
using Aws::S3::Model::UploadPartOutcome;
using Aws::S3::Model::UploadPartRequest;
using google::cmrt::sdk::blob_storage_service::v1::Blob;
using google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using google::cmrt::sdk::blob_storage_service::v1::DeleteBlobRequest;
using google::cmrt::sdk::blob_storage_service::v1::DeleteBlobResponse;
using google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using google::cmrt::sdk::blob_storage_service::v1::GetBlobStreamRequest;
using google::cmrt::sdk::blob_storage_service::v1::GetBlobStreamResponse;
using google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using google::cmrt::sdk::blob_storage_service::v1::PutBlobRequest;
using google::cmrt::sdk::blob_storage_service::v1::PutBlobResponse;
using google::cmrt::sdk::blob_storage_service::v1::PutBlobStreamRequest;
using google::cmrt::sdk::blob_storage_service::v1::PutBlobStreamResponse;
using google::protobuf::util::TimeUtil;
using google::scp::core::AsyncContext;
using google::scp::core::AsyncExecutorInterface;
using google::scp::core::AsyncPriority;
using google::scp::core::ConsumerStreamingContext;
using google::scp::core::ExecutionResult;
using google::scp::core::ExecutionResultOr;
using google::scp::core::FailureExecutionResult;
using google::scp::core::ProducerStreamingContext;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::async_executor::aws::AwsAsyncExecutor;
using google::scp::core::common::kZeroUuid;
using google::scp::core::common::TimeProvider;
using google::scp::core::errors::SC_BLOB_STORAGE_PROVIDER_EMPTY_ETAG;
using google::scp::core::errors::SC_BLOB_STORAGE_PROVIDER_ERROR_GETTING_BLOB;
using google::scp::core::errors::SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS;
using google::scp::core::errors::SC_BLOB_STORAGE_PROVIDER_RETRIABLE_ERROR;
using google::scp::core::errors::
    SC_BLOB_STORAGE_PROVIDER_STREAM_SESSION_CANCELLED;
using google::scp::core::errors::
    SC_BLOB_STORAGE_PROVIDER_STREAM_SESSION_EXPIRED;
using google::scp::core::utils::Base64Encode;
using google::scp::core::utils::CalculateMd5Hash;
using google::scp::cpio::client_providers::AwsInstanceClientUtils;
using std::bind;
using std::make_shared;
using std::move;
using std::shared_ptr;
using std::string;
using std::vector;
using std::chrono::duration_cast;
using std::chrono::minutes;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

namespace {
constexpr char kAwsS3Provider[] = "AwsS3ClientProvider";
constexpr size_t kMaxConcurrentConnections = 1000;
constexpr size_t kListBlobsMetadataMaxResults = 1000;
constexpr nanoseconds kDefaultStreamKeepaliveNanos =
    duration_cast<nanoseconds>(minutes(5));
constexpr nanoseconds kMaximumStreamKeepaliveNanos =
    duration_cast<nanoseconds>(minutes(10));
constexpr seconds kPutBlobRescanTime = seconds(5);

template <typename Context, typename Request>
ExecutionResult SetContentMd5(Context& context, Request& request,
                              const string& body) {
  string md5_checksum;
  auto execution_result = CalculateMd5Hash(body, md5_checksum);
  if (!execution_result.Successful()) {
    SCP_ERROR_CONTEXT(kAwsS3Provider, context, execution_result,
                      "MD5 Hash generation failed");
    return execution_result;
  }

  string base64_md5_checksum;
  execution_result = Base64Encode(md5_checksum, base64_md5_checksum);
  if (!execution_result.Successful()) {
    SCP_ERROR_CONTEXT(kAwsS3Provider, context, execution_result,
                      "Encoding MD5 to base64 failed");
    return execution_result;
  }
  request.SetContentMD5(base64_md5_checksum.c_str());
  return SuccessExecutionResult();
}

}  // namespace

namespace google::scp::cpio::client_providers {
shared_ptr<ClientConfiguration> AwsS3ClientProvider::CreateClientConfiguration(
    const string& region) noexcept {
  return common::CreateClientConfiguration(make_shared<string>(move(region)));
}

ExecutionResult AwsS3ClientProvider::Init() noexcept {
  return SuccessExecutionResult();
}

ExecutionResult AwsS3ClientProvider::Run() noexcept {
  auto region_code_or =
      AwsInstanceClientUtils::GetCurrentRegionCode(instance_client_);
  if (!region_code_or.Successful()) {
    SCP_ERROR(kAwsS3Provider, kZeroUuid, region_code_or.result(),
              "Failed to get region code for current instance");
    return region_code_or.result();
  }

  auto client_or = s3_factory_->CreateClient(
      *CreateClientConfiguration(*region_code_or), io_async_executor_);
  if (!client_or.Successful()) {
    SCP_ERROR(kAwsS3Provider, kZeroUuid, client_or.result(),
              "Failed creating AWS S3 client.");
    return client_or.result();
  }
  s3_client_ = std::move(*client_or);
  return SuccessExecutionResult();
}

ExecutionResult AwsS3ClientProvider::Stop() noexcept {
  return SuccessExecutionResult();
}

ExecutionResult AwsS3ClientProvider::GetBlob(
    AsyncContext<GetBlobRequest, GetBlobResponse>& get_blob_context) noexcept {
  const auto& request = *get_blob_context.request;
  if (request.blob_metadata().bucket_name().empty() ||
      request.blob_metadata().blob_name().empty()) {
    get_blob_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(kAwsS3Provider, get_blob_context, get_blob_context.result,
                      "Get blob request is missing bucket or blob name");
    get_blob_context.Finish();
    return get_blob_context.result;
  }
  if (request.has_byte_range() && request.byte_range().begin_byte_index() >
                                      request.byte_range().end_byte_index()) {
    get_blob_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, get_blob_context, get_blob_context.result,
        "Get blob request provides begin_byte_index that is larger "
        "than end_byte_index");
    get_blob_context.Finish();
    return get_blob_context.result;
  }

  String bucket_name(request.blob_metadata().bucket_name());
  String blob_name(request.blob_metadata().blob_name());
  GetObjectRequest get_object_request;
  get_object_request.SetBucket(bucket_name);
  get_object_request.SetKey(blob_name);
  if (request.has_byte_range()) {
    // SetRange is inclusive on both ends.
    get_object_request.SetRange(
        absl::StrCat("bytes=", request.byte_range().begin_byte_index(), "-",
                     request.byte_range().end_byte_index()));
  }

  s3_client_->GetObjectAsync(get_object_request,
                             bind(&AwsS3ClientProvider::OnGetObjectCallback,
                                  this, get_blob_context, _1, _2, _3, _4),
                             nullptr);

  return SuccessExecutionResult();
}

void AwsS3ClientProvider::OnGetObjectCallback(
    AsyncContext<GetBlobRequest, GetBlobResponse>& get_blob_context,
    const S3Client* s3_client, const GetObjectRequest& get_object_request,
    GetObjectOutcome get_object_outcome,
    const shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!get_object_outcome.IsSuccess()) {
    get_blob_context.result = AwsS3Utils::ConvertS3ErrorToExecutionResult(
        get_object_outcome.GetError().GetErrorType());

    SCP_ERROR_CONTEXT(kAwsS3Provider, get_blob_context, get_blob_context.result,
                      "Get blob request failed. Error code: %d, message: %s",
                      get_object_outcome.GetError().GetResponseCode(),
                      get_object_outcome.GetError().GetMessage().c_str());
    FinishContext(get_blob_context.result, get_blob_context,
                  cpu_async_executor_, AsyncPriority::High);
    return;
  }

  auto& result = get_object_outcome.GetResult();
  auto& body = result.GetBody();
  auto content_length = result.GetContentLength();

  get_blob_context.response = make_shared<GetBlobResponse>();
  get_blob_context.response->mutable_blob()->mutable_metadata()->CopyFrom(
      get_blob_context.request->blob_metadata());
  get_blob_context.response->mutable_blob()->mutable_data()->resize(
      content_length);
  get_blob_context.result = SuccessExecutionResult();

  if (!body.read(
          get_blob_context.response->mutable_blob()->mutable_data()->data(),
          content_length)) {
    get_blob_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_ERROR_GETTING_BLOB);
  }
  FinishContext(get_blob_context.result, get_blob_context, cpu_async_executor_,
                AsyncPriority::High);
}

ExecutionResult AwsS3ClientProvider::GetBlobStream(
    ConsumerStreamingContext<GetBlobStreamRequest, GetBlobStreamResponse>&
        get_blob_stream_context) noexcept {
  // TODO implement.
  return FailureExecutionResult(SC_UNKNOWN);
}

ExecutionResult AwsS3ClientProvider::ListBlobsMetadata(
    AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>&
        list_blobs_context) noexcept {
  const auto& request = *list_blobs_context.request;
  if (request.blob_metadata().bucket_name().empty()) {
    list_blobs_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(kAwsS3Provider, list_blobs_context,
                      list_blobs_context.result,
                      "List blobs metadata request failed. Bucket name empty.");
    list_blobs_context.Finish();
    return list_blobs_context.result;
  }
  if (request.has_max_page_size() &&
      request.max_page_size() > kListBlobsMetadataMaxResults) {
    list_blobs_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, list_blobs_context, list_blobs_context.result,
        "List blobs metadata request failed. Max page size cannot be "
        "greater than 1000.");
    return list_blobs_context.result;
  }
  String bucket_name(list_blobs_context.request->blob_metadata().bucket_name());

  ListObjectsRequest list_objects_request;
  list_objects_request.SetBucket(bucket_name);
  list_objects_request.SetMaxKeys(
      list_blobs_context.request->has_max_page_size()
          ? list_blobs_context.request->max_page_size()
          : kListBlobsMetadataMaxResults);

  if (!list_blobs_context.request->blob_metadata().blob_name().empty()) {
    String blob_name(list_blobs_context.request->blob_metadata().blob_name());
    list_objects_request.SetPrefix(blob_name);
  }

  if (list_blobs_context.request->has_page_token()) {
    String marker(list_blobs_context.request->page_token());
    list_objects_request.SetMarker(marker);
  }

  s3_client_->ListObjectsAsync(
      list_objects_request,
      bind(&AwsS3ClientProvider::OnListObjectsMetadataCallback, this,
           list_blobs_context, _1, _2, _3, _4),
      nullptr);

  return SuccessExecutionResult();
}

void AwsS3ClientProvider::OnListObjectsMetadataCallback(
    AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>&
        list_blobs_metadata_context,
    const S3Client* s3_client, const ListObjectsRequest& list_objects_request,
    ListObjectsOutcome list_objects_outcome,
    const shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!list_objects_outcome.IsSuccess()) {
    list_blobs_metadata_context.result =
        AwsS3Utils::ConvertS3ErrorToExecutionResult(
            list_objects_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(kAwsS3Provider, list_blobs_metadata_context,
                      list_blobs_metadata_context.result,
                      "List blobs request failed. Error code: %d, message: %s",
                      list_objects_outcome.GetError().GetResponseCode(),
                      list_objects_outcome.GetError().GetMessage().c_str());
    FinishContext(list_blobs_metadata_context.result,
                  list_blobs_metadata_context, cpu_async_executor_,
                  AsyncPriority::High);
    return;
  }

  list_blobs_metadata_context.response =
      make_shared<ListBlobsMetadataResponse>();
  auto* blob_metadatas =
      list_blobs_metadata_context.response->mutable_blob_metadatas();
  for (auto& object : list_objects_outcome.GetResult().GetContents()) {
    BlobMetadata metadata;
    metadata.set_blob_name(object.GetKey());
    metadata.set_bucket_name(
        list_blobs_metadata_context.request->blob_metadata().bucket_name());

    blob_metadatas->Add(move(metadata));
  }

  list_blobs_metadata_context.response->set_next_page_token(
      list_objects_outcome.GetResult().GetNextMarker().c_str());

  list_blobs_metadata_context.result = SuccessExecutionResult();
  FinishContext(list_blobs_metadata_context.result, list_blobs_metadata_context,
                cpu_async_executor_, AsyncPriority::High);
}

ExecutionResult AwsS3ClientProvider::PutBlob(
    AsyncContext<PutBlobRequest, PutBlobResponse>& put_blob_context) noexcept {
  const auto& request = *put_blob_context.request;
  if (request.blob().metadata().bucket_name().empty() ||
      request.blob().metadata().blob_name().empty() ||
      request.blob().data().empty()) {
    put_blob_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_context, put_blob_context.result,
                      "Put blob request failed. Ensure that bucket name, blob "
                      "name, and data are present.");
    put_blob_context.Finish();
    return put_blob_context.result;
  }

  String bucket_name(request.blob().metadata().bucket_name());
  String blob_name(request.blob().metadata().blob_name());

  PutObjectRequest put_object_request;
  put_object_request.SetBucket(bucket_name);
  put_object_request.SetKey(blob_name);

  if (auto md5_result = SetContentMd5(put_blob_context, put_object_request,
                                      request.blob().data());
      !md5_result.Successful()) {
    put_blob_context.result = md5_result;
    put_blob_context.Finish();
    return put_blob_context.result;
  }

  auto input_data = Aws::MakeShared<Aws::StringStream>(
      "PutObjectInputStream", std::stringstream::in | std::stringstream::out |
                                  std::stringstream::binary);
  input_data->write(request.blob().data().c_str(),
                    request.blob().data().size());

  put_object_request.SetBody(input_data);

  s3_client_->PutObjectAsync(put_object_request,
                             bind(&AwsS3ClientProvider::OnPutObjectCallback,
                                  this, put_blob_context, _1, _2, _3, _4),
                             nullptr);

  return SuccessExecutionResult();
}

void AwsS3ClientProvider::OnPutObjectCallback(
    AsyncContext<PutBlobRequest, PutBlobResponse>& put_blob_context,
    const S3Client* s3_client, const PutObjectRequest& put_object_request,
    PutObjectOutcome put_object_outcome,
    const shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!put_object_outcome.IsSuccess()) {
    put_blob_context.result = AwsS3Utils::ConvertS3ErrorToExecutionResult(
        put_object_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_context, put_blob_context.result,
                      "Put blob request failed. Error code: %d, message: %s",
                      put_object_outcome.GetError().GetResponseCode(),
                      put_object_outcome.GetError().GetMessage().c_str());
    FinishContext(put_blob_context.result, put_blob_context,
                  cpu_async_executor_, AsyncPriority::High);
    return;
  }
  put_blob_context.response = make_shared<PutBlobResponse>();
  put_blob_context.result = SuccessExecutionResult();
  FinishContext(put_blob_context.result, put_blob_context, cpu_async_executor_,
                AsyncPriority::High);
}

ExecutionResult AwsS3ClientProvider::PutBlobStream(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context) noexcept {
  const auto& request = *put_blob_stream_context.request;
  if (request.blob_portion().metadata().bucket_name().empty() ||
      request.blob_portion().metadata().blob_name().empty() ||
      request.blob_portion().data().empty()) {
    put_blob_stream_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, put_blob_stream_context, put_blob_stream_context.result,
        "Put blob stream request failed. Ensure that bucket name, blob "
        "name, and data are present.");
    put_blob_stream_context.Finish();
    return put_blob_stream_context.result;
  }

  CreateMultipartUploadRequest create_request;
  create_request.SetBucket(
      request.blob_portion().metadata().bucket_name().c_str());
  create_request.SetKey(request.blob_portion().metadata().blob_name().c_str());
  s3_client_->CreateMultipartUploadAsync(
      create_request,
      bind(&AwsS3ClientProvider::OnCreateMultipartUploadCallback, this,
           put_blob_stream_context, _1, _2, _3, _4),
      nullptr);

  return SuccessExecutionResult();
}

void AwsS3ClientProvider::OnCreateMultipartUploadCallback(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    const S3Client* s3_client,
    const CreateMultipartUploadRequest& create_multipart_upload_request,
    CreateMultipartUploadOutcome create_multipart_upload_outcome,
    const std::shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!create_multipart_upload_outcome.IsSuccess()) {
    put_blob_stream_context.result =
        AwsS3Utils::ConvertS3ErrorToExecutionResult(
            create_multipart_upload_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, put_blob_stream_context, put_blob_stream_context.result,
        "Create multipart upload request failed. Error code: %d, "
        "message: %s",
        create_multipart_upload_outcome.GetError().GetResponseCode(),
        create_multipart_upload_outcome.GetError().GetMessage().c_str());
    FinishStreamingContext(put_blob_stream_context.result,
                           put_blob_stream_context, cpu_async_executor_,
                           AsyncPriority::High);
    return;
  }
  auto& request = *put_blob_stream_context.request;
  auto tracker = make_shared<PutBlobStreamTracker>();
  tracker->bucket_name = request.blob_portion().metadata().bucket_name();
  tracker->blob_name = request.blob_portion().metadata().blob_name();
  tracker->upload_id =
      create_multipart_upload_outcome.GetResult().GetUploadId();
  auto duration = request.has_stream_keepalive_duration()
                      ? nanoseconds(TimeUtil::DurationToNanoseconds(
                            request.stream_keepalive_duration()))
                      : kDefaultStreamKeepaliveNanos;
  if (duration > kMaximumStreamKeepaliveNanos) {
    auto result = FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, put_blob_stream_context, result,
        "Supplied keepalive duration is greater than the maximum of "
        "10 minutes.");
    FinishStreamingContext(result, put_blob_stream_context,
                           cpu_async_executor_);
    return;
  }
  tracker->expiry_time_ns =
      TimeProvider::GetWallTimestampInNanoseconds() + duration;

  // Upload the first part as it is in this request.
  UploadPartRequest part_request;
  part_request.SetBucket(tracker->bucket_name.c_str());
  part_request.SetKey(tracker->blob_name.c_str());
  part_request.SetPartNumber(1);
  part_request.SetUploadId(tracker->upload_id.c_str());

  part_request.SetBody(MakeShared<StringStream>("WriteStream::Upload",
                                                request.blob_portion().data()));

  if (auto md5_result = SetContentMd5(put_blob_stream_context, part_request,
                                      request.blob_portion().data());
      !md5_result.Successful()) {
    put_blob_stream_context.result = md5_result;
    FinishStreamingContext(put_blob_stream_context.result,
                           put_blob_stream_context, cpu_async_executor_);
    return;
  }

  s3_client->UploadPartAsync(
      part_request,
      bind(&AwsS3ClientProvider::OnUploadPartCallback, this,
           put_blob_stream_context, tracker, _1, _2, _3, _4),
      nullptr);
}

void AwsS3ClientProvider::OnUploadPartCallback(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    shared_ptr<PutBlobStreamTracker> tracker, const S3Client* s3_client,
    const UploadPartRequest& upload_part_request,
    UploadPartOutcome upload_part_outcome,
    const std::shared_ptr<const AsyncCallerContext> async_context) noexcept {
  // We get called in 2 ways:
  // 1. UploadPart succeeds
  // 2. The wakeup time has elapsed.
  //
  // In the case of 1, the part number in the request will be equal to our
  // next_part_number. In the case of 2, the part number in the request will be
  // the part number of the previously uploaded part - i.e. next_part_number
  // - 1.
  if (tracker->next_part_number == upload_part_request.GetPartNumber()) {
    // If the most recently uploaded part is the same as the "next" one, update
    // the trackers.
    if (!upload_part_outcome.IsSuccess()) {
      put_blob_stream_context.result =
          AwsS3Utils::ConvertS3ErrorToExecutionResult(
              upload_part_outcome.GetError().GetErrorType());
      SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context,
                        put_blob_stream_context.result,
                        "Upload part request failed. Error code: %d, "
                        "message: %s",
                        upload_part_outcome.GetError().GetResponseCode(),
                        upload_part_outcome.GetError().GetMessage().c_str());
      AbortUpload(put_blob_stream_context, tracker);
      return;
    }
    CompletedPart completed_part;
    completed_part.SetPartNumber(upload_part_request.GetPartNumber());
    const auto& etag = upload_part_outcome.GetResult().GetETag();
    if (etag.empty()) {
      put_blob_stream_context.result =
          FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_EMPTY_ETAG);
      SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context,
                        put_blob_stream_context.result,
                        "Upload part request failed. Error code: %d, "
                        "message: %s",
                        upload_part_outcome.GetError().GetResponseCode(),
                        upload_part_outcome.GetError().GetMessage().c_str());
      AbortUpload(put_blob_stream_context, tracker);
      return;
    }
    completed_part.SetETag(etag);
    tracker->completed_multipart_upload.AddParts(move(completed_part));
    tracker->next_part_number++;
  }

  if (put_blob_stream_context.IsCancelled()) {
    put_blob_stream_context.result = FailureExecutionResult(
        SC_BLOB_STORAGE_PROVIDER_STREAM_SESSION_CANCELLED);
    SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context,
                      put_blob_stream_context.result,
                      "Put blob stream request was cancelled");
    AbortUpload(put_blob_stream_context, tracker);
    return;
  }
  // If there's no message, schedule again. If there's a message - write it.
  auto request = put_blob_stream_context.TryGetNextRequest();
  if (request == nullptr) {
    if (put_blob_stream_context.IsMarkedDone()) {
      CompleteUpload(put_blob_stream_context, tracker);
      return;
    }
    // If this session expired, cancel the upload and finish.
    if (TimeProvider::GetWallTimestampInNanoseconds() >=
        tracker->expiry_time_ns) {
      put_blob_stream_context.result = FailureExecutionResult(
          SC_BLOB_STORAGE_PROVIDER_STREAM_SESSION_EXPIRED);
      SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context,
                        put_blob_stream_context.result,
                        "Put blob stream session expired.");
      AbortUpload(put_blob_stream_context, tracker);
      return;
    }
    // Schedule checking for a new message.
    // Forward the old arguments to this callback so it knows that an upload was
    // not done.
    auto schedule_result = io_async_executor_->ScheduleFor(
        bind(&AwsS3ClientProvider::OnUploadPartCallback, this,
             put_blob_stream_context, tracker, s3_client, upload_part_request,
             upload_part_outcome, async_context),
        (TimeProvider::GetSteadyTimestampInNanoseconds() + kPutBlobRescanTime)
            .count());
    if (!schedule_result.Successful()) {
      put_blob_stream_context.result = schedule_result;
      SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context,
                        put_blob_stream_context.result,
                        "Put blob stream request failed to be scheduled");
      FinishStreamingContext(schedule_result, put_blob_stream_context,
                             cpu_async_executor_);
    }
    return;
  }
  // Validate that the new request specifies the same blob.
  if (request->blob_portion().metadata().bucket_name() !=
          tracker->bucket_name ||
      request->blob_portion().metadata().blob_name() != tracker->blob_name) {
    auto result = FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(kAwsS3Provider, put_blob_stream_context, result,
                      "Enqueued message does not specify the same blob (bucket "
                      "name, blob name) as previously.");
    FinishStreamingContext(result, put_blob_stream_context,
                           cpu_async_executor_);
    return;
  }

  // Upload the next part.
  UploadPartRequest new_upload_request;
  new_upload_request.SetBucket(tracker->bucket_name.c_str());
  new_upload_request.SetKey(tracker->blob_name.c_str());
  new_upload_request.SetPartNumber(tracker->next_part_number);
  new_upload_request.SetUploadId(tracker->upload_id.c_str());

  new_upload_request.SetBody(MakeShared<StringStream>(
      "WriteStream::Upload", request->blob_portion().data()));

  if (auto md5_result =
          SetContentMd5(put_blob_stream_context, new_upload_request,
                        request->blob_portion().data());
      !md5_result.Successful()) {
    put_blob_stream_context.result = md5_result;
    FinishStreamingContext(put_blob_stream_context.result,
                           put_blob_stream_context, cpu_async_executor_);
    return;
  }

  s3_client->UploadPartAsync(
      new_upload_request,
      bind(&AwsS3ClientProvider::OnUploadPartCallback, this,
           put_blob_stream_context, tracker, _1, _2, _3, _4),
      nullptr);
}

void AwsS3ClientProvider::CompleteUpload(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    shared_ptr<PutBlobStreamTracker> tracker) {
  CompleteMultipartUploadRequest complete_request;
  complete_request.SetBucket(tracker->bucket_name.c_str());
  complete_request.SetKey(tracker->blob_name.c_str());
  complete_request.SetUploadId(tracker->upload_id.c_str());
  complete_request.WithMultipartUpload(tracker->completed_multipart_upload);

  s3_client_->CompleteMultipartUploadAsync(
      complete_request,
      bind(&AwsS3ClientProvider::OnCompleteMultipartUploadCallback, this,
           put_blob_stream_context, _1, _2, _3, _4),
      nullptr);
}

void AwsS3ClientProvider::OnCompleteMultipartUploadCallback(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    const S3Client* s3_client,
    const CompleteMultipartUploadRequest& complete_multipart_upload_request,
    CompleteMultipartUploadOutcome complete_multipart_upload_outcome,
    const std::shared_ptr<const AsyncCallerContext> async_context) noexcept {
  put_blob_stream_context.result = SuccessExecutionResult();
  if (!complete_multipart_upload_outcome.IsSuccess()) {
    put_blob_stream_context.result =
        AwsS3Utils::ConvertS3ErrorToExecutionResult(
            complete_multipart_upload_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, put_blob_stream_context, put_blob_stream_context.result,
        "Complete multipart upload request failed. Error code: %d, "
        "message: %s",
        complete_multipart_upload_outcome.GetError().GetResponseCode(),
        complete_multipart_upload_outcome.GetError().GetMessage().c_str());
  }
  FinishStreamingContext(put_blob_stream_context.result,
                         put_blob_stream_context, cpu_async_executor_,
                         AsyncPriority::High);
}

void AwsS3ClientProvider::AbortUpload(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    shared_ptr<PutBlobStreamTracker> tracker) {
  AbortMultipartUploadRequest abort_request;
  abort_request.SetBucket(tracker->bucket_name.c_str());
  abort_request.SetKey(tracker->blob_name.c_str());
  abort_request.SetUploadId(tracker->upload_id.c_str());

  s3_client_->AbortMultipartUploadAsync(
      abort_request,
      bind(&AwsS3ClientProvider::OnAbortMultipartUploadCallback, this,
           put_blob_stream_context, _1, _2, _3, _4),
      nullptr);
}

void AwsS3ClientProvider::OnAbortMultipartUploadCallback(
    ProducerStreamingContext<PutBlobStreamRequest, PutBlobStreamResponse>&
        put_blob_stream_context,
    const S3Client* s3_client,
    const AbortMultipartUploadRequest& abort_multipart_upload_request,
    AbortMultipartUploadOutcome abort_multipart_upload_outcome,
    const std::shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!abort_multipart_upload_outcome.IsSuccess()) {
    auto abort_result = AwsS3Utils::ConvertS3ErrorToExecutionResult(
        abort_multipart_upload_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, put_blob_stream_context, abort_result,
        "Abort multipart upload request failed. Error code: %d, "
        "message: %s",
        abort_multipart_upload_outcome.GetError().GetResponseCode(),
        abort_multipart_upload_outcome.GetError().GetMessage().c_str());
  }
  FinishStreamingContext(put_blob_stream_context.result,
                         put_blob_stream_context, cpu_async_executor_,
                         AsyncPriority::High);
}

ExecutionResult AwsS3ClientProvider::DeleteBlob(
    AsyncContext<DeleteBlobRequest, DeleteBlobResponse>&
        delete_blob_context) noexcept {
  const auto& request = *delete_blob_context.request;
  if (request.blob_metadata().bucket_name().empty() ||
      request.blob_metadata().blob_name().empty()) {
    delete_blob_context.result =
        FailureExecutionResult(SC_BLOB_STORAGE_PROVIDER_INVALID_ARGS);
    SCP_ERROR_CONTEXT(
        kAwsS3Provider, delete_blob_context, delete_blob_context.result,
        "Delete blob request failed. Missing bucket or blob name.");
    delete_blob_context.Finish();
    return delete_blob_context.result;
  }
  String bucket_name(request.blob_metadata().bucket_name());
  String blob_name(request.blob_metadata().blob_name());

  DeleteObjectRequest delete_object_request;
  delete_object_request.SetBucket(bucket_name);
  delete_object_request.SetKey(blob_name);

  s3_client_->DeleteObjectAsync(
      delete_object_request,
      bind(&AwsS3ClientProvider::OnDeleteObjectCallback, this,
           delete_blob_context, _1, _2, _3, _4),
      nullptr);

  return SuccessExecutionResult();
}

void AwsS3ClientProvider::OnDeleteObjectCallback(
    AsyncContext<DeleteBlobRequest, DeleteBlobResponse>& delete_blob_context,
    const S3Client* s3_client, const DeleteObjectRequest& delete_object_request,
    DeleteObjectOutcome delete_object_outcome,
    const shared_ptr<const AsyncCallerContext> async_context) noexcept {
  if (!delete_object_outcome.IsSuccess()) {
    delete_blob_context.result = AwsS3Utils::ConvertS3ErrorToExecutionResult(
        delete_object_outcome.GetError().GetErrorType());
    SCP_ERROR_CONTEXT(kAwsS3Provider, delete_blob_context,
                      delete_blob_context.result,
                      "Delete blob request failed. Error code: %d, "
                      "message: %s",
                      delete_object_outcome.GetError().GetResponseCode(),
                      delete_object_outcome.GetError().GetMessage().c_str());
    FinishContext(delete_blob_context.result, delete_blob_context,
                  cpu_async_executor_, AsyncPriority::High);
    return;
  }
  delete_blob_context.response = make_shared<DeleteBlobResponse>();
  delete_blob_context.result = SuccessExecutionResult();
  FinishContext(delete_blob_context.result, delete_blob_context,
                cpu_async_executor_, AsyncPriority::High);
}

#ifndef TEST_CPIO
ExecutionResultOr<shared_ptr<S3Client>> AwsS3Factory::CreateClient(
    ClientConfiguration& client_config,
    const shared_ptr<AsyncExecutorInterface>& async_executor) noexcept {
  client_config.maxConnections = kMaxConcurrentConnections;
  client_config.executor = make_shared<AwsAsyncExecutor>(async_executor);

  return make_shared<S3Client>(client_config);
}

shared_ptr<BlobStorageClientProviderInterface>
BlobStorageClientProviderFactory::Create(
    shared_ptr<BlobStorageClientOptions> options,
    shared_ptr<InstanceClientProviderInterface> instance_client,
    const shared_ptr<core::AsyncExecutorInterface>& cpu_async_executor,
    const shared_ptr<core::AsyncExecutorInterface>&
        io_async_executor) noexcept {
  return make_shared<AwsS3ClientProvider>(
      options, instance_client, cpu_async_executor, io_async_executor);
}
#endif
}  // namespace google::scp::cpio::client_providers
