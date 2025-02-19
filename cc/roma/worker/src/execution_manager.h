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

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/interface/service_interface.h"
#include "core/interface/type_def.h"
#include "include/v8.h"
#include "roma/ipc/src/ipc_message.h"

#include "error_codes.h"
#include "execution_watchdog.h"

namespace google::scp::roma::worker {
/**
 * @brief ExecutionManager leverages V8 to persist code objects in code update
 * requests and handles executable requests with the cached environment to
 * improve the performance. For JS code, ExecutionManager uses V8 Snapshot or V8
 * UnboundScript to persist the code. Current, no code persist for WASM code
 * request.
 */
class ExecutionManager : public core::ServiceInterface {
 public:
  ExecutionManager(
      const JsEngineResourceConstraints& v8_resource_constraints,
      const std::vector<std::shared_ptr<FunctionBindingObjectBase>>&
          function_bindings);

  ~ExecutionManager() {
    if (startup_data_.data) {
      delete[] startup_data_.data;
    }
    unbound_script_.Reset();
  }

  core::ExecutionResult Init() noexcept override;

  core::ExecutionResult Run() noexcept override;

  core::ExecutionResult Stop() noexcept override;

  /**
   * @brief Create a StartupData blob or Global UnboundScript for input code
   * object.
   *
   * @param code_obj code object to be compiled and run.
   * @param err_msg Error message.
   * @return core::ExecutionResult
   */
  core::ExecutionResult Create(const ipc::RomaCodeObj& code_obj,
                               common::RomaString& err_msg) noexcept;

  /**
   * @brief Process the code_obj with the default context in the isolate created
   * by ExecutionManager::Create().
   *
   * @param isolate The input isolate must have a default context which
   * containing valid pre-compiled and run handlers.
   * @param code_obj The code object to be run in the isolate. Uses the handler
   * name and input to call Handler function.
   * @param output Handler function execution output.
   * @param err_msg Error message.
   * @return core::ExecutionResult
   */
  core::ExecutionResult Process(const ipc::RomaCodeObj& code_obj,
                                common::RomaString& output,
                                common::RomaString& err_msg) noexcept;

  /**
   * @brief Get V8 Heap Statistics information
   *
   * @param[out] v8_heap_stats
   */
  void GetV8HeapStatistics(v8::HeapStatistics& v8_heap_stats) noexcept;

 private:
  /**
   * @brief Set up context based on code type and get handler.
   *
   * @param code_obj
   * @param[out] handler
   * @param err_msg
   * @return core::ExecutionResult
   */
  core::ExecutionResult SetUpContextAndGetHandler(
      const ipc::RomaCodeObj& code_obj, v8::Local<v8::Value>& handler,
      common::RomaString& err_msg) noexcept;

  /// @brief Create a v8 isolate instance.
  virtual void CreateV8Isolate(const intptr_t* external_references) noexcept;

  /// @brief Dispose v8 isolate.
  virtual void DisposeV8Isolate() noexcept;

  /// v8 heap resource constraints.
  const JsEngineResourceConstraints v8_resource_constraints_;

  /// @brief User-registered C++/JS function bindings
  const std::vector<std::shared_ptr<FunctionBindingObjectBase>>
      function_bindings_;

  /// @brief These are external references (pointers to data outside of the v8
  /// heap) which are needed for serialization of the v8 snapshot.
  std::vector<intptr_t> external_references_;

  /// The type of the code content, including JavaScript,  WASM, and
  /// JavaScript Mixed with WASM.
  enum class CodeType { kUnknown, kJs, kWasm, kJsWasmMixed };

  /// The code type, either javascript or WASM.
  CodeType code_type_{CodeType::kJs};

  /// The startup data to hold the code snapshot.
  v8::StartupData startup_data_{nullptr, 0};

  /// An instance of UnboundScript used to cache compiled code in isolate.
  v8::Global<v8::UnboundScript> unbound_script_;

  /// @brief An instance of v8 isolate.
  v8::Isolate* v8_isolate_{nullptr};

  /// @brief A timer thread watches the code execution in v8 isolate and
  /// timeouts the execution in set time.
  std::unique_ptr<ExecutionWatchDog> execution_watchdog_{nullptr};

  /// @brief Version num of the code object compiled and stored in
  /// execution_manager_.
  uint64_t code_version_num_{0};

  /// @brief Temp solution for wasm source code cache.
  std::string wasm_code_;
};
}  // namespace google::scp::roma::worker
