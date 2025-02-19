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

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/test/utils/auto_init_run_stop.h"
#include "public/core/test/interface/execution_result_matchers.h"
#include "roma/sandbox/constants/constants.h"
#include "roma/sandbox/js_engine/src/v8_engine/v8_js_engine.h"
#include "roma/sandbox/worker/src/worker.h"

using absl::string_view;
using google::scp::core::test::AutoInitRunStop;
using google::scp::roma::sandbox::constants::kCodeVersion;
using google::scp::roma::sandbox::constants::kHandlerName;
using google::scp::roma::sandbox::constants::kRequestAction;
using google::scp::roma::sandbox::constants::kRequestActionExecute;
using google::scp::roma::sandbox::constants::kRequestActionLoad;
using google::scp::roma::sandbox::constants::kRequestType;
using google::scp::roma::sandbox::constants::kRequestTypeJavascript;
using google::scp::roma::sandbox::constants::kRequestTypeWasm;
using google::scp::roma::sandbox::js_engine::v8_js_engine::V8JsEngine;
using std::make_shared;
using std::string;
using std::unordered_map;
using std::vector;

namespace google::scp::roma::sandbox::worker::test {
class V8EngineWorkerTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    V8JsEngine engine;
    engine.OneTimeSetup();
  }
};

TEST_F(V8EngineWorkerTest, CanRunJsCode) {
  auto engine = make_shared<V8JsEngine>();
  Worker worker(engine, false /*require_preload*/);
  AutoInitRunStop to_handle_worker(worker);

  string js_code = "function hello_js() { return \"Hello World!\"; }";
  vector<string_view> input;
  unordered_map<string, string> metadata = {
      {kRequestType, kRequestTypeJavascript},
      {kHandlerName, "hello_js"},
      {kCodeVersion, "1"},
      {kRequestAction, kRequestActionExecute}};

  auto response_or = worker.RunCode(js_code, input, metadata);

  EXPECT_SUCCESS(response_or.result());

  auto response_string = *response_or;

  EXPECT_EQ(response_string, "\"Hello World!\"");
}

TEST_F(V8EngineWorkerTest, CanRunMultipleVersionsOfTheCode) {
  auto engine = make_shared<V8JsEngine>();
  Worker worker(engine, true /*require_preload*/);
  AutoInitRunStop to_handle_worker(worker);

  // Load v1
  string js_code = "function hello_js() { return \"Hello Version 1!\"; }";
  vector<string_view> input;
  unordered_map<string, string> metadata = {
      {kRequestType, kRequestTypeJavascript},
      {kCodeVersion, "1"},
      {kRequestAction, kRequestActionLoad}};

  auto response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  auto response_string = *response_or;
  EXPECT_EQ(response_string, "");

  // Load v2
  js_code = "function hello_js() { return \"Hello Version 2!\"; }";
  metadata = {{kRequestType, kRequestTypeJavascript},
              {kCodeVersion, "2"},
              {kRequestAction, kRequestActionLoad}};

  response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  response_string = *response_or;
  EXPECT_EQ(response_string, "");

  // Execute v1
  js_code = "";
  metadata = {{kRequestType, kRequestTypeJavascript},
              {kCodeVersion, "1"},
              {kRequestAction, kRequestActionExecute},
              {kHandlerName, "hello_js"}};

  response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  response_string = *response_or;
  EXPECT_EQ(response_string, "\"Hello Version 1!\"");

  // Execute v2
  js_code = "";
  metadata = {{kRequestType, kRequestTypeJavascript},
              {kCodeVersion, "2"},
              {kRequestAction, kRequestActionExecute},
              {kHandlerName, "hello_js"}};

  response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  response_string = *response_or;
  EXPECT_EQ(response_string, "\"Hello Version 2!\"");
}

