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

#include "cc/core/http2_client/src/http2_client.h"

#include <gtest/gtest.h>

#include <stdint.h>

#include <chrono>
#include <csignal>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <nghttp2/asio_http2_server.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "core/async_executor/mock/mock_async_executor.h"
#include "core/async_executor/src/async_executor.h"
#include "core/interface/async_context.h"
#include "core/test/utils/auto_init_run_stop.h"
#include "core/test/utils/conditional_wait.h"
#include "public/core/interface/execution_result.h"

using namespace nghttp2::asio_http2;          // NOLINT
using namespace nghttp2::asio_http2::server;  // NOLINT
using namespace std::chrono_literals;         // NOLINT
using namespace std::placeholders;            // NOLINT

using google::scp::core::AsyncExecutor;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::async_executor::mock::MockAsyncExecutor;
using google::scp::core::test::AutoInitRunStop;
using std::bind;
using std::future;
using std::make_shared;
using std::promise;
using std::shared_ptr;
using std::string;
using std::thread;
using std::to_string;
using std::vector;
using std::chrono::milliseconds;

namespace google::scp::core {

class RandomGenHandler : std::enable_shared_from_this<RandomGenHandler> {
 public:
  explicit RandomGenHandler(size_t length) : remaining_len_(length) {
    SHA256_Init(&sha256_ctx_);
  }

  ssize_t handle(uint8_t* data, size_t len, uint32_t* data_flags) {
    if (remaining_len_ == 0) {
      uint8_t hash[SHA256_DIGEST_LENGTH];
      SHA256_Final(hash, &sha256_ctx_);
      memcpy(data, hash, sizeof(hash));
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
      return sizeof(hash);
    }
    size_t to_generate = len < remaining_len_ ? len : remaining_len_;
    RAND_bytes(data, to_generate);
    SHA256_Update(&sha256_ctx_, data, to_generate);
    remaining_len_ -= to_generate;
    return to_generate;
  }

 private:
  SHA256_CTX sha256_ctx_;
  size_t remaining_len_;
};

class HttpServer {
 public:
  HttpServer(string address, string port, size_t num_threads)
      : address_(address), port_(port), num_threads_(num_threads) {}

  void Run() {
    boost::system::error_code ec;

    server.num_threads(num_threads_);

    server.handle("/test", [](const request& req, const response& res) {
      res.write_head(200, {{"foo", {"bar"}}});
      res.end("hello, world\n");
    });

    server.handle("/random", [](const request& req, const response& res) {
      const auto& query = req.uri().raw_query;
      if (query.empty()) {
        res.write_head(400u);
        res.end();
        return;
      }
      vector<string> params;
      static auto predicate = [](string::value_type c) { return c == '='; };
      boost::split(params, query, predicate);
      if (params.size() != 2 || params[0] != "length") {
        res.write_head(400u);
        res.end();
        return;
      }
      size_t length = std::stoul(params[1]);
      if (length == 0) {
        res.write_head(400u);
        res.end();
        return;
      }

      res.write_head(200u,
                     {{string("content-length"),
                       {to_string(length + SHA256_DIGEST_LENGTH), false}}});
      auto handler = make_shared<RandomGenHandler>(length);
      res.end(bind(&RandomGenHandler::handle, handler, _1, _2, _3));
    });

    server.listen_and_serve(ec, address_, port_, true);
  }

  int PortInUse() { return server.ports()[0]; }

  http2 server;

 private:
  string address_;
  string port_;
  size_t num_threads_;
};

TEST(HttpClientTest, FailedToConnect) {
  auto request = make_shared<HttpRequest>();
  request->method = HttpMethod::GET;
  request->path = make_shared<string>("http://localhost.failed:8000");
  shared_ptr<AsyncExecutorInterface> async_executor =
      make_shared<MockAsyncExecutor>();
  HttpClient http_client(async_executor);
  async_executor->Init();
  async_executor->Run();
  AutoInitRunStop auto_init_run_stop(http_client);

  promise<void> done;
  AsyncContext<HttpRequest, HttpResponse> context(
      move(request), [&](AsyncContext<HttpRequest, HttpResponse>& context) {
        EXPECT_EQ(
            context.result,
            FailureExecutionResult(
                errors::SC_DISPATCHER_NOT_ENOUGH_TIME_REMAINED_FOR_OPERATION));
        done.set_value();
      });

  EXPECT_EQ(http_client.PerformRequest(context), SuccessExecutionResult());
  done.get_future().get();
  async_executor->Stop();
}

class HttpClientTestII : public ::testing::Test {
 protected:
  void SetUp() override {
    server = make_shared<HttpServer>("localhost", "0", 1);
    server->Run();
    async_executor = make_shared<AsyncExecutor>(2, 1000);
    async_executor->Init();
    async_executor->Run();

    http_client = make_shared<HttpClient>(async_executor);
    EXPECT_EQ(http_client->Init(), SuccessExecutionResult());
    EXPECT_EQ(http_client->Run(), SuccessExecutionResult());
  }

  void TearDown() override {
    EXPECT_EQ(http_client->Stop(), SuccessExecutionResult());
    server->server.stop();
    server->server.join();
    async_executor->Stop();
  }

