// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cc/cpio/client_providers/job_client_provider/src/job_client_provider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "cc/cpio/client_providers/job_client_provider/src/error_codes.h"
#include "cc/cpio/client_providers/job_client_provider/src/job_client_utils.h"
#include "core/async_executor/mock/mock_async_executor.h"
#include "core/test/utils/conditional_wait.h"
#include "core/utils/src/base64.h"
#include "cpio/client_providers/job_client_provider/test/hello_world.pb.h"
#include "cpio/client_providers/nosql_database_client_provider/mock/mock_nosql_database_client_provider.h"
#include "cpio/client_providers/nosql_database_client_provider/src/common/error_codes.h"
#include "cpio/client_providers/queue_client_provider/mock/mock_queue_client_provider.h"
#include "google/protobuf/util/time_util.h"
#include "public/core/interface/execution_result.h"
#include "public/core/test/interface/execution_result_matchers.h"
#include "public/cpio/proto/job_service/v1/job_service.pb.h"

using google::cmrt::sdk::job_service::v1::GetJobByIdRequest;
using google::cmrt::sdk::job_service::v1::GetJobByIdResponse;
using google::cmrt::sdk::job_service::v1::GetNextJobRequest;
using google::cmrt::sdk::job_service::v1::GetNextJobResponse;
using google::cmrt::sdk::job_service::v1::Job;
using google::cmrt::sdk::job_service::v1::JobStatus;
using google::cmrt::sdk::job_service::v1::PutJobRequest;
using google::cmrt::sdk::job_service::v1::PutJobResponse;
using google::cmrt::sdk::job_service::v1::UpdateJobBodyRequest;
using google::cmrt::sdk::job_service::v1::UpdateJobBodyResponse;
using google::cmrt::sdk::job_service::v1::UpdateJobStatusRequest;
using google::cmrt::sdk::job_service::v1::UpdateJobStatusResponse;
using google::cmrt::sdk::job_service::v1::UpdateJobVisibilityTimeoutRequest;
using google::cmrt::sdk::job_service::v1::UpdateJobVisibilityTimeoutResponse;
using google::cmrt::sdk::nosql_database_service::v1::GetDatabaseItemResponse;
using google::cmrt::sdk::nosql_database_service::v1::Item;
using google::cmrt::sdk::nosql_database_service::v1::UpsertDatabaseItemResponse;
using google::cmrt::sdk::queue_service::v1::DeleteMessageResponse;
using google::cmrt::sdk::queue_service::v1::EnqueueMessageResponse;
using google::cmrt::sdk::queue_service::v1::GetTopMessageResponse;
using google::cmrt::sdk::queue_service::v1::
    UpdateMessageVisibilityTimeoutResponse;
using google::protobuf::Any;
using google::protobuf::Duration;
using google::protobuf::util::TimeUtil;
using google::scp::core::AsyncContext;
using google::scp::core::ExecutionResult;
using google::scp::core::ExecutionStatus;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::async_executor::mock::MockAsyncExecutor;
using google::scp::core::errors::SC_CPIO_CLOUD_INVALID_ARGUMENT;
using google::scp::core::errors::SC_CPIO_INTERNAL_ERROR;
using google::scp::core::errors::SC_CPIO_INVALID_REQUEST;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_INVALID_DURATION;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_INVALID_JOB_ITEM;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_INVALID_JOB_STATUS;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_INVALID_RECEIPT_INFO;
using google::scp::core::errors::
    SC_JOB_CLIENT_PROVIDER_JOB_CLIENT_OPTIONS_REQUIRED;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID;
using google::scp::core::errors::SC_JOB_CLIENT_PROVIDER_UPDATION_CONFLICT;
using google::scp::core::errors::
    SC_NO_SQL_DATABASE_PROVIDER_INVALID_PARTITION_KEY_NAME;
using google::scp::core::errors::
    SC_NO_SQL_DATABASE_PROVIDER_INVALID_SORT_KEY_NAME;
using google::scp::core::errors::
    SC_NO_SQL_DATABASE_PROVIDER_JSON_FAILED_TO_PARSE;
using google::scp::core::errors::SC_NO_SQL_DATABASE_PROVIDER_RECORD_CORRUPTED;
using google::scp::core::errors::SC_NO_SQL_DATABASE_PROVIDER_RECORD_NOT_FOUND;
using google::scp::core::errors::SC_NO_SQL_DATABASE_PROVIDER_RETRIABLE_ERROR;
using google::scp::core::errors::SC_NO_SQL_DATABASE_PROVIDER_TABLE_NOT_FOUND;
using google::scp::core::errors::SC_NO_SQL_DATABASE_PROVIDER_UNRETRIABLE_ERROR;
using google::scp::core::test::IsSuccessful;
using google::scp::core::test::ResultIs;
using google::scp::core::test::WaitUntil;
using google::scp::core::utils::Base64Encode;
using google::scp::cpio::client_providers::mock::
    MockNoSQLDatabaseClientProvider;
using google::scp::cpio::client_providers::mock::MockQueueClientProvider;
using helloworld::HelloWorld;
using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using testing::Eq;
using testing::Ne;
using testing::NiceMock;

