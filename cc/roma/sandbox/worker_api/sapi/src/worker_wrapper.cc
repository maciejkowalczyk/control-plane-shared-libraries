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

#include "worker_wrapper.h"

#include <stdint.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "core/common/time_provider/src/stopwatch.h"
#include "core/interface/errors.h"
#include "roma/config/src/config.h"
#include "roma/logging/src/logging.h"
#include "roma/sandbox/constants/constants.h"
#include "roma/sandbox/worker_factory/src/worker_factory.h"

using absl::flat_hash_map;
using absl::MakeConstSpan;
using absl::Span;
using absl::StatusOr;
using absl::string_view;
using google::scp::core::StatusCode;
using google::scp::core::common::Stopwatch;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_INIT_DATA;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_RUN_CODE_DATA;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_COULD_NOT_SERIALIZE_RUN_CODE_RESPONSE_DATA;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_FAILED_CREATE_BUFFER_INSIDE_SANDBOXEE;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_RESPONSE_DATA_SIZE_LARGER_THAN_BUFFER_CAPACITY;
using google::scp::core::errors::SC_ROMA_WORKER_API_UNINITIALIZED_WORKER;
using google::scp::core::errors::
    SC_ROMA_WORKER_API_VALID_SANDBOX_BUFFER_REQUIRED;
using google::scp::roma::JsEngineResourceConstraints;
using google::scp::roma::sandbox::constants::kBadFd;
using google::scp::roma::sandbox::constants::kExecutionMetricJsEngineCallNs;
using google::scp::roma::sandbox::worker::Worker;
using google::scp::roma::sandbox::worker::WorkerFactory;
using sandbox2::Buffer;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {
// the pointer of the data shared sandbox2::Buffer which is used to share
// data between the host process and the sandboxee.
unique_ptr<Buffer> sandbox_data_shared_buffer_ptr_{nullptr};
size_t request_and_response_data_buffer_size_bytes_{0};

shared_ptr<Worker> worker_{nullptr};

StatusCode Init(worker_api::WorkerInitParamsProto* init_params) {
  if (worker_) {
    Stop();
  }

  auto worker_engine = static_cast<WorkerFactory::WorkerEngine>(
      init_params->worker_factory_js_engine());

  WorkerFactory::FactoryParams factory_params;
  factory_params.engine = worker_engine;
  factory_params.require_preload =
      init_params->require_code_preload_for_execution();
  factory_params.compilation_context_cache_size =
      init_params->compilation_context_cache_size();

  if (worker_engine == WorkerFactory::WorkerEngine::v8) {
    vector<string> native_js_function_names(
        init_params->native_js_function_names().begin(),
        init_params->native_js_function_names().end());

    JsEngineResourceConstraints resource_constraints;
    resource_constraints.initial_heap_size_in_mb =
        static_cast<size_t>(init_params->js_engine_initial_heap_size_mb());
    resource_constraints.maximum_heap_size_in_mb =
        static_cast<size_t>(init_params->js_engine_maximum_heap_size_mb());

    WorkerFactory::V8WorkerEngineParams v8_params{
        .native_js_function_comms_fd =
            init_params->native_js_function_comms_fd(),
        .native_js_function_names = native_js_function_names,
        .resource_constraints = resource_constraints,
        .max_wasm_memory_number_of_pages = static_cast<size_t>(
            init_params->js_engine_max_wasm_memory_number_of_pages())};

    factory_params.v8_worker_engine_params = v8_params;
  }

  auto worker_or = WorkerFactory::Create(factory_params);
  if (!worker_or.result().Successful()) {
    return worker_or.result().status_code;
  }

  if (init_params->request_and_response_data_buffer_fd() == kBadFd) {
    return SC_ROMA_WORKER_API_VALID_SANDBOX_BUFFER_REQUIRED;
  }
  // create Buffer from file descriptor.
  auto buffer =
      Buffer::CreateFromFd(init_params->request_and_response_data_buffer_fd());
  if (!buffer.ok()) {
    return SC_ROMA_WORKER_API_FAILED_CREATE_BUFFER_INSIDE_SANDBOXEE;
  }

  sandbox_data_shared_buffer_ptr_ = std::move(buffer).value();
  request_and_response_data_buffer_size_bytes_ =
      init_params->request_and_response_data_buffer_size_bytes();

  worker_ = *worker_or;
  ROMA_VLOG(1) << "Worker wrapper successfully created the worker";
  return worker_->Init().status_code;
}

StatusCode RunCode(worker_api::WorkerParamsProto* params) {
  if (!worker_) {
    return SC_ROMA_WORKER_API_UNINITIALIZED_WORKER;
  }

  const auto& code = params->code();
  vector<string_view> input;
  for (int i = 0; i < params->input_size(); i++) {
    input.push_back(params->input().at(i));
  }
  flat_hash_map<string, string> metadata;
  for (auto&& element : params->metadata()) {
    metadata[element.first] = element.second;
  }
  auto wasm_bin = reinterpret_cast<const uint8_t*>(params->wasm().c_str());
  Span<const uint8_t> wasm = MakeConstSpan(wasm_bin, params->wasm().length());

  Stopwatch stopwatch;
  stopwatch.Start();
  auto response_or = worker_->RunCode(code, input, metadata, wasm);
  auto run_code_elapsed_ns = stopwatch.Stop();
  (*params->mutable_metrics())[kExecutionMetricJsEngineCallNs] =
      run_code_elapsed_ns.count();

  if (!response_or.result().Successful()) {
    return response_or.result().status_code;
  }

  for (const auto& pair : response_or.value().metrics) {
    (*params->mutable_metrics())[pair.first] = pair.second;
  }

  params->set_response(*response_or.value().response);
  return SC_OK;
}

}  // namespace

