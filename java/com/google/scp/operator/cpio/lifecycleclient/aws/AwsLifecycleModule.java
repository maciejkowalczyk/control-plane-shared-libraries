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

package com.google.scp.operator.cpio.lifecycleclient.aws;

import com.google.inject.BindingAnnotation;
import com.google.inject.Provides;
import com.google.inject.Singleton;
import com.google.scp.operator.cpio.lifecycleclient.LifecycleClient;
import com.google.scp.operator.cpio.lifecycleclient.LifecycleModule;
import com.google.scp.operator.shared.dao.asginstancesdb.aws.DynamoAsgInstancesDb.AsgInstancesDbDynamoClient;
import com.google.scp.operator.shared.dao.asginstancesdb.aws.DynamoAsgInstancesDb.AsgInstancesDbDynamoTableName;
import com.google.scp.operator.shared.dao.asginstancesdb.aws.DynamoAsgInstancesDb.AsgInstancesDbDynamoTtlDays;
import com.google.scp.shared.clients.configclient.Annotations.ApplicationRegionBinding;
import com.google.scp.shared.clients.configclient.ParameterClient;
import com.google.scp.shared.clients.configclient.ParameterClient.ParameterClientException;
import com.google.scp.shared.clients.configclient.model.WorkerParameter;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.net.URI;
import software.amazon.awssdk.auth.credentials.AwsCredentialsProvider;
import software.amazon.awssdk.core.client.config.ClientOverrideConfiguration;
import software.amazon.awssdk.core.retry.RetryPolicy;
import software.amazon.awssdk.enhanced.dynamodb.DynamoDbEnhancedClient;
import software.amazon.awssdk.http.SdkHttpClient;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.autoscaling.AutoScalingClient;
import software.amazon.awssdk.services.autoscaling.AutoScalingClientBuilder;
import software.amazon.awssdk.services.dynamodb.DynamoDbClient;

/** Guice module for binding the AWS lifecycle client functionality. */
@Singleton
public final class AwsLifecycleModule extends LifecycleModule {

  /**
   * Gets the AWS implementation of {@code LifecycleClient}.
   *
   * @return AWSLifecycleClient
   */
  @Override
  public Class<? extends LifecycleClient> getLifecycleClientImpl() {
    return AwsLifecycleClient.class;
  }

  /** Provider for a singleton instance of the {@code AutoScalingClient} class. */
  @Provides
  @Singleton
  AutoScalingClient provideAutoScalingClient(
      AwsCredentialsProvider credentials,
      SdkHttpClient httpClient,
      RetryPolicy retryPolicy,
      @AutoScalingEndpointOverrideBinding URI endpointOverride,
      // TODO(b/202524864): rename annotation to adtech region
      @ApplicationRegionBinding String region) {
    AutoScalingClientBuilder clientBuilder =
        AutoScalingClient.builder()
            .credentialsProvider(credentials)
            .httpClient(httpClient)
            .overrideConfiguration(
                ClientOverrideConfiguration.builder().retryPolicy(retryPolicy).build())
            .region(Region.of(region));

    if (!endpointOverride.toString().isEmpty()) {
      clientBuilder.endpointOverride(endpointOverride);
    }

    return clientBuilder.build();
  }

  @Provides
  @Singleton
  @AsgInstancesDbDynamoClient
  DynamoDbEnhancedClient provideAsgInstancesDynamoClient(
      SdkHttpClient httpClient, AwsCredentialsProvider credentialsProvider) {
    DynamoDbClient ddbClient =
        DynamoDbClient.builder()
            .credentialsProvider(credentialsProvider)
            .httpClient(httpClient)
            .build();
    return DynamoDbEnhancedClient.builder().dynamoDbClient(ddbClient).build();
  }

  @Provides
  @Singleton
  @AsgInstancesDbDynamoTableName
  String provideAsgInstancesTableName(ParameterClient parameterClient)
      throws ParameterClientException {
    return parameterClient.getParameter(WorkerParameter.ASG_INSTANCES_TABLE_NAME.name()).orElse("");
  }

  /** Set to arbitrary number since Lifecycle Client should not be inserting to the DB. */
  @Provides
  @AsgInstancesDbDynamoTtlDays
  Integer provideAsgInstancesTtlDays() {
    return 3;
  }

  /** Annotation for binding an autoscaling endpoint override. */
  @BindingAnnotation
  @Target({ElementType.FIELD, ElementType.PARAMETER, ElementType.METHOD})
  @Retention(RetentionPolicy.RUNTIME)
  public @interface AutoScalingEndpointOverrideBinding {}
}