namespace {
constexpr char kHelloWorldName[] = "hello world";
constexpr int kHelloWorldId = 42356441;
const google::protobuf::Timestamp kHelloWorldProtoCreatedTime =
    TimeUtil::SecondsToTimestamp(1672531200);

constexpr char kQueueMessageId[] = "message-id";
constexpr char kQueueMessageReceiptInfo[] = "receipt-info";
constexpr char kJobId[] = "job-id";
constexpr char kDefaultTimestampValueInString[] = "0";
const Duration kDefaultVisibilityTimeout = TimeUtil::SecondsToDuration(30);
const Duration kUpdatedVisibilityTimeout = TimeUtil::SecondsToDuration(90);
const Duration kExceededVisibilityTimeout = TimeUtil::SecondsToDuration(1000);
const Duration kNegativeVisibilityTimeout = TimeUtil::SecondsToDuration(-20);

constexpr char kJobsTableName[] = "jobs";
constexpr char kJobsTablePartitionKeyName[] = "job_id";
constexpr char kJobBodyColumnName[] = "job_body";
constexpr char kJobStatusColumnName[] = "job_status";
constexpr char kCreatedTimeColumnName[] = "created_time";
constexpr char kUpdatedTimeColumnName[] = "updated_time";
constexpr char kVisibilityTimeoutColumnName[] = "visibility_timeout";
const google::protobuf::Timestamp kCreatedTime =
    TimeUtil::SecondsToTimestamp(1680709200);
const google::protobuf::Timestamp kLastUpdatedTime =
    TimeUtil::SecondsToTimestamp(1680739200);
const google::protobuf::Timestamp kStaleUpdatedTime =
    TimeUtil::SecondsToTimestamp(946684800);

Any CreateHelloWorldProtoAsAny() {
  HelloWorld hello_world_input;
  hello_world_input.set_name(kHelloWorldName);
  hello_world_input.set_id(kHelloWorldId);
  *hello_world_input.mutable_created_time() = kHelloWorldProtoCreatedTime;

  Any job_body;
  job_body.PackFrom(hello_world_input);
  return job_body;
}

Item CreateJobAsDatabaseItem(
    const Any& job_body, const JobStatus& job_status,
    const google::protobuf::Timestamp& current_time,
    const google::protobuf::Timestamp& updated_time,
    const google::protobuf::Timestamp& visibility_timeout) {
  auto job_body_in_string_or = google::scp::cpio::client_providers::
      JobClientUtils::ConvertAnyToBase64String(job_body);
  Item item;
  *item.mutable_key()->mutable_partition_key() =
      google::scp::cpio::client_providers::JobClientUtils::MakeStringAttribute(
          kJobsTablePartitionKeyName, kJobId);

  *item.add_attributes() =
      google::scp::cpio::client_providers::JobClientUtils::MakeStringAttribute(
          kJobBodyColumnName, *job_body_in_string_or);
  *item.add_attributes() =
      google::scp::cpio::client_providers::JobClientUtils::MakeIntAttribute(
          kJobStatusColumnName, job_status);
  *item.add_attributes() =
      google::scp::cpio::client_providers::JobClientUtils::MakeStringAttribute(
          kCreatedTimeColumnName, TimeUtil::ToString(current_time));
  *item.add_attributes() =
      google::scp::cpio::client_providers::JobClientUtils::MakeStringAttribute(
          kUpdatedTimeColumnName, TimeUtil::ToString(updated_time));
  *item.add_attributes() =
      google::scp::cpio::client_providers::JobClientUtils::MakeStringAttribute(
          kVisibilityTimeoutColumnName, TimeUtil::ToString(visibility_timeout));
  return item;
}
}  // namespace

namespace google::scp::cpio::client_providers::job_client::test {

class JobClientProviderTest : public ::testing::TestWithParam<Duration> {
 protected:
  JobClientProviderTest() {
    job_client_options_ = make_shared<JobClientOptions>();
    job_client_options_->job_table_name = kJobsTableName;
    mock_async_executor_ = make_shared<MockAsyncExecutor>();
    queue_client_provider_ = make_shared<NiceMock<MockQueueClientProvider>>();
    nosql_database_client_provider_ =
        make_shared<NiceMock<MockNoSQLDatabaseClientProvider>>();

    job_client_provider_ = make_unique<JobClientProvider>(
        job_client_options_, queue_client_provider_,
        nosql_database_client_provider_, mock_async_executor_);

    put_job_context_.request = make_shared<PutJobRequest>();
    put_job_context_.callback = [this](auto) { finish_called_ = true; };

    get_next_job_context_.request = make_shared<GetNextJobRequest>();
    get_next_job_context_.callback = [this](auto) { finish_called_ = true; };

    get_job_by_id_context_.request = make_shared<GetJobByIdRequest>();
    get_job_by_id_context_.callback = [this](auto) { finish_called_ = true; };

    update_job_body_context_.request = make_shared<UpdateJobBodyRequest>();
    update_job_body_context_.callback = [this](auto) { finish_called_ = true; };

    update_job_status_context_.request = make_shared<UpdateJobStatusRequest>();
    update_job_status_context_.callback = [this](auto) {
      finish_called_ = true;
    };

    update_job_visibility_timeout_context_.request =
        make_shared<UpdateJobVisibilityTimeoutRequest>();
    update_job_visibility_timeout_context_.callback = [this](auto) {
      finish_called_ = true;
    };
  }

  void TearDown() override { EXPECT_SUCCESS(job_client_provider_->Stop()); }

  shared_ptr<JobClientOptions> job_client_options_;
  shared_ptr<MockAsyncExecutor> mock_async_executor_;
  shared_ptr<MockQueueClientProvider> queue_client_provider_;
  shared_ptr<MockNoSQLDatabaseClientProvider> nosql_database_client_provider_;
  unique_ptr<JobClientProvider> job_client_provider_;

  AsyncContext<PutJobRequest, PutJobResponse> put_job_context_;

  AsyncContext<GetNextJobRequest, GetNextJobResponse> get_next_job_context_;

  AsyncContext<GetJobByIdRequest, GetJobByIdResponse> get_job_by_id_context_;

  AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>
      update_job_body_context_;

  AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>
      update_job_status_context_;

  AsyncContext<UpdateJobVisibilityTimeoutRequest,
               UpdateJobVisibilityTimeoutResponse>
      update_job_visibility_timeout_context_;

