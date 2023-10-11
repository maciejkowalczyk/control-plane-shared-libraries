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

#include "metric_utils.h"

#include <utility>

using std::make_shared;
using std::move;
using std::shared_ptr;
using std::string;
using std::vector;

namespace {
constexpr char kMethodName[] = "MethodName";
constexpr char kComponentName[] = "ComponentName";

}  // namespace

namespace google::scp::cpio {

void MetricUtils::GetPutMetricsRequest(
    shared_ptr<cmrt::sdk::metric_service::v1::PutMetricsRequest>&
        record_metric_request,
    const MetricDefinition& metric_info,
    const MetricValue& metric_value) noexcept {
  auto metric = record_metric_request->add_metrics();
  metric->set_value(metric_value);
  metric->set_name(metric_info.name);
  metric->set_unit(
      client_providers::MetricClientUtils::ConvertToMetricUnitProto(
          metric_info.unit));

  // Adds the labels from metric_info and additional_labels.
  auto labels = metric->mutable_labels();
  if (metric_info.labels.size() > 0) {
    for (const auto& label : metric_info.labels) {
      labels->insert(protobuf::MapPair(label.first, label.second));
    }
  }

  *metric->mutable_timestamp() = protobuf::util::TimeUtil::GetCurrentTime();

  record_metric_request->set_metric_namespace(metric_info.metric_namespace);
}

MetricLabels MetricUtils::CreateMetricLabelsWithComponentSignature(
    string component_name, string method_name) noexcept {
  MetricLabels labels;
  labels[kComponentName] = move(component_name);
  if (!method_name.empty()) {
    labels[kMethodName] = move(method_name);
  }

  return labels;
}
}  // namespace google::scp::cpio
