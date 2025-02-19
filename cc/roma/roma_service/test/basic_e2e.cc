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
#include "roma/interface/roma.h"
#include "roma/roma_service/src/roma_service.h"
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

using namespace std::placeholders;     // NOLINT
using namespace std::chrono_literals;  // NOLINT

namespace google::scp::roma::test {
TEST(RomaBasicE2ETest, InitStop) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());
  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ShouldFailToInitIfNotEnoughSystemMemory) {
  Config config;
  config.GetStartupMemoryCheckMinimumNeededValueKb = []() {
    return UINT64_MAX;
  };

  auto status = RomaInit(config);

  EXPECT_FALSE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteCode) {
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

TEST(RomaBasicE2ETest, ExecuteAsyncCode) {
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
    code_obj->js = R"JS_CODE(
      function sleep(milliseconds) {
        const date = Date.now();
        let currentDate = null;
        do {
          currentDate = Date.now();
        } while (currentDate - date < milliseconds);
      }
      function resolveAfterOneSecond() {
        return new Promise((resolve) => {
          sleep(1000);
          resolve("some cool string");
        });
      }
      async function Handler() {
          const result = await resolveAfterOneSecond();
          return result;
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
  EXPECT_EQ(result, R"("some cool string")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteAsyncCodeWithPromiseAllSuccess) {
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
          resolve("string");
        });

        return Promise.all([p1, p2, p3]).then((values) => {
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
  EXPECT_EQ(result, R"("some cool string")");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteAsyncCodeFailedWithUndefinedError) {
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
    // JS code async handler has undefined func name "setTimeout".
    code_obj->js = R"JS_CODE(
      function resolveAfterOneSecond() {
        return new Promise(resolve => setTimeout(resolve, 2000));
      }
      async function Handler() {
          const result = await resolveAfterOneSecond();
          return result;
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

    status =
        Execute(move(execution_obj),
                [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                  EXPECT_FALSE(resp->ok());
                  EXPECT_EQ(resp->status().message(),
                            "The code object async function execution failed.");
                  execute_finished.store(true);
                });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteAsyncCodeFailedWithPromiseRejected) {
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
    // JS code async handler has rejected promise.
    code_obj->js = R"JS_CODE(
      function sleep(milliseconds) {
        const date = Date.now();
        let currentDate = null;
        do {
          currentDate = Date.now();
        } while (currentDate - date < milliseconds);
      }
      function resolveAfterOneSecond() {
        return new Promise((resolve, reject) => {
          sleep(1000);
          reject("reject error from promise!");
        });
      }
      async function Handler() {
          const result = await resolveAfterOneSecond();
          return result;
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

    status =
        Execute(move(execution_obj),
                [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                  EXPECT_FALSE(resp->ok());
                  EXPECT_EQ(resp->status().message(),
                            "The code object async function execution failed.");
                  execute_finished.store(true);
                });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, BatchExecute) {
  Config config;
  config.number_of_workers = 10;
  config.worker_queue_max_items = 5;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<int> res_count(0);
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
    vector<InvocationRequestStrInput> batch;

    for (auto i = 0; i < batch_size; i++) {
      auto execution_obj = InvocationRequestStrInput();
      execution_obj.id = std::to_string(i);
      execution_obj.version_num = 1;
      execution_obj.handler_name = "Handler";
      execution_obj.input.push_back("\"Foobar\"");

      batch.push_back(execution_obj);
    }

    auto callback =
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto resp : batch_resp) {
            ASSERT_TRUE(resp.ok());
            EXPECT_EQ(resp->resp, R"("Hello world! \"Foobar\"")");
          }
          res_count.store(batch_resp.size());
          execute_finished.store(true);
        };
    while (!BatchExecute(batch, callback).ok()) {}
  }

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(res_count.load(), batch_size);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, BatchExecuteShouldWithSmallQueue) {
  Config config;
  config.number_of_workers = 10;
  config.worker_queue_max_items = 1;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<int> res_count(0);
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
    vector<InvocationRequestStrInput> batch;

    for (auto i = 0; i < batch_size; i++) {
      auto execution_obj = InvocationRequestStrInput();
      execution_obj.id = std::to_string(i);
      execution_obj.version_num = 1;
      execution_obj.handler_name = "Handler";
      execution_obj.input.push_back("\"Foobar\"");

      batch.push_back(execution_obj);
    }
    auto callback =
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto resp : batch_resp) {
            ASSERT_TRUE(resp.ok());
            EXPECT_EQ(resp->resp, R"("Hello world! \"Foobar\"")");
          }
          res_count.store(batch_resp.size());
          execute_finished.store(true);
        };
    while (!BatchExecute(batch, callback).ok()) {}
  }

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(res_count.load(), batch_size);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ShouldWorkWithMultiThreadsDispatchInSmallQueue) {
  Config config;
  config.number_of_workers = 1;
  config.worker_queue_max_items = 1;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<bool> load_finished = false;
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

  // Multiple threads do dispatcher::Execute()
  vector<thread> threads;
  constexpr int num_threads = 101;
  threads.reserve(num_threads);
  vector<atomic<bool>> finished(num_threads);
  // We could potentially have multiple threads dispatch request.
  for (int i = 0; i < num_threads; i++) {
    threads.push_back(thread([i, &finished]() {
      InvocationRequestSharedInput code_obj;
      code_obj.id = "foo";
      code_obj.version_num = 1;
      code_obj.handler_name = "Handler";
      code_obj.input.push_back(make_shared<string>("\"Foobar\""));

      auto callback = [&, i](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
        EXPECT_TRUE(resp->ok());
        if (resp->ok()) {
          ASSERT_TRUE(resp->ok());
          EXPECT_EQ((**resp).resp, R"("Hello world! \"Foobar\"")");
        }
        finished[i].store(true);
      };

      while (!Execute(make_unique<InvocationRequestSharedInput>(code_obj),
                      callback)
                  .ok()) {}
    }));
  }

  // One thread does dispatcher::BatchExecute().
  atomic<int> res_count(0);
  size_t batch_size(100);
  atomic<bool> execute_finished = false;
  {
    vector<InvocationRequestStrInput> batch;

    for (auto i = 0; i < batch_size; i++) {
      auto execution_obj = InvocationRequestStrInput();
      execution_obj.id = std::to_string(i);
      execution_obj.version_num = 1;
      execution_obj.handler_name = "Handler";
      execution_obj.input.push_back("\"Foobar\"");

      batch.push_back(execution_obj);
    }
    auto callback =
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto resp : batch_resp) {
            ASSERT_TRUE(resp.ok());
            EXPECT_EQ(resp->resp, R"("Hello world! \"Foobar\"")");
          }
          res_count.store(batch_resp.size());
          execute_finished.store(true);
        };
    while (!BatchExecute(batch, callback).ok()) {}
  }

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });

  for (auto i = 0u; i < num_threads; ++i) {
    WaitUntil([&, i]() { return finished[i].load(); }, 30s);
  }

  EXPECT_EQ(res_count.load(), batch_size);

  for (auto& t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, MultiThreadedBatchExecuteSmallQueue) {
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

    auto callback = [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
      EXPECT_TRUE(resp->ok());
      load_finished.store(true);
    };

    while (!LoadCodeObj(move(code_obj), callback).ok()) {}
  }

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

      auto callback =
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
          };
      while (!BatchExecute(batch, callback).ok()) {}
      WaitUntil([&]() { return local_execute.load(); });
    });
  }

  WaitUntil([&]() { return load_finished.load(); });
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

