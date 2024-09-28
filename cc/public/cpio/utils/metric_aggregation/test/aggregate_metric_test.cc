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

#include "public/cpio/utils/metric_aggregation/src/aggregate_metric.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/async_executor/mock/mock_async_executor.h"
#include "core/async_executor/src/async_executor.h"
#include "core/interface/async_context.h"
#include "core/test/utils/conditional_wait.h"
#include "public/core/interface/execution_result.h"
#include "public/core/test/interface/execution_result_matchers.h"
#include "public/cpio/mock/metric_client/mock_metric_client.h"
#include "public/cpio/proto/metric_service/v1/metric_service.pb.h"
#include "public/cpio/utils/metric_aggregation/interface/type_def.h"
#include "public/cpio/utils/metric_aggregation/mock/mock_aggregate_metric_with_overrides.h"

using google::cmrt::sdk::metric_service::v1::Metric;
using google::cmrt::sdk::metric_service::v1::PutMetricsRequest;
using google::cmrt::sdk::metric_service::v1::PutMetricsResponse;
using google::scp::core::AsyncContext;
using google::scp::core::AsyncExecutor;
using google::scp::core ::AsyncExecutorInterface;
using google::scp::core::AsyncOperation;
using google::scp::core::ExecutionResult;
using google::scp::core::FailureExecutionResult;
using google::scp::core::RetryExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::TimeDuration;
using google::scp::core::Timestamp;
using google::scp::core::async_executor::mock::MockAsyncExecutor;
using google::scp::core::errors::SC_CUSTOMIZED_METRIC_NOT_RUNNING;
using google::scp::core::errors::SC_CUSTOMIZED_METRIC_PUSH_CANNOT_SCHEDULE;
using google::scp::core::test::ResultIs;
using google::scp::core::test::WaitUntil;
using google::scp::cpio::MetricDefinition;
using google::scp::cpio::MetricUnit;
using std::atomic;
using std::make_shared;
using std::make_unique;
using std::map;
using std::mutex;
using std::shared_ptr;
using std::static_pointer_cast;
using std::string;
using std::thread;
using std::to_string;
using std::vector;

namespace {
constexpr char kMetricName[] = "FrontEndRequestCount";
constexpr char kNamespace[] = "PBS";
const vector<string> kEventList = {"QPS", "Errors"};

MetricDefinition CreateMetricDefinition() {
  return MetricDefinition(kMetricName, MetricUnit::kCount, kNamespace);
}

}  // namespace

namespace google::scp::cpio {

class AggregateMetricTest : public testing::Test {
 protected:
  AggregateMetricTest() {
    mock_metric_client_ = make_shared<MockMetricClient>();
    mock_async_executor_ = make_shared<MockAsyncExecutor>();
    async_executor_ = mock_async_executor_;
  }

  shared_ptr<MetricClientInterface> mock_metric_client_;
  size_t aggregation_time_duration_in_ms_ = 1000;
  shared_ptr<AsyncExecutorInterface> async_executor_;
  shared_ptr<MockAsyncExecutor> mock_async_executor_;
};

TEST_F(AggregateMetricTest, Run) {
  vector<ExecutionResult> results = {SuccessExecutionResult(),
                                     FailureExecutionResult(123),
                                     RetryExecutionResult(123)};

  for (auto result : results) {
    auto aggregate_metric = MockAggregateMetricOverrides(
        async_executor_.get(), mock_metric_client_.get(),
        CreateMetricDefinition(), aggregation_time_duration_in_ms_);

    aggregate_metric.schedule_metric_push_mock = [&]() { return result; };
    EXPECT_THAT(aggregate_metric.Run(), ResultIs(result));
  }
}

TEST_F(AggregateMetricTest, ScheduleMetricPush) {
  atomic<int> schedule_for_is_called = 0;
  mock_async_executor_->schedule_for_mock = [&](const AsyncOperation& work,
                                                Timestamp timestamp,
                                                std::function<bool()>&) {
    schedule_for_is_called++;
    return SuccessExecutionResult();
  };

  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor_.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_);
  EXPECT_THAT(
      aggregate_metric.ScheduleMetricPush(),
      ResultIs(FailureExecutionResult(SC_CUSTOMIZED_METRIC_NOT_RUNNING)));

  EXPECT_SUCCESS(aggregate_metric.Run());
  EXPECT_SUCCESS(aggregate_metric.ScheduleMetricPush());
  WaitUntil([&]() { return schedule_for_is_called.load() == 2; });
}

TEST_F(AggregateMetricTest, RunMetricPush) {
  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor_.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_, kEventList);

  int metric_push_handler_is_called = 0;
  int total_counts = 0;
  aggregate_metric.metric_push_handler_mock =
      [&](int64_t counter, const MetricDefinition& metric_info) {
        metric_push_handler_is_called += 1;
        total_counts += counter;
      };

  for (const auto& code : kEventList) {
    EXPECT_SUCCESS(aggregate_metric.Increment(code));
    EXPECT_SUCCESS(aggregate_metric.Increment());
    EXPECT_EQ(aggregate_metric.GetCounter(code), 1);
  }
  EXPECT_EQ(aggregate_metric.GetCounter(), 2);

  aggregate_metric.RunMetricPush();

  for (const auto& code : kEventList) {
    EXPECT_EQ(aggregate_metric.GetCounter(code), 0);
  }
  EXPECT_EQ(aggregate_metric.GetCounter(), 0);
  EXPECT_EQ(metric_push_handler_is_called, 3);
  EXPECT_EQ(total_counts, 4);
}