  // We check that this gets flipped after every call to ensure the context's
  // Finish() is called.
  std::atomic_bool finish_called_{false};
};

TEST_F(JobClientProviderTest, InitWithNullJobClientOptions) {
  auto client = make_unique<JobClientProvider>(nullptr, queue_client_provider_,
                                               nosql_database_client_provider_,
                                               mock_async_executor_);

  EXPECT_THAT(client->Init(),
              ResultIs(FailureExecutionResult(
                  SC_JOB_CLIENT_PROVIDER_JOB_CLIENT_OPTIONS_REQUIRED)));
}

MATCHER_P6(HasUpsertItemParamsForJobCreations, table_name, job_body_as_string,
           job_status_in_int, job_created_time_default_value,
           job_updated_time_default_value, job_visibility_timeout_default_value,
           "") {
  return ExplainMatchResult(Eq(table_name),
                            arg.request->mutable_key()->table_name(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_body_as_string),
                            arg.request->new_attributes(0).value_string(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_status_in_int),
                            arg.request->new_attributes(1).value_int(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_created_time_default_value),
                            arg.request->new_attributes(2).value_string(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_updated_time_default_value),
                            arg.request->new_attributes(3).value_string(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_visibility_timeout_default_value),
                            arg.request->new_attributes(4).value_string(),
                            result_listener);
}

TEST_F(JobClientProviderTest, PutJobSuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  string job_id_in_request;
  EXPECT_CALL(*queue_client_provider_, EnqueueMessage)
      .WillOnce([&job_id_in_request](auto& enqueue_message_context) {
        job_id_in_request = enqueue_message_context.request->message_body();
        enqueue_message_context.response =
            make_shared<EnqueueMessageResponse>();
        enqueue_message_context.response->set_message_id(kQueueMessageId);
        enqueue_message_context.result = SuccessExecutionResult();
        enqueue_message_context.Finish();
        return SuccessExecutionResult();
      });

  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto encoded_job_body_or =
      JobClientUtils::ConvertAnyToBase64String(job_body_input);

  google::protobuf::Timestamp job_created_time_in_request;
  EXPECT_CALL(
      *nosql_database_client_provider_,
      UpsertDatabaseItem(HasUpsertItemParamsForJobCreations(
          kJobsTableName, *encoded_job_body_or, JobStatus::JOB_STATUS_CREATED,
          kDefaultTimestampValueInString, kDefaultTimestampValueInString,
          kDefaultTimestampValueInString)))
      .WillOnce(
          [&job_created_time_in_request](auto& upsert_database_item_context) {
            auto created_time_in_string =
                upsert_database_item_context.request->new_attributes(2)
                    .value_string();
            TimeUtil::FromString(created_time_in_string,
                                 &job_created_time_in_request);
            upsert_database_item_context.response =
                make_shared<UpsertDatabaseItemResponse>();
            upsert_database_item_context.result = SuccessExecutionResult();
            upsert_database_item_context.Finish();
            return SuccessExecutionResult();
          });

  *put_job_context_.request->mutable_job_body() = job_body_input;
  Job job_output;
  put_job_context_.callback =
      [this, &job_id_in_request, &job_created_time_in_request](
          AsyncContext<PutJobRequest, PutJobResponse>& put_job_context) {
        EXPECT_SUCCESS(put_job_context.result);
        auto job_output = put_job_context.response->job();

        EXPECT_EQ(job_output.job_id(), job_id_in_request);

        auto job_body_output = job_output.job_body();
        HelloWorld hello_world_output;
        job_body_output.UnpackTo(&hello_world_output);
        EXPECT_EQ(hello_world_output.name(), kHelloWorldName);
        EXPECT_EQ(hello_world_output.id(), kHelloWorldId);
        EXPECT_EQ(hello_world_output.created_time(),
                  kHelloWorldProtoCreatedTime);

        EXPECT_EQ(job_output.job_status(), JobStatus::JOB_STATUS_CREATED);
        EXPECT_EQ(job_output.created_time(), job_created_time_in_request);
        EXPECT_EQ(job_output.updated_time(), job_created_time_in_request);
        EXPECT_EQ(job_output.visibility_timeout(),
                  job_created_time_in_request + kDefaultVisibilityTimeout);
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->PutJob(put_job_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, PutJobWithEnqueueMessageFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*queue_client_provider_, EnqueueMessage)
      .WillOnce([](auto& enqueue_message_context) {
        enqueue_message_context.result =
            FailureExecutionResult(SC_CPIO_INTERNAL_ERROR);
        enqueue_message_context.Finish();
        return enqueue_message_context.result;
      });

  put_job_context_.callback =
      [this](AsyncContext<PutJobRequest, PutJobResponse>& put_job_context) {
        EXPECT_THAT(put_job_context.result,
                    ResultIs(FailureExecutionResult(SC_CPIO_INTERNAL_ERROR)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->PutJob(put_job_context_),
              ResultIs(FailureExecutionResult(SC_CPIO_INTERNAL_ERROR)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, PutJobWithUpsertDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*queue_client_provider_, EnqueueMessage)
      .WillOnce([](auto& enqueue_message_context) {
        enqueue_message_context.response =
            make_shared<EnqueueMessageResponse>();
        enqueue_message_context.response->set_message_id(kQueueMessageId);
        enqueue_message_context.result = SuccessExecutionResult();
        enqueue_message_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_, UpsertDatabaseItem)
      .WillOnce([](auto& upsert_database_item_context) {
        upsert_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_RECORD_NOT_FOUND);
        upsert_database_item_context.Finish();
        return upsert_database_item_context.result;
      });

  auto job_body_input = CreateHelloWorldProtoAsAny();
  *put_job_context_.request->mutable_job_body() = job_body_input;
  put_job_context_.callback =
      [this](AsyncContext<PutJobRequest, PutJobResponse>& put_job_context) {
        EXPECT_THAT(put_job_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_RECORD_NOT_FOUND)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->PutJob(put_job_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

MATCHER_P2(HasGetDatabaseItemParams, table_name, job_id, "") {
  return ExplainMatchResult(Eq(table_name),
                            arg.request->mutable_key()->table_name(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_id),
                            arg.request->mutable_key()
                                ->mutable_partition_key()
                                ->value_string(),
                            result_listener);
}

TEST_F(JobClientProviderTest, GetNextJobSuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*queue_client_provider_, GetTopMessage)
      .WillOnce([](auto& get_top_message_context) {
        get_top_message_context.response = make_shared<GetTopMessageResponse>();
        get_top_message_context.response->set_message_id(kQueueMessageId);
        get_top_message_context.response->set_message_body(kJobId);
        get_top_message_context.response->set_receipt_info(
            kQueueMessageReceiptInfo);
        get_top_message_context.result = SuccessExecutionResult();
        get_top_message_context.Finish();
        return SuccessExecutionResult();
      });

  auto created_time = TimeUtil::GetCurrentTime();
  auto updated_time = created_time;
  auto visibility_timeout = created_time + TimeUtil::SecondsToDuration(30);
  auto job_body = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  get_next_job_context_.callback =
      [this, &created_time, &updated_time,
       &visibility_timeout](AsyncContext<GetNextJobRequest, GetNextJobResponse>&
                                get_next_job_context) {
        EXPECT_SUCCESS(get_next_job_context.result);
        auto job_output = get_next_job_context.response->job();
        EXPECT_EQ(job_output.job_id(), kJobId);

        auto job_body_output = job_output.job_body();
        HelloWorld hello_world_output;
        job_body_output.UnpackTo(&hello_world_output);
        EXPECT_EQ(hello_world_output.name(), kHelloWorldName);
        EXPECT_EQ(hello_world_output.id(), kHelloWorldId);
        EXPECT_EQ(hello_world_output.created_time(),
                  kHelloWorldProtoCreatedTime);

        EXPECT_EQ(job_output.job_status(), JobStatus::JOB_STATUS_CREATED);
        EXPECT_EQ(job_output.created_time(), created_time);
        EXPECT_EQ(job_output.updated_time(), updated_time);
        EXPECT_EQ(job_output.visibility_timeout(), visibility_timeout);

        EXPECT_EQ(get_next_job_context.response->receipt_info(),
                  kQueueMessageReceiptInfo);

        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->GetNextJob(get_next_job_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetNextJobWithGetTopMessageFailure) {
  EXPECT_CALL(*queue_client_provider_, GetTopMessage)
      .WillOnce([](auto& get_top_message_context) {
        get_top_message_context.result =
            FailureExecutionResult(SC_CPIO_INTERNAL_ERROR);
        get_top_message_context.Finish();
        return get_top_message_context.result;
      });

  get_next_job_context_.callback =
      [this](AsyncContext<GetNextJobRequest, GetNextJobResponse>&
                 get_next_job_context) {
        EXPECT_THAT(get_next_job_context.result,
                    ResultIs(FailureExecutionResult(SC_CPIO_INTERNAL_ERROR)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->GetNextJob(get_next_job_context_),
              ResultIs(FailureExecutionResult(SC_CPIO_INTERNAL_ERROR)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetNextJobWithGetDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*queue_client_provider_, GetTopMessage)
      .WillOnce([](auto& get_top_message_context) {
        get_top_message_context.response = make_shared<GetTopMessageResponse>();
        get_top_message_context.response->set_message_id(kQueueMessageId);
        get_top_message_context.response->set_message_body(kJobId);
        get_top_message_context.response->set_receipt_info(
            kQueueMessageReceiptInfo);
        get_top_message_context.result = SuccessExecutionResult();
        get_top_message_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_, GetDatabaseItem)
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_RECORD_NOT_FOUND);
        get_database_item_context.Finish();
        return get_database_item_context.result;
      });

  get_next_job_context_.callback =
      [this](AsyncContext<GetNextJobRequest, GetNextJobResponse>&
                 get_next_job_context) {
        EXPECT_THAT(get_next_job_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_RECORD_NOT_FOUND)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->GetNextJob(get_next_job_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetNextJobWithInvalidDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*queue_client_provider_, GetTopMessage)
      .WillOnce([](auto& get_top_message_context) {
        get_top_message_context.response = make_shared<GetTopMessageResponse>();
        get_top_message_context.response->set_message_id(kQueueMessageId);
        get_top_message_context.response->set_message_body(kJobId);
        get_top_message_context.response->set_receipt_info(
            kQueueMessageReceiptInfo);
        get_top_message_context.result = SuccessExecutionResult();
        get_top_message_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  get_next_job_context_.callback =
      [this](AsyncContext<GetNextJobRequest, GetNextJobResponse>&
                 get_next_job_context) {
        EXPECT_THAT(get_next_job_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_JOB_ITEM)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->GetNextJob(get_next_job_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetJobById) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = TimeUtil::GetCurrentTime();
  auto updated_time = created_time;
  auto visibility_timeout = created_time + TimeUtil::SecondsToDuration(30);
  auto job_body = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  get_job_by_id_context_.request->set_job_id(kJobId);
  get_job_by_id_context_.callback =
      [this, &created_time, &updated_time,
       &visibility_timeout](AsyncContext<GetJobByIdRequest, GetJobByIdResponse>&
                                get_job_by_id_context) {
        EXPECT_SUCCESS(get_job_by_id_context.result);
        auto job_output = get_job_by_id_context.response->job();
        EXPECT_EQ(job_output.job_id(), kJobId);

        auto job_body_output = job_output.job_body();
        HelloWorld hello_world_output;
        job_body_output.UnpackTo(&hello_world_output);
        EXPECT_EQ(hello_world_output.name(), kHelloWorldName);
        EXPECT_EQ(hello_world_output.id(), kHelloWorldId);
        EXPECT_EQ(hello_world_output.created_time(),
                  kHelloWorldProtoCreatedTime);

        EXPECT_EQ(job_output.job_status(), JobStatus::JOB_STATUS_CREATED);
        EXPECT_EQ(job_output.created_time(), created_time);
        EXPECT_EQ(job_output.updated_time(), updated_time);
        EXPECT_EQ(job_output.visibility_timeout(), visibility_timeout);

        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->GetJobById(get_job_by_id_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetJobByIdWithMissingJobIdFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  get_job_by_id_context_.callback =
      [this](AsyncContext<GetJobByIdRequest, GetJobByIdResponse>&
                 get_job_by_id_context) {
        EXPECT_THAT(get_job_by_id_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));
        finish_called_ = true;
      };

  EXPECT_THAT(
      job_client_provider_->GetJobById(get_job_by_id_context_),
      ResultIs(FailureExecutionResult(SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetJobByIdWithGetDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = TimeUtil::GetCurrentTime();
  auto updated_time = created_time;
  auto visibility_timeout = created_time + TimeUtil::SecondsToDuration(30);
  auto job_body = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_UNRETRIABLE_ERROR);
        get_database_item_context.Finish();
        return get_database_item_context.result;
      });

  get_job_by_id_context_.request->set_job_id(kJobId);
  get_job_by_id_context_.callback =
      [this](AsyncContext<GetJobByIdRequest, GetJobByIdResponse>&
                 get_job_by_id_context) {
        EXPECT_THAT(get_job_by_id_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_UNRETRIABLE_ERROR)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->GetJobById(get_job_by_id_context_),
              ResultIs(FailureExecutionResult(
                  SC_NO_SQL_DATABASE_PROVIDER_UNRETRIABLE_ERROR)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, GetJobByIdWithInvalidDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  get_job_by_id_context_.request->set_job_id(kJobId);
  get_job_by_id_context_.callback =
      [this](AsyncContext<GetJobByIdRequest, GetJobByIdResponse>&
                 get_job_by_id_context) {
        EXPECT_THAT(get_job_by_id_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_JOB_ITEM)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->GetJobById(get_job_by_id_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

MATCHER_P3(HasUpsertItemParamsForJobBodyUpdates, table_name, job_body_as_string,
           job_updated_time_default_value, "") {
  return ExplainMatchResult(Eq(table_name),
                            arg.request->mutable_key()->table_name(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_body_as_string),
                            arg.request->new_attributes(0).value_string(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_updated_time_default_value),
                            arg.request->new_attributes(1).value_string(),
                            result_listener);
}

TEST_F(JobClientProviderTest, UpdateJobBodySuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto encoded_job_body_or =
      JobClientUtils::ConvertAnyToBase64String(job_body_input);
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  google::protobuf::Timestamp job_updated_time_in_request;
  EXPECT_CALL(*nosql_database_client_provider_,
              UpsertDatabaseItem(HasUpsertItemParamsForJobBodyUpdates(
                  kJobsTableName, *encoded_job_body_or,
                  kDefaultTimestampValueInString)))
      .WillOnce(
          [&job_updated_time_in_request](auto& upsert_database_item_context) {
            auto updated_time_in_string =
                upsert_database_item_context.request->new_attributes(1)
                    .value_string();
            TimeUtil::FromString(updated_time_in_string,
                                 &job_updated_time_in_request);
            upsert_database_item_context.response =
                make_shared<UpsertDatabaseItemResponse>();
            upsert_database_item_context.result = SuccessExecutionResult();
            upsert_database_item_context.Finish();
            return SuccessExecutionResult();
          });

  update_job_body_context_.request->set_job_id(kJobId);
  *update_job_body_context_.request->mutable_job_body() = job_body_input;
  *update_job_body_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  update_job_body_context_.callback =
      [this, &job_updated_time_in_request](
          AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
              update_job_body_context) {
        EXPECT_SUCCESS(update_job_body_context.result);
        EXPECT_EQ(update_job_body_context.response->updated_time(),
                  job_updated_time_in_request);
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobBody(update_job_body_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobBodyWithMissingJobIdFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  update_job_body_context_.callback =
      [this](AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
                 update_job_body_context) {
        EXPECT_THAT(update_job_body_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));
        finish_called_ = true;
      };

  EXPECT_THAT(
      job_client_provider_->UpdateJobBody(update_job_body_context_),
      ResultIs(FailureExecutionResult(SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobBodyWithGetDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_JSON_FAILED_TO_PARSE);
        get_database_item_context.Finish();
        return get_database_item_context.result;
      });

  update_job_body_context_.request->set_job_id(kJobId);
  *update_job_body_context_.request->mutable_job_body() =
      CreateHelloWorldProtoAsAny();
  update_job_body_context_.callback =
      [this](AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
                 update_job_body_context) {
        EXPECT_THAT(update_job_body_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_JSON_FAILED_TO_PARSE)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->UpdateJobBody(update_job_body_context_),
              ResultIs(FailureExecutionResult(
                  SC_NO_SQL_DATABASE_PROVIDER_JSON_FAILED_TO_PARSE)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobBodyWithInvalidDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto encoded_job_body_or =
      JobClientUtils::ConvertAnyToBase64String(job_body_input);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  update_job_body_context_.request->set_job_id(kJobId);
  *update_job_body_context_.request->mutable_job_body() = job_body_input;
  update_job_body_context_.callback =
      [this](AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
                 update_job_body_context) {
        EXPECT_THAT(update_job_body_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_JOB_ITEM)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobBody(update_job_body_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobBodyWithRequestConflictsFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  update_job_body_context_.request->set_job_id(kJobId);
  *update_job_body_context_.request->mutable_job_body() = job_body_input;
  *update_job_body_context_.request->mutable_most_recent_updated_time() =
      kStaleUpdatedTime;
  update_job_body_context_.callback =
      [this](AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
                 update_job_body_context) {
        EXPECT_THAT(update_job_body_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_UPDATION_CONFLICT)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobBody(update_job_body_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobBodyWithUpsertDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto encoded_job_body_or =
      JobClientUtils::ConvertAnyToBase64String(job_body_input);
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_,
              UpsertDatabaseItem(HasUpsertItemParamsForJobBodyUpdates(
                  kJobsTableName, *encoded_job_body_or,
                  kDefaultTimestampValueInString)))
      .WillOnce([](auto& upsert_database_item_context) {
        upsert_database_item_context.response =
            make_shared<UpsertDatabaseItemResponse>();
        upsert_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_RECORD_CORRUPTED);
        upsert_database_item_context.Finish();
        return upsert_database_item_context.result;
      });

  update_job_body_context_.request->set_job_id(kJobId);
  *update_job_body_context_.request->mutable_job_body() = job_body_input;
  *update_job_body_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  update_job_body_context_.callback =
      [this](AsyncContext<UpdateJobBodyRequest, UpdateJobBodyResponse>&
                 update_job_body_context) {
        EXPECT_THAT(update_job_body_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_RECORD_CORRUPTED)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobBody(update_job_body_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

MATCHER_P(HasReceiptInfo, receipt_info, "") {
  return ExplainMatchResult(Eq(receipt_info), arg.request->receipt_info(),
                            result_listener);
}

MATCHER_P4(HasUpsertItemParamsForJobStatusUpdates, table_name, job_id,
           job_status, job_updated_time_default_value, "") {
  return ExplainMatchResult(Eq(table_name),
                            arg.request->mutable_key()->table_name(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_id),
                            arg.request->mutable_key()
                                ->mutable_partition_key()
                                ->value_string(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_status),
                            arg.request->new_attributes(0).value_int(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_updated_time_default_value),
                            arg.request->new_attributes(1).value_string(),
                            result_listener);
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithJobDeletionSuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_,
              DeleteMessage(HasReceiptInfo(kQueueMessageReceiptInfo)))
      .WillOnce([](auto& delete_message_context) {
        delete_message_context.response = make_shared<DeleteMessageResponse>();
        delete_message_context.result = SuccessExecutionResult();
        delete_message_context.Finish();
        return SuccessExecutionResult();
      });

  google::protobuf::Timestamp job_updated_time_in_request;
  EXPECT_CALL(*nosql_database_client_provider_,
              UpsertDatabaseItem(HasUpsertItemParamsForJobStatusUpdates(
                  kJobsTableName, kJobId, JobStatus::JOB_STATUS_SUCCESS,
                  kDefaultTimestampValueInString)))
      .WillOnce(
          [&job_updated_time_in_request](auto& upsert_database_item_context) {
            auto updated_time_in_string =
                upsert_database_item_context.request->new_attributes(1)
                    .value_string();
            TimeUtil::FromString(updated_time_in_string,
                                 &job_updated_time_in_request);
            upsert_database_item_context.response =
                make_shared<UpsertDatabaseItemResponse>();
            upsert_database_item_context.result = SuccessExecutionResult();
            upsert_database_item_context.Finish();
            return SuccessExecutionResult();
          });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_SUCCESS);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  *update_job_status_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  update_job_status_context_.callback =
      [this, &job_updated_time_in_request](
          AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
              update_job_status_context) {
        EXPECT_SUCCESS(update_job_status_context.result);
        EXPECT_EQ(update_job_status_context.response->updated_time(),
                  job_updated_time_in_request);
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithNoJobDeletionSuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  google::protobuf::Timestamp job_updated_time_in_request;
  EXPECT_CALL(*nosql_database_client_provider_,
              UpsertDatabaseItem(HasUpsertItemParamsForJobStatusUpdates(
                  kJobsTableName, kJobId, JobStatus::JOB_STATUS_PROCESSING,
                  kDefaultTimestampValueInString)))
      .WillOnce(
          [&job_updated_time_in_request](auto& upsert_database_item_context) {
            auto updated_time_in_string =
                upsert_database_item_context.request->new_attributes(1)
                    .value_string();
            TimeUtil::FromString(updated_time_in_string,
                                 &job_updated_time_in_request);
            upsert_database_item_context.response =
                make_shared<UpsertDatabaseItemResponse>();
            upsert_database_item_context.result = SuccessExecutionResult();
            upsert_database_item_context.Finish();
            return SuccessExecutionResult();
          });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_PROCESSING);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  update_job_status_context_.callback =
      [this, &job_updated_time_in_request](
          AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
              update_job_status_context) {
        EXPECT_SUCCESS(update_job_status_context.result);
        EXPECT_EQ(update_job_status_context.response->updated_time(),
                  job_updated_time_in_request);
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithMissingJobIdFailed) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_FAILURE);
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(update_job_status_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));
        finish_called_ = true;
      };

  EXPECT_THAT(
      job_client_provider_->UpdateJobStatus(update_job_status_context_),
      ResultIs(FailureExecutionResult(SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobStatusWithJobStatusSuccessMissingReceiptInfoFailed) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_SUCCESS);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(update_job_status_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_RECEIPT_INFO)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->UpdateJobStatus(update_job_status_context_),
              ResultIs(FailureExecutionResult(
                  SC_JOB_CLIENT_PROVIDER_INVALID_RECEIPT_INFO)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithDeleteMessageFailed) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_, DeleteMessage)
      .WillOnce([](auto& delete_message_context) {
        delete_message_context.result =
            FailureExecutionResult(SC_CPIO_CLOUD_INVALID_ARGUMENT);
        delete_message_context.Finish();
        return delete_message_context.result;
      });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_FAILURE);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  *update_job_status_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(
            update_job_status_context.result,
            ResultIs(FailureExecutionResult(SC_CPIO_CLOUD_INVALID_ARGUMENT)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithUpsertDatabaseItemFailed) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_,
              DeleteMessage(HasReceiptInfo(kQueueMessageReceiptInfo)))
      .WillOnce([](auto& delete_message_context) {
        delete_message_context.response = make_shared<DeleteMessageResponse>();
        delete_message_context.result = SuccessExecutionResult();
        delete_message_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_, UpsertDatabaseItem)
      .WillOnce([](auto& upsert_database_item_context) {
        upsert_database_item_context.result =
            FailureExecutionResult(SC_NO_SQL_DATABASE_PROVIDER_TABLE_NOT_FOUND);
        upsert_database_item_context.Finish();
        return upsert_database_item_context.result;
      });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_SUCCESS);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  *update_job_status_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(update_job_status_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_TABLE_NOT_FOUND)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithInvalidJobStatusFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_UNKNOWN);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kLastUpdatedTime;
  *update_job_status_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(update_job_status_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_JOB_STATUS)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest, UpdateJobStatusWithRequestConflictsFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  *update_job_status_context_.request->mutable_job_id() = kJobId;
  update_job_status_context_.request->set_job_status(
      JobStatus::JOB_STATUS_PROCESSING);
  *update_job_status_context_.request->mutable_most_recent_updated_time() =
      kStaleUpdatedTime;
  *update_job_status_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  update_job_status_context_.callback =
      [this](AsyncContext<UpdateJobStatusRequest, UpdateJobStatusResponse>&
                 update_job_status_context) {
        EXPECT_THAT(update_job_status_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_UPDATION_CONFLICT)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(
      job_client_provider_->UpdateJobStatus(update_job_status_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

MATCHER_P2(HasMessageVisibilityTimeoutParams, receipt_info,
           message_visibility_timeout_in_seconds, "") {
  return ExplainMatchResult(Eq(receipt_info), arg.request->receipt_info(),
                            result_listener);
}

MATCHER_P4(HasUpsertItemParamsForJobVisibilityTimeoutUpdates, table_name,
           job_id, job_updated_time_default_value,
           job_visibility_timeout_default_value, "") {
  return ExplainMatchResult(Eq(table_name),
                            arg.request->mutable_key()->table_name(),
                            result_listener) &&
         ExplainMatchResult(Eq(job_id),
                            arg.request->mutable_key()
                                ->mutable_partition_key()
                                ->value_string(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_updated_time_default_value),
                            arg.request->new_attributes(0).value_string(),
                            result_listener) &&
         ExplainMatchResult(Ne(job_visibility_timeout_default_value),
                            arg.request->new_attributes(1).value_string(),
                            result_listener);
}

TEST_F(JobClientProviderTest, UpdateJobVisibilityTimeoutSuccess) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_,
              UpdateMessageVisibilityTimeout(HasMessageVisibilityTimeoutParams(
                  kQueueMessageReceiptInfo,
                  TimeUtil::DurationToSeconds(kUpdatedVisibilityTimeout))))
      .WillOnce([](auto& update_message_visibility_timeout_context) {
        update_message_visibility_timeout_context.response =
            make_shared<UpdateMessageVisibilityTimeoutResponse>();
        update_message_visibility_timeout_context.result =
            SuccessExecutionResult();
        update_message_visibility_timeout_context.Finish();
        return SuccessExecutionResult();
      });

  google::protobuf::Timestamp job_updated_time_in_request;
  EXPECT_CALL(
      *nosql_database_client_provider_,
      UpsertDatabaseItem(HasUpsertItemParamsForJobVisibilityTimeoutUpdates(
          kJobsTableName, kJobId, kDefaultTimestampValueInString,
          kDefaultTimestampValueInString)))
      .WillOnce([&job_updated_time_in_request](
                    auto& upsert_database_item_context) {
        auto updated_time_in_string =
            upsert_database_item_context.request->new_attributes(0)
                .value_string();
        TimeUtil::FromString(updated_time_in_string,
                             &job_updated_time_in_request);
        auto visibility_timeout_in_string =
            upsert_database_item_context.request->new_attributes(1)
                .value_string();
        google::protobuf::Timestamp visibility_timeout;
        TimeUtil::FromString(visibility_timeout_in_string, &visibility_timeout);
        EXPECT_EQ(job_updated_time_in_request + kUpdatedVisibilityTimeout,
                  visibility_timeout);
        upsert_database_item_context.response =
            make_shared<UpsertDatabaseItemResponse>();
        upsert_database_item_context.result = SuccessExecutionResult();
        upsert_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this, &job_updated_time_in_request](
          AsyncContext<UpdateJobVisibilityTimeoutRequest,
                       UpdateJobVisibilityTimeoutResponse>&
              update_job_visibility_timeout_context) {
        EXPECT_SUCCESS(update_job_visibility_timeout_context.result);
        EXPECT_EQ(
            update_job_visibility_timeout_context.response->updated_time(),
            job_updated_time_in_request);
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobVisibilityTimeout(
      update_job_visibility_timeout_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithMissingJobIdFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));
        finish_called_ = true;
      };

  EXPECT_THAT(
      job_client_provider_->UpdateJobVisibilityTimeout(
          update_job_visibility_timeout_context_),
      ResultIs(FailureExecutionResult(SC_JOB_CLIENT_PROVIDER_MISSING_JOB_ID)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithMissingReceiptInfoFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_RECEIPT_INFO)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->UpdateJobVisibilityTimeout(
                  update_job_visibility_timeout_context_),
              ResultIs(FailureExecutionResult(
                  SC_JOB_CLIENT_PROVIDER_INVALID_RECEIPT_INFO)));

  WaitUntil([this]() { return finish_called_.load(); });
}

INSTANTIATE_TEST_SUITE_P(InvalidDurations, JobClientProviderTest,
                         testing::Values(kExceededVisibilityTimeout,
                                         kNegativeVisibilityTimeout));

TEST_P(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithInvalidDurationFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = GetParam();
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_DURATION)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->UpdateJobVisibilityTimeout(
                  update_job_visibility_timeout_context_),
              ResultIs(FailureExecutionResult(
                  SC_JOB_CLIENT_PROVIDER_INVALID_DURATION)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithGetDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_INVALID_SORT_KEY_NAME);
        get_database_item_context.Finish();
        return get_database_item_context.result;
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_NO_SQL_DATABASE_PROVIDER_INVALID_SORT_KEY_NAME)));
        finish_called_ = true;
      };

  EXPECT_THAT(job_client_provider_->UpdateJobVisibilityTimeout(
                  update_job_visibility_timeout_context_),
              ResultIs(FailureExecutionResult(
                  SC_NO_SQL_DATABASE_PROVIDER_INVALID_SORT_KEY_NAME)));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithInvalidDatabaseItemFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_INVALID_JOB_ITEM)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobVisibilityTimeout(
      update_job_visibility_timeout_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithRequestConflictsFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kStaleUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(
                        SC_JOB_CLIENT_PROVIDER_UPDATION_CONFLICT)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobVisibilityTimeout(
      update_job_visibility_timeout_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithUpdateMessageVisibilityTimeoutFailure) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_, UpdateMessageVisibilityTimeout)
      .WillOnce([](auto& update_message_visibility_timeout_context) {
        update_message_visibility_timeout_context.result =
            FailureExecutionResult(SC_CPIO_INVALID_REQUEST);
        update_message_visibility_timeout_context.Finish();
        return update_message_visibility_timeout_context.result;
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(update_job_visibility_timeout_context.result,
                    ResultIs(FailureExecutionResult(SC_CPIO_INVALID_REQUEST)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobVisibilityTimeout(
      update_job_visibility_timeout_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

TEST_F(JobClientProviderTest,
       UpdateJobVisibilityTimeoutWithUpsertDatabaseItemFailed) {
  EXPECT_SUCCESS(job_client_provider_->Init());
  EXPECT_SUCCESS(job_client_provider_->Run());

  auto created_time = kCreatedTime;
  auto updated_time = kLastUpdatedTime;
  auto visibility_timeout = kLastUpdatedTime + TimeUtil::SecondsToDuration(30);
  auto job_body_input = CreateHelloWorldProtoAsAny();
  auto job_status = JobStatus::JOB_STATUS_CREATED;
  auto item = CreateJobAsDatabaseItem(job_body_input, job_status, created_time,
                                      updated_time, visibility_timeout);

  EXPECT_CALL(*nosql_database_client_provider_,
              GetDatabaseItem(HasGetDatabaseItemParams(kJobsTableName, kJobId)))
      .WillOnce([&item](auto& get_database_item_context) {
        get_database_item_context.response =
            make_shared<GetDatabaseItemResponse>();
        *get_database_item_context.response->mutable_item() = item;
        get_database_item_context.result = SuccessExecutionResult();
        get_database_item_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*queue_client_provider_,
              UpdateMessageVisibilityTimeout(HasMessageVisibilityTimeoutParams(
                  kQueueMessageReceiptInfo,
                  TimeUtil::DurationToSeconds(kUpdatedVisibilityTimeout))))
      .WillOnce([](auto& update_message_visibility_timeout_context) {
        update_message_visibility_timeout_context.response =
            make_shared<UpdateMessageVisibilityTimeoutResponse>();
        update_message_visibility_timeout_context.result =
            SuccessExecutionResult();
        update_message_visibility_timeout_context.Finish();
        return SuccessExecutionResult();
      });

  EXPECT_CALL(*nosql_database_client_provider_, UpsertDatabaseItem)
      .WillOnce([](auto& upsert_database_item_context) {
        upsert_database_item_context.result = FailureExecutionResult(
            SC_NO_SQL_DATABASE_PROVIDER_INVALID_PARTITION_KEY_NAME);
        upsert_database_item_context.Finish();
        return upsert_database_item_context.result;
      });

  *update_job_visibility_timeout_context_.request->mutable_job_id() = kJobId;
  *update_job_visibility_timeout_context_.request
       ->mutable_duration_to_update() = kUpdatedVisibilityTimeout;
  *update_job_visibility_timeout_context_.request->mutable_receipt_info() =
      kQueueMessageReceiptInfo;
  *update_job_visibility_timeout_context_.request
       ->mutable_most_recent_updated_time() = kLastUpdatedTime;
  update_job_visibility_timeout_context_.callback =
      [this](AsyncContext<UpdateJobVisibilityTimeoutRequest,
                          UpdateJobVisibilityTimeoutResponse>&
                 update_job_visibility_timeout_context) {
        EXPECT_THAT(
            update_job_visibility_timeout_context.result,
            ResultIs(FailureExecutionResult(
                SC_NO_SQL_DATABASE_PROVIDER_INVALID_PARTITION_KEY_NAME)));
        finish_called_ = true;
      };

  EXPECT_SUCCESS(job_client_provider_->UpdateJobVisibilityTimeout(
      update_job_visibility_timeout_context_));

  WaitUntil([this]() { return finish_called_.load(); });
}

}  // namespace google::scp::cpio::client_providers::job_client::test
