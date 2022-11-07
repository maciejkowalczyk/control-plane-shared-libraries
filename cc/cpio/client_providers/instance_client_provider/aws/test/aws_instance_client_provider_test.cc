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

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <aws/core/Aws.h>
#include <aws/core/utils/Outcome.h>
#include <aws/ec2/EC2Client.h>
#include <aws/ec2/EC2Errors.h>
#include <aws/ec2/model/DescribeInstancesRequest.h>
#include <aws/ec2/model/DescribeTagsRequest.h>

#include "cpio/client_providers/instance_client_provider/aws/mock/mock_aws_instance_client_provider_with_overrides.h"
#include "cpio/client_providers/instance_client_provider/aws/mock/mock_ec2_client.h"
#include "cpio/client_providers/instance_client_provider/aws/mock/mock_ec2_metadata_client.h"
#include "cpio/client_providers/instance_client_provider/aws/src/error_codes.h"
#include "cpio/common/aws/src/error_codes.h"
#include "public/core/interface/execution_result.h"

using Aws::InitAPI;
using Aws::SDKOptions;
using Aws::ShutdownAPI;
using Aws::Client::AWSError;
using Aws::EC2::EC2Errors;
using Aws::EC2::Model::DescribeTagsOutcome;
using Aws::EC2::Model::DescribeTagsRequest;
using Aws::EC2::Model::DescribeTagsResponse;
using Aws::EC2::Model::Filter;
using Aws::EC2::Model::TagDescription;
using google::scp::core::FailureExecutionResult;
using google::scp::core::SuccessExecutionResult;
using google::scp::core::errors::
    SC_AWS_INSTANCE_CLIENT_PROVIDER_INVALID_INSTANCE_ID;
using google::scp::core::errors::
    SC_AWS_INSTANCE_CLIENT_PROVIDER_INVALID_TAG_NAME;
using google::scp::core::errors::
    SC_AWS_INSTANCE_CLIENT_PROVIDER_MULTIPLE_TAG_VALUES_FOUND;
using google::scp::core::errors::
    SC_AWS_INSTANCE_CLIENT_PROVIDER_RESOURCE_NOT_FOUND;
using google::scp::core::errors::
    SC_AWS_INSTANCE_CLIENT_PROVIDER_TAG_VALUE_NOT_FOUND;
using google::scp::core::errors::SC_AWS_INTERNAL_SERVICE_ERROR;
using google::scp::cpio::client_providers::mock::
    MockAwsInstanceClientProviderWithOverrides;
using std::make_unique;
using std::string;
using std::unique_ptr;

static constexpr char kInstanceId[] = "instance_id";
static constexpr char kRegion[] = "us-west-1";
static constexpr char kPublicIp[] = "public_ip";
static constexpr char kPrivateIp[] = "private_ip";
static constexpr char kEnvTag[] = "environment";
static constexpr char kEnvName[] = "env_name";

namespace google::scp::cpio::cloud_providers::test {
class AwsInstanceClientProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    aws_instance_client_provider_mock_ =
        make_unique<MockAwsInstanceClientProviderWithOverrides>();
    EXPECT_EQ(aws_instance_client_provider_mock_->Init(),
              SuccessExecutionResult());
    aws_instance_client_provider_mock_->GetEC2MetadataClient()
        ->resource_path_mock = "/latest/meta-data/placement/region";
    aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
        string(kRegion);
    EXPECT_EQ(aws_instance_client_provider_mock_->Run(),
              SuccessExecutionResult());

    // Mocks DescirbeTagsRequest.
    DescribeTagsRequest describe_tags_request;
    Filter resource_id_filter;
    resource_id_filter.SetName("resource-id");
    resource_id_filter.AddValues(kInstanceId);
    describe_tags_request.AddFilters(resource_id_filter);
    Filter key_filter;
    key_filter.SetName("key");
    key_filter.AddValues(kEnvTag);
    describe_tags_request.AddFilters(key_filter);
    aws_instance_client_provider_mock_->GetEC2Client()
        ->describe_tags_request_mock = describe_tags_request;
  }

  void TearDown() override {
    EXPECT_EQ(aws_instance_client_provider_mock_->Stop(),
              SuccessExecutionResult());
  }

  static void SetUpTestSuite() {
    SDKOptions options;
    InitAPI(options);
  }

  static void TearDownTestSuite() {
    SDKOptions options;
    ShutdownAPI(options);
  }

  unique_ptr<MockAwsInstanceClientProviderWithOverrides>
      aws_instance_client_provider_mock_;
};

TEST_F(AwsInstanceClientProviderTest, SucceededToFetchInstanceId) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/instance-id";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      string(kInstanceId);

  string instance_id;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstanceId(instance_id),
            SuccessExecutionResult());
  EXPECT_EQ(instance_id, string(kInstanceId));
}

TEST_F(AwsInstanceClientProviderTest, InstanceIdNotFound) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/instance-id";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      "";
  string instance_id;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstanceId(instance_id),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_RESOURCE_NOT_FOUND));
}

