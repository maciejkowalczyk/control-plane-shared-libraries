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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "core/test/utils/conditional_wait.h"
#include "roma/config/src/config.h"
#include "roma/interface/roma.h"
#include "roma/wasm/test/testing_utils.h"

using absl::StatusOr;
using google::scp::core::test::WaitUntil;
using google::scp::roma::wasm::testing::WasmTestingUtils;
using std::atomic;
using std::bind;
using std::get;
using std::make_shared;
using std::make_unique;
using std::move;
using std::string;
using std::thread;
using std::to_string;
using std::tuple;
using std::unique_ptr;
using std::vector;
using ::testing::HasSubstr;

using namespace std::placeholders;     // NOLINT
using namespace std::chrono_literals;  // NOLINT

namespace google::scp::roma::test {
TEST(SandboxedServiceTest, InitStop) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());
  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest,
     ShouldFailToInitializeIfVirtualMemoryCapIsTooLittle) {
  Config config;
  config.max_worker_virtual_memory_mb = 10;

  auto status = RomaInit(config);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(
      "Roma initialization failed due to internal error: Could not initialize "
      "the wrapper API.",
      status.message());

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ExecuteCode) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ExecuteCodeWithEmptyId) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ShouldAllowEmptyInputs) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input1, input2) { return input1; }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, "undefined");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ShouldGetIdInResponse) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->id = "my_cool_id";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           EXPECT_EQ("my_cool_id", (*resp)->id);
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest,
     ShouldReturnWithVersionNotFoundWhenExecutingAVersionThatHasNotBeenLoaded) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  // We don't load any code, just try to execute some version
  atomic<bool> execute_finished = false;

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Execute should fail with the expected error.
                       EXPECT_FALSE(resp->ok());
                       EXPECT_EQ("Could not find code version in cache.",
                                 resp->status().message());
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, CanRunAsyncJsCode) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
      function sleep(milliseconds) {
        const date = Date.now();
        let currentDate = null;
        do {
          currentDate = Date.now();
        } while (currentDate - date < milliseconds);
      }

      function multiplePromises() {
        const p1 = Promise.resolve("some");
        const p2 = "cool";
        const p3 = new Promise((resolve, reject) => {
          sleep(1000);
          resolve("string1");
        });
        const p4 = new Promise((resolve, reject) => {
          sleep(200);
          resolve("string2");
        });

        return Promise.all([p1, p2, p3, p4]).then((values) => {
          return values;
        });
      }

      async function Handler() {
          const result = await multiplePromises();
          return result.join(" ");
      }
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, "\"some cool string1 string2\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, BatchExecute) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<int> res_count(0);
  size_t batch_size(5);
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = InvocationRequestStrInput();
    execution_obj.id = "foo";
    execution_obj.version_num = 1;
    execution_obj.handler_name = "Handler";
    execution_obj.input.push_back("\"Foobar\"");

    vector<InvocationRequestStrInput> batch(batch_size, execution_obj);
    status = BatchExecute(
        batch,
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto resp : batch_resp) {
            EXPECT_TRUE(resp.ok());
            EXPECT_EQ(resp->resp, R"("Hello world! \"Foobar\"")");
          }
          res_count.store(batch_resp.size());
          execute_finished.store(true);
        });
    EXPECT_TRUE(status.ok());
  }

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(res_count.load(), batch_size);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest,
     BatchExecuteShouldExecuteAllRequestsEvenWithSmallQueues) {
  Config config;
  // Queue of size one and 10 workers. Incoming work should block while workers
  // are busy and can't pick up items.
  config.worker_queue_max_items = 1;
  config.number_of_workers = 10;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<int> res_count(0);
  // Large batch
  size_t batch_size(100);
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = InvocationRequestStrInput();
    execution_obj.id = "foo";
    execution_obj.version_num = 1;
    execution_obj.handler_name = "Handler";
    execution_obj.input.push_back("\"Foobar\"");

    vector<InvocationRequestStrInput> batch(batch_size, execution_obj);

    status = absl::Status(absl::StatusCode::kInternal, "fail");
    while (!status.ok()) {
      status = BatchExecute(
          batch,
          [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
            for (auto resp : batch_resp) {
              EXPECT_TRUE(resp.ok());
              EXPECT_EQ(resp->resp, R"("Hello world! \"Foobar\"")");
            }
            res_count.store(batch_resp.size());
            execute_finished.store(true);
          });
    }
    EXPECT_TRUE(status.ok());
  }

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(res_count.load(), batch_size);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, MultiThreadedBatchExecuteSmallQueue) {
  Config config;
  config.worker_queue_max_items = 1;
  config.number_of_workers = 10;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<int> res_count(0);
  size_t batch_size(100);
  atomic<bool> load_finished = false;
  atomic<int> execute_finished = 0;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  WaitUntil([&]() { return load_finished.load(); });

  int num_threads = 10;
  vector<thread> threads;
  for (int i = 0; i < num_threads; i++) {
    std::atomic<bool> local_execute = false;
    threads.emplace_back([&, i]() {
      auto execution_obj = InvocationRequestStrInput();
      execution_obj.id = "foo";
      execution_obj.version_num = 1;
      execution_obj.handler_name = "Handler";
      auto input = "Foobar" + to_string(i);
      execution_obj.input.push_back("\"" + input + "\"");

      vector<InvocationRequestStrInput> batch(batch_size, execution_obj);

      auto batch_status = absl::Status(absl::StatusCode::kInternal, "fail");
      while (!batch_status.ok()) {
        batch_status = BatchExecute(
            batch,
            [&,
             i](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
              for (auto resp : batch_resp) {
                EXPECT_TRUE(resp.ok());
                auto result =
                    "\"Hello world! \\\"Foobar" + to_string(i) + "\\\"\"";
                EXPECT_EQ(resp->resp, result);
              }
              res_count += batch_resp.size();
              execute_finished++;
              local_execute.store(true);
            });
      }

      EXPECT_TRUE(batch_status.ok());

      WaitUntil([&]() { return local_execute.load(); });
    });
  }

  WaitUntil([&]() { return execute_finished >= num_threads; });
  EXPECT_EQ(res_count.load(), batch_size * num_threads);

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ExecuteCodeConcurrently) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<bool> load_finished = false;
  size_t total_runs = 10;
  vector<string> results(total_runs);
  vector<atomic<bool>> finished(total_runs);
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    for (auto i = 0u; i < total_runs; ++i) {
      auto code_obj = make_unique<InvocationRequestSharedInput>();
      code_obj->id = "foo";
      code_obj->version_num = 1;
      code_obj->handler_name = "Handler";
      code_obj->input.push_back(
          make_shared<string>("\"Foobar" + to_string(i) + "\""));

      status = Execute(move(code_obj),
                       [&, i](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                         EXPECT_TRUE(resp->ok());
                         if (resp->ok()) {
                           auto& code_resp = **resp;
                           results[i] = code_resp.resp;
                         }
                         finished[i].store(true);
                       });
      EXPECT_TRUE(status.ok());
    }
  }

  WaitUntil([&]() { return load_finished.load(); });

  for (auto i = 0u; i < total_runs; ++i) {
    WaitUntil([&, i]() { return finished[i].load(); }, 30s);
    string expected_result = string("\"Hello world! ") + string("\\\"Foobar") +
                             to_string(i) + string("\\\"\"");
    EXPECT_EQ(results[i], expected_result);
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void StringInStringOutFunction(proto::FunctionBindingIoProto& io) {
  io.set_output_string(io.input_string() + " String from C++");
}

TEST(SandboxedServiceTest,
     CanRegisterBindingAndExecuteCodeThatCallsItWithInputAndOutputString) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function = StringInStringOutFunction;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return cool_function(input);}
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Foobar String from C++")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void StringInStringOutFunctionWithRequestIdCheck(
    proto::FunctionBindingIoProto& io) {
  // Should be able to read the request ID
  EXPECT_EQ("id-that-should-be-available-in-hook-metadata",
            io.metadata().at("roma.request.id"));

  io.set_output_string(io.input_string() + " String from C++");
}