TEST_F(AggregateMetricTest, RunMetricPushHandler) {
  auto mock_metric_client = make_shared<MockMetricClient>();
  auto time_duration = 1000;
  auto counter_value = 1234;

  auto mock_async_executor = make_shared<MockAsyncExecutor>();

  shared_ptr<AsyncExecutorInterface> async_executor =
      static_pointer_cast<AsyncExecutorInterface>(mock_async_executor);

  Metric metric_received;
  int metric_push_is_called = 0;

  EXPECT_CALL(*mock_metric_client, PutMetrics)
      .Times(3)
      .WillRepeatedly([&](auto context) {
        metric_push_is_called += 1;
        metric_received.CopyFrom(context.request->metrics()[0]);
        context.result = FailureExecutionResult(123);
        context.Finish();
        return context.result;
      });

  auto metric_info = CreateMetricDefinition();
  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor.get(), mock_metric_client.get(), metric_info,
      time_duration, kEventList);

  for (const auto& code : kEventList) {
    auto info = aggregate_metric.GetMetricInfo(code);
    EXPECT_SUCCESS(info.result());
    aggregate_metric.MetricPushHandler(counter_value, info.value());
    EXPECT_EQ(metric_received.name(), kMetricName);
    EXPECT_EQ(metric_received.labels().find("EventCode")->second, code);
    EXPECT_EQ(metric_received.value(), to_string(counter_value));
  }

  aggregate_metric.MetricPushHandler(counter_value, metric_info);
  EXPECT_EQ(metric_received.name(), kMetricName);
  EXPECT_EQ(metric_received.labels().size(), 0);
  EXPECT_EQ(metric_received.value(), to_string(counter_value));
  WaitUntil([&]() { return metric_push_is_called == 3; });
}

TEST_F(AggregateMetricTest, Increment) {
  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor_.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_, kEventList);

  auto value = 1;
  for (const auto& code : kEventList) {
    for (auto i = 0; i < value; i++) {
      EXPECT_SUCCESS(aggregate_metric.Increment(code));
    }
    EXPECT_EQ(aggregate_metric.GetCounter(code), value);
    value++;
  }
}

TEST_F(AggregateMetricTest, IncrementBy) {
  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor_.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_, kEventList);

  auto value = 10;
  for (const auto& code : kEventList) {
    for (auto i = 0; i < value; i++) {
      EXPECT_SUCCESS(aggregate_metric.IncrementBy(value, code));
    }
    EXPECT_EQ(aggregate_metric.GetCounter(code), value * value);
  }
}

TEST_F(AggregateMetricTest, IncrementByMultipleThreads) {
  auto aggregate_metric = MockAggregateMetricOverrides(
      async_executor_.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_, kEventList);
  auto value = 10;
  auto num_threads = 2;
  auto num_calls = 10;
  vector<thread> threads;

  for (auto i = 0; i < num_threads; ++i) {
    threads.push_back(thread([&]() {
      for (auto j = 0; j < num_calls; j++) {
        for (const auto& code : kEventList) {
          EXPECT_SUCCESS(aggregate_metric.IncrementBy(value, code));
        }
      }
    }));
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (const auto& code : kEventList) {
    EXPECT_EQ(aggregate_metric.GetCounter(code),
              value * num_threads * num_calls);
  }
}

TEST_F(AggregateMetricTest, StopShouldNotDiscardAnyCounters) {
  auto real_async_executor = make_shared<AsyncExecutor>(
      2 /* thread count */, 1000 /* queue capacity */,
      true /* drop tasks on stop*/);
  EXPECT_SUCCESS(real_async_executor->Init());
  EXPECT_SUCCESS(real_async_executor->Run());

  auto aggregate_metric = MockAggregateMetricOverrides(
      real_async_executor.get(), mock_metric_client_.get(),
      CreateMetricDefinition(), aggregation_time_duration_in_ms_, kEventList);

  EXPECT_SUCCESS(aggregate_metric.Init());
  EXPECT_SUCCESS(aggregate_metric.Run());

  auto value = 1;
  for (const auto& code : kEventList) {
    for (auto i = 0; i < value; i++) {
      EXPECT_SUCCESS(aggregate_metric.Increment(code));
    }
    EXPECT_EQ(aggregate_metric.GetCounter(code), value);
    value++;
  }

  EXPECT_SUCCESS(aggregate_metric.Stop());

  // Counters should be 0
  for (const auto& event_code : kEventList) {
    EXPECT_EQ(aggregate_metric.GetCounter(event_code), 0);
  }

  EXPECT_SUCCESS(real_async_executor->Stop());
}

}  // namespace google::scp::cpio
