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

#include "core/interface/service_interface.h"
#include "cpio/client_providers/interface/job_client_provider_interface.h"

namespace google::scp::cpio {

/**
 * @brief Platform specific factory interface to provide platform specific
 * clients to JobService
 *
 */
class JobServiceFactoryInterface : public core::ServiceInterface {
 public:
  virtual ~JobServiceFactoryInterface() = default;

  /**
   * @brief Creates JobClient.
   *
   * @return std::shared_ptr<client_providers::JobClientProviderInterface>
   * created JobClient.
   */
  virtual std::shared_ptr<client_providers::JobClientProviderInterface>
  CreateJobClient() noexcept = 0;
};
}  // namespace google::scp::cpio
