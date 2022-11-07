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

#include "test_aws_config_client_provider.h"

#include <memory>
#include <string>

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>

#include "cpio/client_providers/global_cpio/src/global_cpio.h"
#include "cpio/client_providers/interface/config_client_provider_interface.h"
#include "cpio/common/aws/test/test_aws_utils.h"
#include "public/cpio/test/test_aws_config_client_options.h"

using Aws::Client::ClientConfiguration;
using google::scp::core::ExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::cpio::common::test::CreateTestClientConfiguration;
using std::make_shared;
using std::shared_ptr;
using std::string;

namespace google::scp::cpio::client_providers {
ExecutionResult TestAwsConfigClientProvider::CreateClientConfiguration(
    shared_ptr<ClientConfiguration>& client_config) noexcept {
  client_config = CreateTestClientConfiguration(ssm_endpoint_override_);
  return SuccessExecutionResult();
}

std::shared_ptr<ConfigClientProviderInterface>
ConfigClientProviderFactory::Create(
    const std::shared_ptr<ConfigClientOptions>& options) {
  return make_shared<TestAwsConfigClientProvider>(
      std::dynamic_pointer_cast<TestAwsConfigClientOptions>(options),
      GlobalCpio::GetGlobalCpio()->GetInstanceClientProvider(),
      GlobalCpio::GetGlobalCpio()->GetMessageRouter());
}
}  // namespace google::scp::cpio::client_providers
