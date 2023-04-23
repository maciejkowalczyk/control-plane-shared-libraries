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

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>

#include "core/test/utils/aws_helper/aws_helper.h"
#include "core/test/utils/conditional_wait.h"
#include "core/test/utils/docker_helper/docker_helper.h"
#include "public/core/interface/execution_result.h"
#include "public/core/test/interface/execution_result_matchers.h"
#include "public/cpio/adapters/metric_client/test/test_aws_metric_client.h"
#include "public/cpio/adapters/parameter_client/test/test_aws_parameter_client.h"
#include "public/cpio/test/global_cpio/test_cpio_options.h"
#include "public/cpio/test/global_cpio/test_lib_cpio.h"
#include "public/cpio/test/parameter_client/test_aws_parameter_client_options.h"

using google::cmrt::sdk::parameter_service::v1::GetParameterRequest;
using google::cmrt::sdk::parameter_service::v1::GetParameterResponse;
using google::scp::core::ExecutionResult;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::errors::GetErrorMessage;
using google::scp::core::test::CreateNetwork;
using google::scp::core::test::CreateSSMClient;
using google::scp::core::test::PutParameter;
using google::scp::core::test::RemoveNetwork;
using google::scp::core::test::StartLocalStackContainer;
using google::scp::core::test::StopContainer;
using google::scp::core::test::WaitUntil;
using google::scp::cpio::TestAwsMetricClient;
using google::scp::cpio::TestAwsMetricClientOptions;
using google::scp::cpio::TestAwsParameterClient;
using google::scp::cpio::TestAwsParameterClientOptions;
using google::scp::cpio::TestCpioOptions;
using google::scp::cpio::TestLibCpio;
using std::atomic;
using std::make_shared;
using std::make_unique;
using std::runtime_error;
using std::shared_ptr;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

static constexpr char kLocalHost[] = "http://127.0.0.1";
static constexpr char kLocalstackContainerName[] =
    "cpio_integration_test_localstack";
// TODO(b/241857324): pick available ports randomly.
static constexpr char kLocalstackPort[] = "8888";
static constexpr char kParameterName[] = "test_parameter_name";
static constexpr char kParameterValue[] = "test_parameter_value";

namespace google::scp::cpio::test {
shared_ptr<PutMetricsRequest> CreatePutMetricsRequest() {
  Metric metric;
  metric.name = "test_metric";
  metric.value = "12";
  metric.unit = MetricUnit::kCount;
  metric.labels = {{"lable_key", "label_value"}};
  auto request = make_shared<PutMetricsRequest>();
  request->metrics.push_back(metric);
  return request;
}

class CpioIntegrationTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    // Starts localstack
    if (StartLocalStackContainer("", kLocalstackContainerName,
                                 kLocalstackPort) != 0) {
      throw runtime_error("Failed to start localstack!");
    }
  }

  static void TearDownTestSuite() { StopContainer(kLocalstackContainerName); }

  void SetUp() override {
    cpio_options.log_option = LogOption::kConsoleLog;
    cpio_options.region = "us-east-1";
    cpio_options.owner_id = "123456789";
    cpio_options.instance_id = "987654321";
    EXPECT_SUCCESS(TestLibCpio::InitCpio(cpio_options));
  }

  void TearDown() override {
    if (metric_client) {
      EXPECT_SUCCESS(metric_client->Stop());
    }
    if (parameter_client) {
      EXPECT_SUCCESS(parameter_client->Stop());
    }

    EXPECT_SUCCESS(TestLibCpio::ShutdownCpio(cpio_options));
  }

  void CreateMetricClient(bool enable_batch_recording) {
    auto metric_client_options = make_shared<TestAwsMetricClientOptions>();
    metric_client_options->cloud_watch_endpoint_override =
        make_shared<string>(localstack_endpoint);
    metric_client_options->metric_namespace = "test_metrics";
    metric_client_options->enable_batch_recording = enable_batch_recording;
    metric_client_options->batch_recording_time_duration = milliseconds(2000);
    metric_client = make_unique<TestAwsMetricClient>(metric_client_options);

    EXPECT_SUCCESS(metric_client->Init());
    EXPECT_SUCCESS(metric_client->Run());
  }

  void CreateParameterClient() {
    // Setup test data.
    auto ssm_client = CreateSSMClient(localstack_endpoint);
    PutParameter(ssm_client, kParameterName, kParameterValue);

    auto parameter_client_options =
        make_shared<TestAwsParameterClientOptions>();
    parameter_client_options->ssm_endpoint_override =
        make_shared<string>(localstack_endpoint);
    parameter_client =
        make_unique<TestAwsParameterClient>(parameter_client_options);

    EXPECT_SUCCESS(parameter_client->Init());
    EXPECT_SUCCESS(parameter_client->Run());
  }

  string localstack_endpoint =
      string(kLocalHost) + ":" + string(kLocalstackPort);
  unique_ptr<TestAwsMetricClient> metric_client;
  unique_ptr<TestAwsParameterClient> parameter_client;

  TestCpioOptions cpio_options;
};

TEST_F(CpioIntegrationTest, MetricClientBatchRecordingDisabled) {
  CreateMetricClient(false);

  vector<thread> threads;
  for (auto i = 0; i < 2; ++i) {
    threads.push_back(thread([&]() {
      for (auto j = 0; j < 5; j++) {
        atomic<bool> condition = false;
        EXPECT_EQ(
            metric_client->PutMetrics(
                std::move(*CreatePutMetricsRequest()),
                [&](const ExecutionResult result, PutMetricsResponse response) {
                  EXPECT_SUCCESS(result);
                  condition = true;
                }),
            SuccessExecutionResult());
        WaitUntil([&condition]() { return condition.load(); },
                  std::chrono::milliseconds(60000));
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(CpioIntegrationTest, MetricClientBatchRecordingEnabled) {
  CreateMetricClient(true);

  vector<thread> threads;
  for (auto i = 0; i < 5; ++i) {
    threads.push_back(thread([&]() {
      for (auto j = 0; j < 20; j++) {
        atomic<bool> condition = false;
        EXPECT_EQ(
            metric_client->PutMetrics(
                std::move(*CreatePutMetricsRequest()),
                [&](const ExecutionResult result, PutMetricsResponse response) {
                  EXPECT_SUCCESS(result);
                  condition = true;
                }),
            SuccessExecutionResult());
        WaitUntil([&condition]() { return condition.load(); },
                  std::chrono::milliseconds(60000));
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

// GetInstanceId and GetTag cannot be tested in Localstack.
TEST_F(CpioIntegrationTest, ParameterClientGetParameterSuccessfully) {
  // Waits some time for the parameter to be available.
  sleep_for(milliseconds(2000));

  CreateParameterClient();

  atomic<bool> condition = false;
  GetParameterRequest request;
  request.set_parameter_name(kParameterName);
  EXPECT_EQ(
      parameter_client->GetParameter(
          std::move(request),
          [&](const ExecutionResult result, GetParameterResponse response) {
            EXPECT_SUCCESS(result);
            EXPECT_EQ(response.parameter_value(), kParameterValue);
            condition = true;
          }),
      SuccessExecutionResult());
  WaitUntil([&condition]() { return condition.load(); },
            std::chrono::milliseconds(60000));
}
}  // namespace google::scp::cpio::test
