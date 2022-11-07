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

package com.google.crypto.tink.integration.awskmsv2;

import com.google.crypto.tink.PublicKeySign;
import java.security.GeneralSecurityException;
import software.amazon.awssdk.core.SdkBytes;
import software.amazon.awssdk.core.exception.SdkClientException;
import software.amazon.awssdk.services.kms.KmsClient;
import software.amazon.awssdk.services.kms.model.SignRequest;

/** PublicKeySign implementation based AWS KMS through KmsClient. */
public final class AwsKmsPublicKeySign implements PublicKeySign {

  private final KmsClient kmsClient;
  private final String signatureAlgorithm;
  private final String signatureKeyId;

  public AwsKmsPublicKeySign(
      KmsClient kmsClient, String signatureKeyId, String signatureAlgorithm) {
    this.kmsClient = kmsClient;
    this.signatureKeyId = signatureKeyId;
    this.signatureAlgorithm = signatureAlgorithm;
  }

  @Override
  public byte[] sign(final byte[] data) throws GeneralSecurityException {
    var signRequest =
        SignRequest.builder()
            .keyId(signatureKeyId)
            .message(SdkBytes.fromByteArray(data))
            .signingAlgorithm(signatureAlgorithm)
            .build();
    try {
      return kmsClient.sign(signRequest).signature().asByteArray();
    } catch (SdkClientException e) {
      throw new GeneralSecurityException("Request to AWS KMS failed", e);
    }
  }
}