TEST(SandboxedServiceTest,
     ShouldBeAbleToGeRequestIdFromFunctionBindingMetadataInHook) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function =
      StringInStringOutFunctionWithRequestIdCheck;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "some-cool-id-doesnt-matter-because-its-a-load-request";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return cool_function(input);}
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    // Should be available in the hook
    execution_obj->id = "id-that-should-be-available-in-hook-metadata";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Foobar String from C++")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void ListOfStringInListOfStringOutFunction(proto::FunctionBindingIoProto& io) {
  int i = 1;

  for (auto& str : io.input_list_of_string().data()) {
    io.mutable_output_list_of_string()->mutable_data()->Add(
        str + " Some other stuff " + to_string(i++));
  }
}

TEST(
    SandboxedServiceTest,
    CanRegisterBindingAndExecuteCodeThatCallsItWithInputAndOutputListOfString) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function = ListOfStringInListOfStringOutFunction;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler() { some_array = ["str 1", "str 2", "str 3"]; return cool_function(some_array);}
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(
      result,
      R"(["str 1 Some other stuff 1","str 2 Some other stuff 2","str 3 Some other stuff 3"])");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void MapOfStringInMapOfStringOutFunction(proto::FunctionBindingIoProto& io) {
  for (auto& [key, value] : io.input_map_of_string().data()) {
    string new_key;
    string new_val;
    if (key == "key-a") {
      new_key = key + to_string(1);
      new_val = value + to_string(1);
    } else {
      new_key = key + to_string(2);
      new_val = value + to_string(2);
    }
    (*io.mutable_output_map_of_string()->mutable_data())[new_key] = new_val;
  }
}

