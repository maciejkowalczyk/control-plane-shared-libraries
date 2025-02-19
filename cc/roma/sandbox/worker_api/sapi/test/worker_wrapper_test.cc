/*
 * Copyright 2023 Google LLC
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

#include "roma/sandbox/worker_api/sapi/src/worker_wrapper.h"

#include <gtest/gtest.h>

#include "public/core/test/interface/execution_result_matchers.h"
#include "roma/sandbox/constants/constants.h"
#include "roma/sandbox/worker_api/sapi/src/worker_init_params.pb.h"
#include "roma/sandbox/worker_factory/src/worker_factory.h"

using google::scp::roma::sandbox::constants::kCodeVersion;
using google::scp::roma::sandbox::constants::kHandlerName;
using google::scp::roma::sandbox::constants::kRequestAction;
using google::scp::roma::sandbox::constants::kRequestActionExecute;
using google::scp::roma::sandbox::constants::kRequestType;
using google::scp::roma::sandbox::constants::kRequestTypeJavascript;
using google::scp::roma::sandbox::worker::WorkerFactory;

namespace google::scp::roma::sandbox::worker_api::test {
static ::worker_api::WorkerInitParamsProto GetDefaultInitParams() {
  ::worker_api::WorkerInitParamsProto init_params;
  init_params.set_worker_factory_js_engine(
      static_cast<int>(worker::WorkerFactory::WorkerEngine::v8));
  init_params.set_require_code_preload_for_execution(false);
  init_params.set_compilation_context_cache_size(5);
  init_params.set_native_js_function_comms_fd(-1);
  init_params.mutable_native_js_function_names()->Clear();
  init_params.set_js_engine_initial_heap_size_mb(0);
  init_params.set_js_engine_maximum_heap_size_mb(0);
  init_params.set_js_engine_max_wasm_memory_number_of_pages(0);
  return init_params;
}

TEST(WorkerWrapperTest, CanRunCodeThroughWrapperWithoutPreload) {
  auto init_params = GetDefaultInitParams();
  auto result = ::Init(&init_params);
  EXPECT_EQ(SC_OK, result);

  result = ::Run();
  EXPECT_EQ(SC_OK, result);

  ::worker_api::WorkerParamsProto params_proto;
  params_proto.set_code(
      "function cool_func() { return \"Hi there from JS :)\" }");
  (*params_proto.mutable_metadata())[kRequestType] = kRequestTypeJavascript;
  (*params_proto.mutable_metadata())[kHandlerName] = "cool_func";
  (*params_proto.mutable_metadata())[kCodeVersion] = "1";
  (*params_proto.mutable_metadata())[kRequestAction] = kRequestActionExecute;

  result = ::RunCode(&params_proto);
  EXPECT_EQ(SC_OK, result);

  EXPECT_EQ(params_proto.response(), "\"Hi there from JS :)\"");

  result = ::Stop();
  EXPECT_EQ(SC_OK, result);
}

TEST(WorkerWrapperTest, FailsToRunCodeWhenPreloadIsRequiredAndExecuteIsSent) {
  auto init_params = GetDefaultInitParams();
  init_params.set_require_code_preload_for_execution(true);
  init_params.set_worker_factory_js_engine(
      static_cast<int>(worker::WorkerFactory::WorkerEngine::v8));
  init_params.set_require_code_preload_for_execution(true);
  auto result = ::Init(&init_params);
  EXPECT_EQ(SC_OK, result);

  result = ::Run();
  EXPECT_EQ(SC_OK, result);

  ::worker_api::WorkerParamsProto params_proto;
  params_proto.set_code(
      "function cool_func() { return \"Hi there from JS :)\" }");
  (*params_proto.mutable_metadata())[kRequestType] = kRequestTypeJavascript;
  (*params_proto.mutable_metadata())[kHandlerName] = "cool_func";
  (*params_proto.mutable_metadata())[kCodeVersion] = "1";
  (*params_proto.mutable_metadata())[kRequestAction] = kRequestActionExecute;

  result = ::RunCode(&params_proto);
  EXPECT_NE(SC_OK, result);

  result = ::Stop();
  EXPECT_EQ(SC_OK, result);
}
}  // namespace google::scp::roma::sandbox::worker_api::test