StatusCode InitFromSerializedData(sapi::LenValStruct* data) {
  worker_api::WorkerInitParamsProto init_params;
  if (!init_params.ParseFromArray(data->data, data->size)) {
    return SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_INIT_DATA;
  }

  ROMA_VLOG(1) << "Worker wrapper successfully received the init data";
  return Init(&init_params);
}

StatusCode Run() {
  if (!worker_) {
    return SC_ROMA_WORKER_API_UNINITIALIZED_WORKER;
  }

  return worker_->Run().status_code;
}

StatusCode Stop() {
  if (!worker_) {
    return SC_ROMA_WORKER_API_UNINITIALIZED_WORKER;
  }
  auto result = worker_->Stop();
  worker_.reset();
  return result.status_code;
}

StatusCode RunCodeFromSerializedData(sapi::LenValStruct* data,
                                     int input_serialized_size,
                                     size_t* output_serialized_size) {
  ROMA_VLOG(1)
      << "Worker wrapper RunCodeFromSerializedData() received the request";

  if (!sandbox_data_shared_buffer_ptr_) {
    return SC_ROMA_WORKER_API_VALID_SANDBOX_BUFFER_REQUIRED;
  }

  worker_api::WorkerParamsProto params;

  // If input_serialized_size is greater than zero, then the data is shared by
  // the buffer.
  if (input_serialized_size > 0) {
    if (!params.ParseFromArray(sandbox_data_shared_buffer_ptr_->data(),
                               input_serialized_size)) {
      LOG(ERROR) << "Could not deserialize run_code request from sandbox "
                    "buffer. The input_serialized_size in Bytes is "
                 << input_serialized_size;
      return SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_RUN_CODE_DATA;
    }
  } else if (!params.ParseFromArray(data->data, data->size)) {
    LOG(ERROR) << "Could not deserialize run_code request from "
                  "sapi::LenValStruct* with data size "
               << data->size;
    return SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_RUN_CODE_DATA;
  }

  auto result = RunCode(&params);
  if (result != SC_OK) {
    return result;
  }

  // Don't return the input or code
  params.clear_code();
  params.clear_input();

  auto serialized_size = params.ByteSizeLong();

  if (serialized_size < request_and_response_data_buffer_size_bytes_) {
    ROMA_VLOG(1) << "Response data sharing with Buffer";

    // Write the response into Buffer.
    if (!params.SerializeToArray(sandbox_data_shared_buffer_ptr_->data(),
                                 serialized_size)) {
      LOG(ERROR) << "Failed to serialize run_code response into buffer with "
                    "serialized_size in Bytes "
                 << serialized_size;
      return SC_ROMA_WORKER_API_COULD_NOT_SERIALIZE_RUN_CODE_RESPONSE_DATA;
    }

    *output_serialized_size = serialized_size;

  } else {
    ROMA_VLOG(1) << "Response serialized size " << serialized_size
                 << "Bytes is larger than the Buffer capacity "
                 << request_and_response_data_buffer_size_bytes_
                 << "Bytes. Data sharing with Bytes";

    uint8_t* serialized_data = static_cast<uint8_t*>(malloc(serialized_size));
    if (!serialized_data) {
      LOG(ERROR) << "Failed to malloc uint8_t* serialized_data with size of "
                 << serialized_size;
      return SC_ROMA_WORKER_API_COULD_NOT_SERIALIZE_RUN_CODE_RESPONSE_DATA;
    }

    if (!params.SerializeToArray(serialized_data, serialized_size)) {
      LOG(ERROR) << "Failed to serialize run_code response into uint8_t* "
                    "serialized_data with serialized_size of "
                 << serialized_size;
      // In this failure scenario, free the buffer.
      // We don't free it otherwise, as it is free'd in the destructor of the
      // LenValStruct* passed to this function, which is owned by SAPI.
      free(serialized_data);
      return SC_ROMA_WORKER_API_COULD_NOT_SERIALIZE_RUN_CODE_RESPONSE_DATA;
    }

    // Free old data
    free(data->data);

    data->data = serialized_data;
    data->size = serialized_size;

    // output_serialized_size set to 0 to indicate the response shared by Bytes.
    *output_serialized_size = 0;
  }

  ROMA_VLOG(1) << "Worker wrapper successfully executed the request";
  return result;
}

