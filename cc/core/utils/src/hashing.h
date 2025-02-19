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

#include <string>

#include "core/interface/type_def.h"
#include "public/core/interface/execution_result.h"

namespace google::scp::core::utils {
/**
 * @brief Calculates MD5 hash of the input data and sets it in checksum input
 * parameter as a binary string.
 *
 * @param[in] buffer The buffer to calculate the hash.
 * @param[out] checksum The checksum value.
 * @return ExecutionResult The execution result of the operation.
 */
ExecutionResult CalculateMd5Hash(const BytesBuffer& buffer,
                                 std::string& checksum);

// Same as above but accepts a string.
ExecutionResult CalculateMd5Hash(const std::string& buffer,
                                 std::string& checksum);

}  // namespace google::scp::core::utils
