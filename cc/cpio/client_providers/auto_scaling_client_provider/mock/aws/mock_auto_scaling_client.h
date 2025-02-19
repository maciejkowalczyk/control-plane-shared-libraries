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

#include <aws/autoscaling/AutoScalingClient.h>
#include <aws/autoscaling/model/CompleteLifecycleActionRequest.h>
#include <aws/autoscaling/model/DescribeAutoScalingInstancesRequest.h>
#include <gmock/gmock.h>

namespace google::scp::cpio::client_providers::mock {

// Even though this is a mock, the default constructor also calls the default
// constructor of AutoScalingClient which MUST be called after InitSDK.
class MockAutoScalingClient : public Aws::AutoScaling::AutoScalingClient {
 public:
  MOCK_METHOD(
      void, DescribeAutoScalingInstancesAsync,
      (const Aws::AutoScaling::Model::DescribeAutoScalingInstancesRequest&,
       const Aws::AutoScaling::
           DescribeAutoScalingInstancesResponseReceivedHandler&,
       const std::shared_ptr<const Aws::Client::AsyncCallerContext>&),
      (const, override));
  MOCK_METHOD(
      void, CompleteLifecycleActionAsync,
      (const Aws::AutoScaling::Model::CompleteLifecycleActionRequest&,
       const Aws::AutoScaling::CompleteLifecycleActionResponseReceivedHandler&,
       const std::shared_ptr<const Aws::Client::AsyncCallerContext>&),
      (const, override));
};

}  // namespace google::scp::cpio::client_providers::mock
