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

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "v8_basic",
    size = "small",
    srcs = ["v8_basic.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "@com_google_googletest//:gtest_main",
        "@v8//:v8_icu",
    ],
)

cc_test(
    name = "shared_memory_pool_test",
    size = "small",
    srcs = ["shared_memory_pool_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "process_test",
    size = "small",
    srcs = ["process_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/public/core/test/interface:execution_result_matchers",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "shm_allocator_test",
    size = "small",
    srcs = ["shm_allocator_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "work_queue_test",
    size = "small",
    srcs = ["work_queue_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/public/core/test/interface:execution_result_matchers",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "shm_semaphore_test",
    size = "small",
    srcs = ["shm_semaphore_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/public/core/test/interface:execution_result_matchers",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "map_test",
    size = "small",
    srcs = ["map_test.cc"],
    copts = [
        "-std=c++17",
    ],
    deps = [
        "//cc/core/test/utils:utils_lib",
        "//cc/roma/common/src:roma_common_lib",
        "@com_google_googletest//:gtest_main",
    ],
)