  shared_ptr<HttpServer> server;
  shared_ptr<AsyncExecutorInterface> async_executor;
  shared_ptr<HttpClient> http_client;
};

void SubmitUntilSuccess(shared_ptr<HttpClient>& http_client,
                        AsyncContext<HttpRequest, HttpResponse>& context) {
  ExecutionResult execution_result = RetryExecutionResult(123);
  while (execution_result.status == ExecutionStatus::Retry) {
    execution_result = http_client->PerformRequest(context);
    std::this_thread::sleep_for(milliseconds(50));
  }
  EXPECT_EQ(execution_result, SuccessExecutionResult());
}

TEST_F(HttpClientTestII, Success) {
  auto request = make_shared<HttpRequest>();
  request->method = HttpMethod::GET;
  request->path = make_shared<string>(
      "http://localhost:" + std::to_string(server->PortInUse()) + "/test");
  promise<void> done;
  AsyncContext<HttpRequest, HttpResponse> context(
      move(request), [&](AsyncContext<HttpRequest, HttpResponse>& context) {
        EXPECT_EQ(context.result, SuccessExecutionResult());
        const auto& bytes = *context.response->body.bytes;
        EXPECT_EQ(string(bytes.begin(), bytes.end()), "hello, world\n");
        done.set_value();
      });

  SubmitUntilSuccess(http_client, context);

  done.get_future().get();
}

TEST_F(HttpClientTestII, FailedToGetResponse) {
  auto request = make_shared<HttpRequest>();
  // Get has no corresponding handler.
  request->path = make_shared<string>("http://localhost:" +
                                      std::to_string(server->PortInUse()));
  promise<void> done;
  AsyncContext<HttpRequest, HttpResponse> context(
      move(request), [&](AsyncContext<HttpRequest, HttpResponse>& context) {
        EXPECT_EQ(context.result,
                  FailureExecutionResult(
                      errors::SC_HTTP2_CLIENT_HTTP_STATUS_NOT_FOUND));
        done.set_value();
      });
  shared_ptr<AsyncExecutorInterface> async_executor =
      make_shared<AsyncExecutor>(2, 1000);

  SubmitUntilSuccess(http_client, context);
  done.get_future().get();
}

TEST_F(HttpClientTestII, SequentialReuse) {
  auto request = make_shared<HttpRequest>();
  request->method = HttpMethod::GET;
  request->path = make_shared<string>(
      "http://localhost:" + std::to_string(server->PortInUse()) + "/test");
  shared_ptr<AsyncExecutorInterface> async_executor =
      make_shared<AsyncExecutor>(2, 1000);

  for (int i = 0; i < 10; ++i) {
    promise<void> done;
    AsyncContext<HttpRequest, HttpResponse> context(
        move(request), [&](AsyncContext<HttpRequest, HttpResponse>& context) {
          EXPECT_EQ(context.result, SuccessExecutionResult());
          const auto& bytes = *context.response->body.bytes;
          EXPECT_EQ(string(bytes.begin(), bytes.end()), "hello, world\n");
          done.set_value();
        });
    SubmitUntilSuccess(http_client, context);
    done.get_future().get();
  }
}

TEST_F(HttpClientTestII, ConcurrentReuse) {
  auto request = make_shared<HttpRequest>();
  request->method = HttpMethod::GET;
  request->path = make_shared<string>(
      "http://localhost:" + std::to_string(server->PortInUse()) + "/test");
  shared_ptr<AsyncExecutorInterface> async_executor =
      make_shared<AsyncExecutor>(2, 1000);

  vector<promise<void>> done;
  for (int i = 0; i < 10; ++i) {
    done.emplace_back();
    AsyncContext<HttpRequest, HttpResponse> context(
        move(request),
        [&, i](AsyncContext<HttpRequest, HttpResponse>& context) {
          EXPECT_EQ(context.result, SuccessExecutionResult());
          const auto& bytes = *context.response->body.bytes;
          EXPECT_EQ(string(bytes.begin(), bytes.end()), "hello, world\n");
          done[i].set_value();
        });
    SubmitUntilSuccess(http_client, context);
  }
  for (auto& p : done) {
    p.get_future().get();
  }
}

// Request /random?length=xxxx and verify the hash of the return.
TEST_F(HttpClientTestII, LargeData) {
  auto request = make_shared<HttpRequest>();
  size_t to_generate = 1048576UL;
  request->path = make_shared<string>(
      "http://localhost:" + std::to_string(server->PortInUse()) +
      "/random?length=" + std::to_string(to_generate));
  promise<void> done;
  AsyncContext<HttpRequest, HttpResponse> context(
      move(request), [&](AsyncContext<HttpRequest, HttpResponse>& context) {
        EXPECT_EQ(context.result, SuccessExecutionResult());
        EXPECT_EQ(context.response->body.length,
                  1048576 + SHA256_DIGEST_LENGTH);
        uint8_t hash[SHA256_DIGEST_LENGTH];
        const auto* data = reinterpret_cast<const uint8_t*>(
            context.response->body.bytes->data());
        SHA256(data, to_generate, hash);
        auto ret = memcmp(hash, data + to_generate, SHA256_DIGEST_LENGTH);
        EXPECT_EQ(ret, 0);
        done.set_value();
      });

  SubmitUntilSuccess(http_client, context);
  done.get_future().get();
}

}  // namespace google::scp::core