TEST(SandboxedServiceTest,
     CanRegisterBindingAndExecuteCodeThatCallsItWithInputAndOutputMapOfString) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function = MapOfStringInMapOfStringOutFunction;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler() { 
      some_map = [["key-a","value-a"], ["key-b","value-b"]];
      // Since we can't stringify a Map, we build an array from the resulting map entries.
      returned_map = cool_function(new Map(some_map));
      return Array.from(returned_map.entries());
    }
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  // Since the map makes it over the wire, we can't guarantee the order of the
  // keys so we assert that the expected key-value pairs are present.
  EXPECT_THAT(result, HasSubstr("[\"key-a1\",\"value-a1\"]"));
  EXPECT_THAT(result, HasSubstr("[\"key-b2\",\"value-b2\"]"));

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void StringInStringOutFunctionThatThrows(proto::FunctionBindingIoProto& io) {
  io.set_output_string(io.input_string() + " String from C++");
  throw std::runtime_error("An error :(");
}

TEST(SandboxedServiceTest, ShouldFailGracefullyIfBindingExecutionFails) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function = StringInStringOutFunctionThatThrows;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return cool_function(input);}
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Failure in execution
                       EXPECT_FALSE(resp->ok());
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void StringInStringOutFunctionWithNoInputParams(
    proto::FunctionBindingIoProto& io) {
  // Params are all empty
  EXPECT_FALSE(io.has_input_string());
  EXPECT_FALSE(io.has_input_list_of_string());
  EXPECT_FALSE(io.has_input_map_of_string());

  io.set_output_string("String from C++");
}

TEST(SandboxedServiceTest, CanCallFunctionBindingThatDoesNotTakeAnyArguments) {
  Config config;
  config.number_of_workers = 2;
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function =
      StringInStringOutFunctionWithNoInputParams;
  function_binding_object->function_name = "cool_function";
  config.RegisterFunctionBinding(move(function_binding_object));

  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler() { return cool_function();}
    )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  EXPECT_EQ(result, R"("String from C++")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, CanExecuteWasmCode) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_string_in_string_out_example/"
      "string_in_string_out.wasm");
  auto wasm_code =
      string(reinterpret_cast<char*>(wasm_bin.data()), wasm_bin.size());
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->wasm = wasm_code;

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Foobar Hello World from WASM")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ExecuteCodeGotTimeoutError) {
  Config config;
  config.number_of_workers = 1;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"""(
    function sleep(milliseconds) {
      const date = Date.now();
      let currentDate = null;
      do {
        currentDate = Date.now();
      } while (currentDate - date < milliseconds);
    }
    function hello_js() {
        sleep(200);
        return 0;
      }
    )""";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "hello_js";
    execution_obj->tags[kTimeoutMsTag] = "100";

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_FALSE(resp->ok());
                       // Timeout error only shows in err_msg not result.
                       EXPECT_EQ(resp->status().message(),
                                 "Error when invoking the handler.");
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

