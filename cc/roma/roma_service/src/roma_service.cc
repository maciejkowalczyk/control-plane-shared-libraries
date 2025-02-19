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

#include "roma_service.h"

#include <linux/limits.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include "include/libplatform/libplatform.h"

using google::scp::core::ExecutionResult;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using std::make_unique;
using std::min;
using std::string;
using std::thread;
using std::to_string;
using std::unique_ptr;

static constexpr char kWasmMemPagesFlag[] = "--wasm_max_mem_pages=";
static constexpr size_t kMaxNumberOfWasm32BitMemPages = 65536;

namespace google::scp::roma::roma_service {

using dispatcher::Dispatcher;
using ipc::IpcManager;
using worker::WorkerPool;

RomaService* RomaService::instance_ = nullptr;

ExecutionResult RomaService::Init() noexcept {
  int my_pid = getpid();
  string proc_exe_path = string("/proc/") + to_string(my_pid) + "/exe";
  auto my_path = std::make_unique<char[]>(PATH_MAX);
  readlink(proc_exe_path.c_str(), my_path.get(), PATH_MAX);
  v8::V8::InitializeICUDefaultLocation(my_path.get());
  v8::V8::InitializeExternalStartupData(my_path.get());

  // Set the max number of WASM memory pages
  if (config_.max_wasm_memory_number_of_pages != 0) {
    auto page_count = min(config_.max_wasm_memory_number_of_pages,
                          kMaxNumberOfWasm32BitMemPages);
    auto flag_value = string(kWasmMemPagesFlag) + to_string(page_count);
    v8::V8::SetFlagsFromString(flag_value.c_str());
  }

  static v8::Platform* platform = [] {
      std::unique_ptr<v8::Platform> v8_platform =
          v8::platform::NewDefaultPlatform();
      v8::V8::InitializePlatform(v8_platform.get());
      v8::V8::Initialize();
      return v8_platform.release();
    }();

  auto* ipc_manager_ = IpcManager::Create(config_);
  auto result = ipc_manager_->Init();
  if (!result.Successful()) {
    return result;
  }
  dispatcher_ = make_unique<class Dispatcher>(*ipc_manager_);
  result = dispatcher_->Init();
  if (!result.Successful()) {
    return result;
  }

  worker_pool_ = make_unique<WorkerPool>(config_);
  result = worker_pool_->Init();
  if (!result.Successful()) {
    return result;
  }
  return SuccessExecutionResult();
}

ExecutionResult RomaService::Run() noexcept {
  auto result = IpcManager::Instance()->Run();
  if (!result.Successful()) {
    return result;
  }
  result = dispatcher_->Run();
  if (!result.Successful()) {
    return result;
  }
  result = worker_pool_->Run();
  if (!result.Successful()) {
    return result;
  }
  return SuccessExecutionResult();
}

ExecutionResult RomaService::Stop() noexcept {
  // Make sure the dispatcher response poller threads and the worker processes
  // can exit. This makes sure blocking calls to the IpcChannel return.
  IpcManager::Instance()->ReleaseLocks();

  // Stop worker pool first
  auto result = worker_pool_->Stop();
  if (!result.Successful()) {
    return result;
  }
  // Then dispatcher
  result = dispatcher_->Stop();
  if (!result.Successful()) {
    return result;
  }
  // Then ipc_manager
  result = IpcManager::Instance()->Stop();
  if (!result.Successful()) {
    return result;
  }
  return SuccessExecutionResult();
}

}  // namespace google::scp::roma::roma_service