TEST_F(AwsInstanceClientProviderTest, SucceededToFetchRegion) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/placement/region";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      string(kRegion);

  string region;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetRegion(region),
            SuccessExecutionResult());
  EXPECT_EQ(region, string(kRegion));
}

TEST_F(AwsInstanceClientProviderTest, RegionNotFound) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/placement/region";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      "";
  string region;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetRegion(region),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_RESOURCE_NOT_FOUND));
}

TEST_F(AwsInstanceClientProviderTest, SucceededToFetchPublicIp) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/public-ipv4";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      string(kPublicIp);

  string public_ip;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstancePublicIpv4Address(
                public_ip),
            SuccessExecutionResult());
  EXPECT_EQ(public_ip, string(kPublicIp));
}

TEST_F(AwsInstanceClientProviderTest, PublicIpNotFound) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/public-ipv4";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      "";
  string public_ip;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstancePublicIpv4Address(
                public_ip),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_RESOURCE_NOT_FOUND));
}

TEST_F(AwsInstanceClientProviderTest, SucceededToFetchPrivateIp) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/local-ipv4";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      string(kPrivateIp);

  string private_ip;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstancePrivateIpv4Address(
                private_ip),
            SuccessExecutionResult());
  EXPECT_EQ(private_ip, string(kPrivateIp));
}

TEST_F(AwsInstanceClientProviderTest, PrivateIpNotFound) {
  aws_instance_client_provider_mock_->GetEC2MetadataClient()
      ->resource_path_mock = "/latest/meta-data/local-ipv4";
  aws_instance_client_provider_mock_->GetEC2MetadataClient()->resource_mock =
      "";
  string private_ip;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetInstancePrivateIpv4Address(
                private_ip),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_RESOURCE_NOT_FOUND));
}

TEST_F(AwsInstanceClientProviderTest, SucceededToFetchEnvName) {
  DescribeTagsResponse response;
  TagDescription tag;
  tag.SetValue(kEnvName);
  response.AddTags(tag);
  DescribeTagsOutcome describe_tags_outcome(response);
  aws_instance_client_provider_mock_->GetEC2Client()
      ->describe_tags_outcome_mock = describe_tags_outcome;

  string name;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetEnvironmentName(
                name, kEnvTag, kInstanceId),
            SuccessExecutionResult());
  EXPECT_EQ(name, string(kEnvName));
}

TEST_F(AwsInstanceClientProviderTest, EnvTagNotSpecified) {
  string name;
  EXPECT_EQ(
      aws_instance_client_provider_mock_->GetEnvironmentName(name, "",
                                                             kInstanceId),
      FailureExecutionResult(SC_AWS_INSTANCE_CLIENT_PROVIDER_INVALID_TAG_NAME));
}

TEST_F(AwsInstanceClientProviderTest, InstanceIdNotSpecified) {
  string name;
  EXPECT_EQ(
      aws_instance_client_provider_mock_->GetEnvironmentName(name, kEnvTag, ""),
      FailureExecutionResult(
          SC_AWS_INSTANCE_CLIENT_PROVIDER_INVALID_INSTANCE_ID));
}

TEST_F(AwsInstanceClientProviderTest, FailedToFetchEnvName) {
  AWSError<EC2Errors> error(EC2Errors::INTERNAL_FAILURE, false);
  DescribeTagsOutcome outcome(error);
  aws_instance_client_provider_mock_->GetEC2Client()
      ->describe_tags_outcome_mock = outcome;

  string name;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetEnvironmentName(
                name, kEnvTag, kInstanceId),
            FailureExecutionResult(SC_AWS_INTERNAL_SERVICE_ERROR));
}

TEST_F(AwsInstanceClientProviderTest, NoEnvNameFound) {
  DescribeTagsResponse response;
  DescribeTagsOutcome outcome(response);
  aws_instance_client_provider_mock_->GetEC2Client()
      ->describe_tags_outcome_mock = outcome;

  string name;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetEnvironmentName(
                name, kEnvTag, kInstanceId),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_TAG_VALUE_NOT_FOUND));
}

TEST_F(AwsInstanceClientProviderTest, MultipleEnvNamesFound) {
  DescribeTagsResponse response;
  TagDescription tag_1;
  const char value_1[] = "env_name_1";
  tag_1.SetValue(value_1);
  response.AddTags(tag_1);
  TagDescription tag_2;
  const char value_2[] = "env_name_2";
  tag_2.SetValue(value_2);
  response.AddTags(tag_2);
  DescribeTagsOutcome outcome(response);
  aws_instance_client_provider_mock_->GetEC2Client()
      ->describe_tags_outcome_mock = outcome;

  string name;
  EXPECT_EQ(aws_instance_client_provider_mock_->GetEnvironmentName(
                name, kEnvTag, kInstanceId),
            FailureExecutionResult(
                SC_AWS_INSTANCE_CLIENT_PROVIDER_MULTIPLE_TAG_VALUES_FOUND));
}
}  // namespace google::scp::cpio::cloud_providers::test
