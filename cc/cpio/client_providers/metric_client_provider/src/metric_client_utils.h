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

#include <map>

#include "cpio/proto/metric_client.pb.h"
#include "public/core/interface/execution_result.h"
#include "public/cpio/interface/metric_client/type_def.h"

namespace google::scp::cpio::client_providers {
class MetricClientUtils {
 public:
  static metric_client::MetricUnitProto ConvertToMetricUnitProto(
      MetricUnit metric_unit);

  static core::ExecutionResult ValidateRequest(
      const metric_client::RecordMetricsProtoRequest& request);
};
}  // namespace google::scp::cpio::client_providers