StatusCode RunCodeFromBuffer(int input_serialized_size,
                             size_t* output_serialized_size) {
  ROMA_VLOG(1) << "Worker wrapper RunCodeFromBuffer() received the request";

  if (!sandbox_data_shared_buffer_ptr_) {
    return SC_ROMA_WORKER_API_VALID_SANDBOX_BUFFER_REQUIRED;
  }

  worker_api::WorkerParamsProto params;
  if (!params.ParseFromArray(sandbox_data_shared_buffer_ptr_->data(),
                             input_serialized_size)) {
    LOG(ERROR) << "Could not deserialize run_code request from sandbox";
    return SC_ROMA_WORKER_API_COULD_NOT_DESERIALIZE_RUN_CODE_DATA;
  }

  ROMA_VLOG(1) << "Worker wrapper successfully received the request data";
  auto result = RunCode(&params);
  if (result != SC_OK) {
    return result;
  }

  // Don't return the input or code
  params.clear_code();
  params.clear_input();

  auto serialized_size = params.ByteSizeLong();
  if (serialized_size > request_and_response_data_buffer_size_bytes_) {
    LOG(ERROR) << "Serialized data size " << serialized_size
               << " Bytes is larger than Buffer capacity "
               << request_and_response_data_buffer_size_bytes_ << " Bytes.";
    return SC_ROMA_WORKER_API_RESPONSE_DATA_SIZE_LARGER_THAN_BUFFER_CAPACITY;
  }

  if (!params.SerializeToArray(sandbox_data_shared_buffer_ptr_->data(),
                               serialized_size)) {
    LOG(ERROR) << "Failed to serialize run_code response into buffer";
    return SC_ROMA_WORKER_API_COULD_NOT_SERIALIZE_RUN_CODE_RESPONSE_DATA;
  }

  *output_serialized_size = serialized_size;

  ROMA_VLOG(1) << "Worker wrapper successfully executed the request";
  return result;
}