void EchoFunction(proto::FunctionBindingIoProto& io) {
  io.set_output_string(io.input_string());
}

TEST(SandboxedServiceTest,
     ShouldRespectJsHeapLimitsAndContinueWorkingAfterWorkerRestart) {
  Config config;
  // Only one worker so we can make sure it's actually restarted.
  config.number_of_workers = 1;
  // Too large an allocation will cause the worker to crash and be restarted
  // since we're giving it a max of 15 MB of heap for JS execution.
  config.ConfigureJsEngineResourceConstraints(1 /*initial_heap_size_in_mb*/,
                                              15 /*maximum_heap_size_in_mb*/);
  // We register a hook to make sure it continues to work when the worker is
  // restarted
  auto function_binding_object = make_unique<FunctionBindingObjectV2>();
  function_binding_object->function = EchoFunction;
  function_binding_object->function_name = "echo_function";
  config.RegisterFunctionBinding(move(function_binding_object));
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    // Dummy code to allocate memory based on input
    code_obj->js = R"(
        function Handler(input) {
          const bigObject = [];
          for (let i = 0; i < 1024*512*Number(input); i++) {
            var person = {
            name: 'test',
            age: 24,
            };
            bigObject.push(person);
          }
          return 233;
        }
      )";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);

  load_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo2";
    code_obj->version_num = 2;
    // Dummy code to exercise binding
    code_obj->js = R"(
        function Handler(input) {
          return echo_function(input);
        }
      )";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    // Large input which should fail
    execution_obj->input.push_back("\"10\"");

    status = Execute(
        move(execution_obj),
        [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
          EXPECT_FALSE(resp->ok());
          EXPECT_EQ("Sandbox worker crashed during execution of request.",
                    resp->status().message());
          execute_finished.store(true);
        });
    EXPECT_TRUE(status.ok());
  }

  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  execute_finished.store(false);

  {
    string result;

    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    // Small input which should work
    execution_obj->input.push_back("\"1\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());

    WaitUntil([&]() { return execute_finished.load(); }, 10s);

    EXPECT_EQ("233", result);
  }

  execute_finished.store(false);

  {
    string result;

    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 2;
    execution_obj->handler_name = "Handler";
    // Small input which should work
    execution_obj->input.push_back("\"Hello, World!\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());

    WaitUntil([&]() { return execute_finished.load(); }, 10s);

    EXPECT_EQ("\"Hello, World!\"", result);
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest,
     LoadingWasmModuleShouldFailIfMemoryRequirementIsNotMet) {
  {
    Config config;
    // This module was compiled with a memory requirement of 10MiB (160 pages -
    // each page is 64KiB). When we set the limit to 150 pages, it fails to
    // properly build the WASM object.
    config.max_wasm_memory_number_of_pages = 150;
    config.number_of_workers = 1;

    auto status = RomaInit(config);
    EXPECT_TRUE(status.ok());

    auto wasm_bin = WasmTestingUtils::LoadWasmFile(
        "./cc/roma/testing/cpp_wasm_allocate_memory/allocate_memory.wasm");

    atomic<bool> load_finished = false;
    {
      auto code_obj = make_unique<CodeObject>();
      code_obj->id = "foo";
      code_obj->version_num = 1;
      code_obj->js = "";
      code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                            wasm_bin.size());

      status = LoadCodeObj(
          move(code_obj), [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
            // Fails
            EXPECT_FALSE(resp->ok());
            EXPECT_EQ("Failed to create wasm object.",
                      resp->status().message());
            load_finished.store(true);
          });
      EXPECT_TRUE(status.ok());
    }

    WaitUntil([&]() { return load_finished.load(); });

    status = RomaStop();
    EXPECT_TRUE(status.ok());
  }

  // We now load the same WASM but with the amount of memory it requires, and it
  // should work. Note that this requires restarting the service since this
  // limit is an initialization limit for the JS engine.

  {
    Config config;
    // This module was compiled with a memory requirement of 10MiB (160 pages -
    // each page is 64KiB). When we set the limit to 160 pages, it should be
    // able to properly build the WASM object.
    config.max_wasm_memory_number_of_pages = 160;
    config.number_of_workers = 1;

    auto status = RomaInit(config);
    EXPECT_TRUE(status.ok());

    auto wasm_bin = WasmTestingUtils::LoadWasmFile(
        "./cc/roma/testing/cpp_wasm_allocate_memory/allocate_memory.wasm");

    atomic<bool> load_finished = false;
    {
      auto code_obj = make_unique<CodeObject>();
      code_obj->id = "foo";
      code_obj->version_num = 1;
      code_obj->js = "";
      code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                            wasm_bin.size());

      status = LoadCodeObj(
          move(code_obj), [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
            // Loading works
            EXPECT_TRUE(resp->ok());
            load_finished.store(true);
          });
      EXPECT_TRUE(status.ok());
    }

    WaitUntil([&]() { return load_finished.load(); });

    status = RomaStop();
    EXPECT_TRUE(status.ok());
  }
}

