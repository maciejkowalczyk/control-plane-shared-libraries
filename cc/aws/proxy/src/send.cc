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

#include "proxy/src/send.h"

#include <sys/socket.h>

#include "proxy/src/logging.h"

namespace google::scp::proxy {
bool Send(SocketHandle handle, const void* data, ssize_t size) {
  // Send message (blocking)
  ssize_t bytes_sent = 0;
  ssize_t result = 0;
  auto* buffer = static_cast<const uint8_t*>(data);
  while (bytes_sent != size) {
    result = send(handle, buffer + bytes_sent, size - bytes_sent, 0);
    if (result == -1) {
      break;
    }
    bytes_sent += result;
  }
  if (result != -1) {
    // All sent, no issues
    return true;
  }

  // Handle socket error if any
  LogError("ERROR: Cannot send message, errno=", errno);
  return false;
}
}  // namespace google::scp::proxy
