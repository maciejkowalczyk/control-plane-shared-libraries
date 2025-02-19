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
#   JSON implementation in C

load("@rules_foreign_cc//foreign_cc:defs.bzl", "cmake")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

filegroup(
    name = "all_srcs",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

# We use cmake instead of bazel as the cmake file in this repo is complex.
cmake(
    name = "s2n_tls",
    cache_entries = {
        "BUILD_TESTING": "0",
        "DISABLE_WERROR": "ON",
        "S2N_LIBCRYPTO": "boringssl",
        "BUILD_SHARED_LIBS": "ON",
        "BUILD_S2N": "true",
    },
    includes = [
        "include",
    ],
    lib_source = ":all_srcs",
    out_shared_libs = [
        "libs2n.so",
        "libs2n.so.1",
        "libs2n.so.1.0.0",
    ],
    deps = [
        "@boringssl//:crypto",
        "@boringssl//:ssl",
    ],
)
