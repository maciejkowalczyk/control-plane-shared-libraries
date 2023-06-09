# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Description:
#   AWS C HTTP/1.1 and HTTP/2 implementation

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

cc_library(
    name = "aws_c_http",
    srcs = glob([
        "include/aws/http/*.h",
        "include/aws/http/private/*.h",
        "source/*.c",
    ]),
    hdrs = glob([
        "include/aws/http/private/*.def",
    ]),
    includes = [
        "include",
    ],
    deps = [
        "@aws_c_cal",
        "@aws_c_common",
        "@aws_c_compression",
        "@aws_c_io",
    ],
)