TEST(SandboxedServiceTest, ShouldGetMetricsInResponse) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(
        move(execution_obj),
        [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
          EXPECT_TRUE(resp->ok());
          if (resp->ok()) {
            auto& code_resp = **resp;
            result = code_resp.resp;
          }

          EXPECT_GT(resp->value().metrics["roma.metric.sandboxed_code_run_ns"],
                    0);
          EXPECT_GT(resp->value().metrics["roma.metric.code_run_ns"], 0);

          std::cout
              << "Metrics:\n roma.metric.sandboxed_code_run_ns:"
              << resp->value().metrics["roma.metric.sandboxed_code_run_ns"]
              << "\n roma.metric.code_run_ns:"
              << resp->value().metrics["roma.metric.code_run_ns"] << std::endl;

          execute_finished.store(true);
        });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ShouldRespectCodeObjectCacheSize) {
  Config config;
  config.number_of_workers = 2;
  // Only one version
  config.code_version_cache_size = 1;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  // Load version 1
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world1! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  // Execute version 1
  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world1! \"Foobar\"")");

  load_finished = false;

  // Load version 2
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 2;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world2! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);

  execute_finished = false;

  // Execute version 1 - Should fail since the cache has one spot, and we loaded
  // a new version.
  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Should fail
                       EXPECT_FALSE(resp->ok());
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  execute_finished = false;
  result = "";

  // Execute version 2
  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 2;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("\"Foobar\"");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return execute_finished.load(); }, 10s);
  EXPECT_EQ(result, R"("Hello world2! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(SandboxedServiceTest, ShouldAllowLoadingVersionWhileDispatching) {
  Config config;
  config.number_of_workers = 2;
  // Up to 2 code versions at a time.
  config.code_version_cache_size = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  // Load version 1
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world1! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);

  // Start a batch execution
  {
    vector<InvocationRequestStrInput> batch;
    for (int i = 0; i < 50; i++) {
      InvocationRequestStrInput req;
      req.id = "foo";
      req.version_num = 1;
      req.handler_name = "Handler";
      req.input.push_back("\"Foobar\"");
      batch.push_back(req);
    }
    auto batch_result = BatchExecute(
        batch,
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto& resp : batch_resp) {
            EXPECT_TRUE(resp.ok());
            if (resp.ok()) {
              auto& code_resp = resp.value();
              result = code_resp.resp;
            }
          }
          execute_finished.store(true);
        });
  }

  load_finished = false;
  // Load version 2 while execution is happening
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 2;
    code_obj->js = R"JS_CODE(
    function Handler(input) { return "Hello world2! " + JSON.stringify(input);
    }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  EXPECT_EQ(result, R"("Hello world1! \"Foobar\"")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}
}  // namespace google::scp::roma::test