TEST(RomaBasicE2ETest, ExecuteCodeConcurrently) {
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
      code_obj->input.push_back(make_shared<string>("\"Foobar\""));

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
    EXPECT_EQ(results[i], R"("Hello world! \"Foobar\"")");
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

static string StringInStringOutFunction(tuple<string>& input) {
  return get<0>(input) + " I'm actually coming from a c++ function :)";
}

TEST(RomaBasicE2ETest, StringInStringOutFunctionBindingRegistration) {
  // Create function binding object to add to config
  auto function_object = make_unique<FunctionBindingObject<string, string>>();
  function_object->function_name = "my_cool_func";
  function_object->function = StringInStringOutFunction;

  // Create config object and add function registration object to it
  Config config;
  config.number_of_workers = 2;
  config.RegisterFunctionBinding(std::move(function_object));

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
    function Handler(input) { return my_cool_func(input); }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar:\""));

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  // Check that we got the return string from the C++ function
  EXPECT_EQ(result, "\"Foobar: I'm actually coming from a c++ function :)\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest,
     StringInStringOutFunctionBindingRegistrationWithInlineLambda) {
  // Create function binding object to add to config
  auto function_object = make_unique<FunctionBindingObject<string, string>>();
  function_object->function_name = "my_cool_func";
  function_object->function = [](std::tuple<string>& input) -> string {
    return get<0>(input) + "With text from lambda";
  };

  // Create config object and add function registration object to it
  Config config;
  config.number_of_workers = 2;
  config.RegisterFunctionBinding(std::move(function_object));

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
    function Handler(input) { return my_cool_func(input); }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar:\""));

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  // Check that we got the return string from the C++ function
  EXPECT_EQ(result, "\"Foobar:With text from lambda\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

class MyHandler {
 public:
  explicit MyHandler(string input) { return_value_ = input; }

  string HookHandler(std::tuple<string>& input) {
    return get<0>(input) + return_value_;
  }

 private:
  string return_value_;
};

TEST(RomaBasicE2ETest,
     StringInStringOutFunctionBindingRegistrationWithMemberFunction) {
  MyHandler my_handler("With text from member function");

  // Create function binding object to add to config
  auto function_object = make_unique<FunctionBindingObject<string, string>>();
  function_object->function_name = "my_cool_func";
  function_object->function = bind(&MyHandler::HookHandler, my_handler, _1);

  // Create config object and add function registration object to it
  Config config;
  config.number_of_workers = 2;
  config.RegisterFunctionBinding(std::move(function_object));

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
    function Handler(input) { return my_cool_func(input); }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar:\""));

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  // Check that we got the return string from the C++ function
  EXPECT_EQ(result, "\"Foobar:With text from member function\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

static string FunctionOne(tuple<string>& input) {
  return get<0>(input) + " str1 from c++ func1 ";
}

static string FunctionTwo(tuple<string>& input) {
  return get<0>(input) + " str2 from c++ func2";
}

TEST(RomaBasicE2ETest, StringInStringOutRegisterMultipleFunctions) {
  // Create function binding objects to add to config
  auto function_object1 = make_unique<FunctionBindingObject<string, string>>();
  function_object1->function_name = "func_one";
  function_object1->function = FunctionOne;

  auto function_object2 = make_unique<FunctionBindingObject<string, string>>();
  function_object2->function_name = "func_two";
  function_object2->function = FunctionTwo;

  // Create config object and add function registration objects to it
  Config config;
  config.number_of_workers = 2;
  config.RegisterFunctionBinding(std::move(function_object1));
  config.RegisterFunctionBinding(std::move(function_object2));

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
    function Handler(input) { return func_one(input) + func_two(input); }
  )JS_CODE";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar:\""));

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  // Check that we got the return string from the C++ function
  EXPECT_EQ(result,
            "\"Foobar: str1 from c++ func1 Foobar: str2 from c++ func2\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

static string ConcatenateVector(vector<string>& vec) {
  string ret;
  for (const auto& str : vec) {
    ret += str;
  }
  return ret;
}

static common::Map<string, string> ListsOfStringsInMapOutFunction(
    tuple<vector<string>, vector<string>, vector<string>, vector<string>>&
        input) {
  common::Map<string, string> output;

  output.Set("list1", ConcatenateVector(get<0>(input)));
  output.Set("list2", ConcatenateVector(get<1>(input)));
  output.Set("list3", ConcatenateVector(get<2>(input)));
  output.Set("list4", ConcatenateVector(get<3>(input)));
  return output;
}

TEST(RomaBasicE2ETest, ListsOfStringInMapOfStringOutFunctionRegistration) {
  // Create function binding object to add to config
  auto function_object = make_unique<
      FunctionBindingObject<common::Map<string, string>, vector<string>,
                            vector<string>, vector<string>, vector<string>>>();
  function_object->function_name = "awesome_func";
  function_object->function = ListsOfStringsInMapOutFunction;

  // Create config object and add function registration object to it
  Config config;
  config.number_of_workers = 2;
  config.RegisterFunctionBinding(std::move(function_object));

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
    function Handler(input) {
      map = awesome_func(
        ['hello','from'],
        ['a','js','function'],
        ['that','will','call'],
        ['a', 'c++','function']);

      result = [];

      for (let [key, value] of  map.entries()) {
        entry = key + ':' + value;
        result.push(entry);
      }

      return result;
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
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar:\""));

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  // Check that we got the return string from the C++ function
  EXPECT_EQ(result,
            "[\"list1:hellofrom\",\"list2:ajsfunction\",\"list3:thatwillcall\","
            "\"list4:ac++function\"]");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, CppCompiledStringInputStringOutputWasm) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_string_in_string_out_example/"
      "string_in_string_out.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar\""));
    execution_obj->wasm_return_type = WasmDataType::kString;

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(result, "\"Foobar Hello World from WASM\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, RustCompiledStringInputStringOutputWasm) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/rust_wasm_string_in_string_out_example/"
      "string_in_string_out.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar\""));
    execution_obj->wasm_return_type = WasmDataType::kString;

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(result, "\"Foobar Hello from rust!\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteCodeWithBadInput) {
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
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    // Bad input with invalid string.
    execution_obj->input.push_back(make_shared<string>("\"Foobar"));

    status = Execute(
        move(execution_obj),
        [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
          EXPECT_TRUE(!resp->ok());
          EXPECT_EQ(resp->status().message(),
                    "Failed due to bad input arguments, invalid std::string.");
          execute_finished.store(true);
        });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, CppCompiledListOfStringInputListOfStringOutputWasm) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_list_of_string_in_list_of_string_out_example/"
      "list_of_string_in_list_of_string_out.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(
        make_shared<string>("[\"Foobar\", \"Barfoo\"]"));
    execution_obj->wasm_return_type = WasmDataType::kListOfString;

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(
      result,
      "[\"Foobar\",\"Barfoo\",\"String from Cpp1\",\"String from Cpp2\"]");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, RustCompiledListOfStringInputListOfStringOutputWasm) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/"
      "rust_wasm_list_of_string_in_list_of_string_out_example/"
      "list_of_string_in_list_of_string_out.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(
        make_shared<string>("[\"Foobar\", \"Barfoo\"]"));
    execution_obj->wasm_return_type = WasmDataType::kListOfString;

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

  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });
  EXPECT_EQ(
      result,
      "[\"Foobar\",\"Barfoo\",\"Hello from rust1\",\"Hello from rust2\"]");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, EmptyWasmAndEmptyJsInCodeObject) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());

  auto code_obj = make_unique<CodeObject>();
  code_obj->id = "foo";
  code_obj->version_num = 1;

  status = LoadCodeObj(move(code_obj),
                       [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "Roma LoadCodeObj failed due to empty code content.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, CodeObjMissingVersionNumber) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto code_obj = make_unique<CodeObject>();
  code_obj->id = "foo";
  code_obj->js = "dummy";

  status = LoadCodeObj(move(code_obj),
                       [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "Roma LoadCodeObj failed due to invalid version.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecutionObjMissingVersionNumber) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());

  auto code_obj = make_unique<InvocationRequestSharedInput>();
  code_obj->id = "foo";
  code_obj->handler_name = "Handler";
  string result;
  status = Execute(move(code_obj),
                   [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
  EXPECT_FALSE(status.ok());

  EXPECT_EQ(status.message(), "Roma Execute failed due to invalid version.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, WasmBatchWithMissingVersionNumber) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  // Wasm from https://github.com/v8/v8/blob/master/samples/hello-world.cc#L69
  char wasm_bin[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01,
                     0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03,
                     0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61, 0x64,
                     0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20,
                     0x00, 0x20, 0x01, 0x6a, 0x0b};

  atomic<bool> load_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(wasm_bin, sizeof(wasm_bin));

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });

  auto execution_obj = InvocationRequestSharedInput();
  execution_obj.id = "foo";
  execution_obj.handler_name = "Handler";
  execution_obj.wasm_return_type = WasmDataType::kUint32;
  execution_obj.input.push_back(make_shared<string>("\"Foobar\""));

  vector<InvocationRequestSharedInput> requests = {execution_obj};
  // Add the version num so it's only missing from one request
  execution_obj.version_num = 1;
  requests.push_back(execution_obj);

  status = BatchExecute(requests,
                        [&](const vector<StatusOr<ResponseObject>>& resp) {});

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "Roma BatchExecute failed due to invalid version.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecutionObjMissingHandlerName) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto code_obj = make_unique<InvocationRequestSharedInput>();
  code_obj->id = "foo";
  code_obj->version_num = 1;
  string result;
  status = Execute(move(code_obj),
                   [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
  EXPECT_FALSE(status.ok());

  EXPECT_EQ(status.message(), "Roma Execute failed due to empty handler name.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, WasmBatchWithMissingHandlerName) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());

  // Wasm from https://github.com/v8/v8/blob/master/samples/hello-world.cc#L69
  char wasm_bin[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01,
                     0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x03,
                     0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61, 0x64,
                     0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20,
                     0x00, 0x20, 0x01, 0x6a, 0x0b};

  atomic<bool> load_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(wasm_bin, sizeof(wasm_bin));

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });

  auto execution_obj = InvocationRequestSharedInput();
  execution_obj.id = "foo";
  execution_obj.version_num = 1;
  execution_obj.wasm_return_type = WasmDataType::kUint32;
  execution_obj.input.push_back(make_shared<string>("\"Foobar\""));

  vector<InvocationRequestSharedInput> requests = {execution_obj};
  // Add the handler name so it's only missing from one request
  execution_obj.handler_name = "Handler";
  requests.push_back(execution_obj);

  status = BatchExecute(requests,
                        [&](const vector<StatusOr<ResponseObject>>& resp) {});

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "Roma BatchExecute failed due to empty handler name.");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

/**
 * @brief Based on empirical testing, we can always allocate an amount close to
 * half of the total module memory.
 */
TEST(RomaBasicE2ETest, WasmAllocationShouldFailWhenAllocatingTooMuch) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_allocate_memory/allocate_memory.wasm");

  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    // A 5MB allocation would fail since the module has an overall 10MB memory
    // size. And we can always allocate close to half, but not half.
    constexpr uint32_t size_to_allocate = 5 * 1024 * 1024;  // 5 MB
    execution_obj->input.push_back(
        make_shared<string>(to_string(size_to_allocate)));
    execution_obj->wasm_return_type = WasmDataType::kUint32;

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Fails
                       EXPECT_FALSE(resp->ok());
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

/**
 * @brief Based on empirical testing, we can always allocate an amount close to
 * half of the total module memory.
 */
TEST(RomaBasicE2ETest, WasmAllocationShouldWorkWhenAllocatingWithinBounds) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_allocate_memory/allocate_memory.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    // A 5MB allocation would fail since the module has an overall 10MB memory
    // size. And we can always allocate close to half, but not half.
    constexpr uint32_t size_to_allocate = (4.99 * 1024 * 1024);  // 4.99MB
    execution_obj->input.push_back(
        make_shared<string>(to_string(size_to_allocate)));
    execution_obj->wasm_return_type = WasmDataType::kUint32;

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Succeeds
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

/**
 * @brief WASM returns a bad pointer, which we would expect to be a string, so
 * we should just parse an empty string out
 *
 */
TEST(RomaBasicE2ETest, WasmReturnsBadPointerWhenAStringIsExpected) {
  Config config;
  config.number_of_workers = 2;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_return_bad_pointer/return_bad_pointer.wasm");

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("0"));
    execution_obj->wasm_return_type = WasmDataType::kString;

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Succeeds
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });

  // We get an empty string out
  EXPECT_EQ(result, "\"\"");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

/**
 * @brief WASM returns a bad pointer, which we would expect to be a list of
 * string, so we should just parse an empty list out
 *
 */
TEST(RomaBasicE2ETest, WasmReturnsBadPointerWhenAListOfStringIsExpected) {
  auto status = RomaInit();
  EXPECT_TRUE(status.ok());

  auto wasm_bin = WasmTestingUtils::LoadWasmFile(
      "./cc/roma/testing/cpp_wasm_return_bad_pointer/return_bad_pointer.wasm");

  string result = "NOT EMPTY";
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = "";
    code_obj->wasm.assign(reinterpret_cast<char*>(wasm_bin.data()),
                          wasm_bin.size());

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  {
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("0"));
    execution_obj->wasm_return_type = WasmDataType::kListOfString;

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       // Succeeds
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         result = code_resp.resp;
                       }
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); });
  WaitUntil([&]() { return execute_finished.load(); });

  // We get an empty list of string out
  EXPECT_EQ(result, "[]");

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteInvocationRequestSharedInput) {
  auto status = RomaInit();
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
    auto execution_obj = make_unique<InvocationRequestSharedInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back(make_shared<string>("\"Foobar\""));

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

TEST(RomaBasicE2ETest, BatchExecuteInvocationRequestSharedInput) {
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
    auto execution_obj = InvocationRequestSharedInput();
    execution_obj.id = "foo";
    execution_obj.version_num = 1;
    execution_obj.handler_name = "Handler";
    execution_obj.input.push_back(make_shared<string>("\"Foobar\""));

    vector<InvocationRequestSharedInput> batch(batch_size, execution_obj);
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

TEST(RomaBasicE2ETest, ExecuteCodeWithConfiguredHeap) {
  Config config;
  config.number_of_workers = 2;
  config.ConfigureJsEngineResourceConstraints(1, 15);
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  atomic<bool> load_finished = false;
  atomic<int> execute_finished = 0;

  // Load code to workers.
  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    // The required JS code execution heap size depends on the input.
    code_obj->js = R"""(
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
      )""";

    status = LoadCodeObj(move(code_obj),
                         [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                           EXPECT_TRUE(resp->ok());
                           load_finished.store(true);
                         });
    EXPECT_TRUE(status.ok());
  }

  // An input that is too large will cause an OOM.
  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("10");

    status = Execute(
        move(execution_obj),
        [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
          EXPECT_FALSE(resp->ok());
          EXPECT_EQ(resp->status().message(),
                    "The work item has already been worked on. This implies "
                    "the worker initially died while handling this item.");
          execute_finished.fetch_add(1);
        });
    EXPECT_TRUE(status.ok());
  }

  // With smaller inputs, the execution should succeed.
  for (auto i = 0; i < 3; i++) {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";
    execution_obj->input.push_back("1");

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {
                       EXPECT_TRUE(resp->ok());
                       if (resp->ok()) {
                         auto& code_resp = **resp;
                         EXPECT_EQ(code_resp.resp, R"(233)");
                       }
                       execute_finished++;
                     });
    EXPECT_TRUE(status.ok());
  }

  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load() == 4; }, 300s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, ExecuteCodeTimeout) {
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
                                 "Code object execute timeout.");
                       execute_finished.store(true);
                     });
    EXPECT_TRUE(status.ok());
  }
  WaitUntil([&]() { return load_finished.load(); }, 10s);
  WaitUntil([&]() { return execute_finished.load(); }, 10s);

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}

