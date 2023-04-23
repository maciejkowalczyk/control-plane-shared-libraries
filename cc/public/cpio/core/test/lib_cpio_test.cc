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

#include <gmock/gmock.h>

#include "core/async_executor/src/error_codes.h"
#include "core/common/global_logger/src/global_logger.h"
#include "core/interface/async_executor_interface.h"
#include "core/message_router/src/message_router.h"
#include "cpio/client_providers/global_cpio/src/global_cpio.h"
#include "google/protobuf/any.pb.h"
#include "public/core/interface/execution_result.h"
#include "public/core/test/interface/execution_result_matchers.h"
#include "public/cpio/interface/cpio.h"
#include "public/cpio/test/global_cpio/test_cpio_options.h"
#include "public/cpio/test/global_cpio/test_lib_cpio.h"

using google::scp::core::AsyncExecutorInterface;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::common::GlobalLogger;
using google::scp::core::errors::SC_ASYNC_EXECUTOR_NOT_RUNNING;
using google::scp::core::test::ResultIs;
using google::scp::cpio::client_providers::GlobalCpio;
using std::shared_ptr;
using ::testing::IsNull;
using ::testing::NotNull;

static constexpr char kRegion[] = "us-east-1";

namespace google::scp::cpio::test {
TEST(LibCpioTest, NoLogTest) {
  TestCpioOptions options;
  options.log_option = LogOption::kNoLog;
  options.region = kRegion;
  EXPECT_SUCCESS(TestLibCpio::InitCpio(options));
  EXPECT_THAT(GlobalLogger::GetGlobalLogger(), IsNull());
  EXPECT_THAT(GlobalCpio::GetGlobalCpio(), NotNull());
  EXPECT_SUCCESS(TestLibCpio::ShutdownCpio(options));
}

TEST(LibCpioTest, ConsoleLogTest) {
  TestCpioOptions options;
  options.log_option = LogOption::kConsoleLog;
  options.region = kRegion;
  EXPECT_SUCCESS(TestLibCpio::InitCpio(options));
  EXPECT_THAT(GlobalLogger::GetGlobalLogger(), NotNull());
  EXPECT_THAT(GlobalCpio::GetGlobalCpio(), NotNull());
  EXPECT_SUCCESS(TestLibCpio::ShutdownCpio(options));
}

TEST(LibCpioTest, SysLogTest) {
  TestCpioOptions options;
  options.log_option = LogOption::kSysLog;
  options.region = kRegion;
  EXPECT_SUCCESS(TestLibCpio::InitCpio(options));
  EXPECT_THAT(GlobalLogger::GetGlobalLogger(), NotNull());
  EXPECT_THAT(GlobalCpio::GetGlobalCpio(), NotNull());
  EXPECT_SUCCESS(TestLibCpio::ShutdownCpio(options));
}

TEST(LibCpioTest, StopSuccessfully) {
  TestCpioOptions options;
  options.log_option = LogOption::kSysLog;
  options.region = kRegion;
  EXPECT_SUCCESS(TestLibCpio::InitCpio(options));
  shared_ptr<AsyncExecutorInterface> async_executor;
  EXPECT_EQ(GlobalCpio::GetGlobalCpio()->GetAsyncExecutor(async_executor),
            SuccessExecutionResult());
  EXPECT_SUCCESS(TestLibCpio::ShutdownCpio(options));

  // AsyncExecutor already stopped in ShutdownCpio, and the second stop will
  // fail.
  EXPECT_EQ(async_executor->Stop(),
            FailureExecutionResult(SC_ASYNC_EXECUTOR_NOT_RUNNING));
}
}  // namespace google::scp::cpio::test
