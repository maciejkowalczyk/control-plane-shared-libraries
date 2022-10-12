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

package com.google.scp.operator.cpio.blobstorageclient;

import com.google.common.collect.ImmutableList;
import com.google.scp.operator.cpio.blobstorageclient.model.DataLocation;
import com.google.scp.operator.cpio.blobstorageclient.model.DataLocation.BlobStoreDataLocation;
import java.io.InputStream;
import java.nio.file.Path;

/** Interface for blob storage services */
public interface BlobStorageClient {

  /**
   * Blocking call to retrieve a blob object from the storage provider.
   *
   * @param location The data location of the blob to retrieve.
   * @return An {@code InputStream} with the blob contents.
   * @throws BlobStorageClientException
   */
  InputStream getBlob(DataLocation location) throws BlobStorageClientException;

  /**
   * Blocking call to upload a blob to the storage provider.
   *
   * @param location The data location of the blob to be uploaded.
   * @param filePath the file path for the blob to be uploaded.
   * @throws BlobStorageClientException
   */
  void putBlob(DataLocation location, Path filePath) throws BlobStorageClientException;

  /**
   * Blocking call to delete a blob from the storage provider.
   *
   * @param location The data location of the blob to be deleted.
   * @throws BlobStorageClientException
   */
  void deleteBlob(DataLocation location) throws BlobStorageClientException;

  /**
   * Blocking call to list the blobs in a bucket on the storage provider, which handles pagination.
   *
   * @param location The data location of the blobs to list.
   * @throws BlobStorageClientException
   * @return An {@code ImmutableList<String>} of keys that correspond to blobs in the bucket.
   */
  ImmutableList<String> listBlobs(DataLocation location) throws BlobStorageClientException;

  /** Returns a data location of the bucket/path. */
  static DataLocation getDataLocation(String bucket, String prefix) {
    return DataLocation.ofBlobStoreDataLocation(BlobStoreDataLocation.create(bucket, prefix));
  }

  /** Represents an exception thrown by the {@code BlobStorageClient} class. */
  class BlobStorageClientException extends Exception {
    /** Creates a new instance from a {@code Throwable}. */
    public BlobStorageClientException(Throwable cause) {
      super(cause);
    }
  }
}