TEST_F(V8EngineWorkerTest, CanRunMultipleVersionsOfCompilationContexts) {
  auto engine = make_shared<V8JsEngine>();
  Worker worker(engine, true /*require_preload*/);
  AutoInitRunStop to_handle_worker(worker);

  // Load v1
  string js_code = R"""(
          let bytes = new Uint8Array([
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01,
            0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07,
            0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01,
            0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b
          ]);
          let module = new WebAssembly.Module(bytes);
          let instance = new WebAssembly.Instance(module);
          function hello_js(a, b) {
            return instance.exports.add(a, b);
          }
        )""";
  vector<string_view> input;
  unordered_map<string, string> metadata = {
      {kRequestType, kRequestTypeJavascript},
      {kCodeVersion, "1"},
      {kRequestAction, kRequestActionLoad}};

  auto response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  auto response_string = *response_or;
  EXPECT_EQ(response_string, "");

  // Load v2
  js_code = R"""(
          let bytes = new Uint8Array([
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01,
            0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07,
            0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01,
            0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b
          ]);
          function hello_js(a, b) {
            var module = new WebAssembly.Module(bytes);
            var instance = new WebAssembly.Instance(module);
            return instance.exports.add(a, b);
          }
        )""";
  metadata = {{kRequestType, kRequestTypeJavascript},
              {kCodeVersion, "2"},
              {kRequestAction, kRequestActionLoad}};

  response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());
  response_string = *response_or;
  EXPECT_EQ(response_string, "");

  // Execute v1
  {
    js_code = "";
    input = {"1", "2"};
    metadata = {{kRequestType, kRequestTypeJavascript},
                {kCodeVersion, "1"},
                {kRequestAction, kRequestActionExecute},
                {kHandlerName, "hello_js"}};

    response_or = worker.RunCode(js_code, input, metadata);
    EXPECT_SUCCESS(response_or.result());
    response_string = *response_or;
    EXPECT_EQ(response_string, "3");
  }

  // Execute v2
  {
    js_code = "";
    input = {"5", "7"};
    metadata = {{kRequestType, kRequestTypeJavascript},
                {kCodeVersion, "2"},
                {kRequestAction, kRequestActionExecute},
                {kHandlerName, "hello_js"}};

    response_or = worker.RunCode(js_code, input, metadata);
    EXPECT_SUCCESS(response_or.result());
    response_string = *response_or;
    EXPECT_EQ(response_string, "12");
  }
}

TEST_F(V8EngineWorkerTest, ShouldReturnFailureIfVersionIsNotInInCache) {
  auto engine = make_shared<V8JsEngine>();
  Worker worker(engine, true /*require_preload*/,
                1 /*compilation_context_cache_size*/);
  AutoInitRunStop to_handle_worker(worker);

  string js_code = "function hello_js() { return \"Hello World!\"; }";
  vector<string_view> input;

  // Load
  unordered_map<string, string> metadata = {
      {kRequestType, kRequestTypeJavascript},
      {kHandlerName, "hello_js"},
      {kCodeVersion, "1"},
      {kRequestAction, kRequestActionLoad}};
  auto response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());

  // Execute v1
  metadata[kRequestAction] = kRequestActionExecute;
  // Code was loaded so we don't need to send the source code again
  response_or = worker.RunCode("", input, metadata);
  EXPECT_SUCCESS(response_or.result());
  auto response_string = *response_or;
  EXPECT_EQ(response_string, "\"Hello World!\"");

  // Execute v2 - Should fail since we haven't loaded v2
  metadata[kCodeVersion] = "2";
  metadata[kRequestAction] = kRequestActionExecute;
  response_or = worker.RunCode("", input, metadata);
  EXPECT_FALSE(response_or.result());

  // Load v2
  metadata[kCodeVersion] = "2";
  metadata[kRequestAction] = kRequestActionLoad;
  response_or = worker.RunCode(js_code, input, metadata);
  EXPECT_SUCCESS(response_or.result());

  // Execute v1 - Should fail since the cache size is 1 and we loaded a new
  // version
  metadata[kCodeVersion] = "1";
  metadata[kRequestAction] = kRequestActionExecute;
  response_or = worker.RunCode("", input, metadata);
  EXPECT_FALSE(response_or.result());

  // Execute v2
  metadata[kCodeVersion] = "2";
  metadata[kRequestAction] = kRequestActionExecute;
  // Code was loaded so we don't need to send the source code again
  response_or = worker.RunCode("", input, metadata);
  EXPECT_SUCCESS(response_or.result());
  response_string = *response_or;
  EXPECT_EQ(response_string, "\"Hello World!\"");
}
}  // namespace google::scp::roma::sandbox::worker::test
