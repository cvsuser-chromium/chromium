// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system/create_file_operation.h"

#include "chrome/browser/chromeos/drive/file_system/operation_test_base.h"
#include "chrome/browser/google_apis/test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace file_system {

typedef OperationTestBase CreateFileOperationTest;

TEST_F(CreateFileOperationTest, CreateFile) {
  CreateFileOperation operation(blocking_task_runner(),
                                observer(),
                                scheduler(),
                                metadata(),
                                cache());

  const base::FilePath kExistingFile(
      FILE_PATH_LITERAL("drive/root/File 1.txt"));
  const base::FilePath kExistingDirectory(
      FILE_PATH_LITERAL("drive/root/Directory 1"));
  const base::FilePath kNonExistingFile(
      FILE_PATH_LITERAL("drive/root/Directory 1/not exist.png"));
  const base::FilePath kFileInNonExistingDirectory(
      FILE_PATH_LITERAL("drive/root/not exist/not exist.png"));

  // Create fails if is_exclusive = true and a file exists.
  FileError error = FILE_ERROR_FAILED;
  operation.CreateFile(
      kExistingFile,
      true,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_EXISTS, error);

  // Create succeeds if is_exclusive = false and a file exists.
  operation.CreateFile(
      kExistingFile,
      false,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Create fails if a directory existed even when is_exclusive = false.
  operation.CreateFile(
      kExistingDirectory,
      false,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_EXISTS, error);

  // Create succeeds if no entry exists.
  operation.CreateFile(
      kNonExistingFile,
      true,   // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Create fails if the parent directory does not exist.
  operation.CreateFile(
      kFileInNonExistingDirectory,
      false,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_NOT_A_DIRECTORY, error);
}

TEST_F(CreateFileOperationTest, CreateFileMimeType) {
  CreateFileOperation operation(blocking_task_runner(),
                                observer(),
                                scheduler(),
                                metadata(),
                                cache());

  const base::FilePath kPng1(FILE_PATH_LITERAL("drive/root/1.png"));
  const base::FilePath kPng2(FILE_PATH_LITERAL("drive/root/2.png"));
  const base::FilePath kUnknown(FILE_PATH_LITERAL("drive/root/3.unknown"));
  const std::string kSpecialMimeType("application/x-createfile-test");

  FileError error = FILE_ERROR_FAILED;
  operation.CreateFile(
      kPng1,
      false,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // If no mime type is specified, it is guessed from the file name.
  ResourceEntry entry;
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(kPng1, &entry));
  EXPECT_EQ("image/png", entry.file_specific_info().content_mime_type());

  error = FILE_ERROR_FAILED;
  operation.CreateFile(
      kPng2,
      false,  // is_exclusive
      kSpecialMimeType,
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // If the mime type is explicitly set, respect it.
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(kPng2, &entry));
  EXPECT_EQ(kSpecialMimeType, entry.file_specific_info().content_mime_type());

  error = FILE_ERROR_FAILED;
  operation.CreateFile(
      kUnknown,
      false,  // is_exclusive
      std::string(),  // no predetermined mime type
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // If the mime type is not set and unknown, default to octet-stream.
  ASSERT_EQ(FILE_ERROR_OK, GetLocalResourceEntry(kUnknown, &entry));
  EXPECT_EQ("application/octet-stream",
            entry.file_specific_info().content_mime_type());
}


}  // namespace file_system
}  // namespace drive
