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

java_test(
    name = "JobRequestIdLengthValidatorTest",
    srcs = ["JobRequestIdLengthValidatorTest.java"],
    deps = [
        "//java/com/google/scp/operator/frontend/tasks/validation",
        "//java/com/google/scp/operator/shared/dao/metadatadb/testing:job_generator",
        "//java/com/google/scp/operator/shared/model",
        "//java/external:google_truth8",
        "//java/external:guava",
        "//operator/protos/shared/backend:java_proto",
    ],
)

java_test(
    name = "JobRequestIdCharactersValidatorTest",
    srcs = ["JobRequestIdCharactersValidatorTest.java"],
    deps = [
        "//java/com/google/scp/operator/frontend/tasks/validation",
        "//java/com/google/scp/operator/shared/dao/metadatadb/testing:job_generator",
        "//java/com/google/scp/operator/shared/model",
        "//java/external:google_truth8",
        "//java/external:guava",
        # Needs snakeyaml dependency here to choose the right snakeyaml version.
        "//java/external:snakeyaml",
        "//java/external:test_parameter_injector",
        "//operator/protos/shared/backend:java_proto",
    ],
)