TEST(RomaBasicE2ETest, BatchExecuteTimeout) {
  Config config;
  config.number_of_workers = 1;
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
    auto execution_obj = InvocationRequestSharedInput();
    execution_obj.id = "foo";
    execution_obj.version_num = 1;
    execution_obj.handler_name = "hello_js";
    execution_obj.tags[kTimeoutMsTag] = "100";

    vector<InvocationRequestSharedInput> batch(batch_size, execution_obj);
    status = BatchExecute(
        batch,
        [&](const std::vector<absl::StatusOr<ResponseObject>>& batch_resp) {
          for (auto resp : batch_resp) {
            EXPECT_FALSE(resp.ok());
            // Timeout error only shows in err_msg not result.
            EXPECT_EQ(resp.status().message(), "Code object execute timeout.");
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

TEST(RomaBasicE2ETest, ShouldReturnFailureIfShmAllocationFails) {
  Config config;
  config.number_of_workers = 1;
  config.ipc_memory_size_in_mb = 1;
  // Only one item in the queue so we can maximize the 1MB shared memory block
  config.worker_queue_max_items = 1;
  auto status = RomaInit(config);
  EXPECT_TRUE(status.ok());

  const size_t one_mb = 1 * 1024 * 1024;
  string one_mb_string("A", one_mb);
  EXPECT_EQ(one_mb, one_mb_string.length());

  string result;
  atomic<bool> load_finished = false;
  atomic<bool> execute_finished = false;

  {
    auto code_obj = make_unique<CodeObject>();
    code_obj->id = "foo";
    code_obj->version_num = 1;
    code_obj->js = one_mb_string;

    status =
        LoadCodeObj(move(code_obj),
                    [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
    // Loading should fail since the item doesn't fit in shared memory
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(
        "Roma LoadCodeObj failed with: Allocating in the shared memory block "
        "failed.",
        status.message());
  }

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
    WaitUntil([&]() { return load_finished.load(); }, 10s);
  }

  // Execute with large input, should fail since it can't be allocated
  {
    auto execution_obj = make_unique<InvocationRequestStrInput>();
    execution_obj->id = "foo";
    execution_obj->version_num = 1;
    execution_obj->handler_name = "Handler";

    // Pass the 1MB string as an input. This will need to be copied into shared
    // memory, but it shouldn't fit since the SHM block is of 1MB overall.
    execution_obj->input.push_back(one_mb_string);

    status = Execute(move(execution_obj),
                     [&](unique_ptr<absl::StatusOr<ResponseObject>> resp) {});
    // This doesn't even enqueue
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(
        "Roma Execute failed due to: Allocating in the shared memory block "
        "failed.",
        status.message());
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

    WaitUntil([&]() { return execute_finished.load(); }, 10s);
    // This one should work since it has a small input
    EXPECT_EQ(result, R"("Hello world! \"Foobar\"")");
  }

  status = RomaStop();
  EXPECT_TRUE(status.ok());
}
}  // namespace google::scp::roma::test
