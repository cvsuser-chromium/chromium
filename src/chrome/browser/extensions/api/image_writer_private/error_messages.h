// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_IMAGE_WRITER_PRIVATE_ERROR_MESSAGES_H_
#define CHROME_BROWSER_EXTENSIONS_API_IMAGE_WRITER_PRIVATE_ERROR_MESSAGES_H_

namespace extensions {
namespace image_writer {
namespace error {

extern const char kAborted[];
extern const char kCloseDevice[];
extern const char kCloseImage[];
extern const char kDeviceList[];
extern const char kDeviceMD5[];
extern const char kDownloadCancelled[];
extern const char kDownloadHash[];
extern const char kDownloadInterrupted[];
extern const char kDownloadMD5[];
extern const char kEmptyUnzip[];
extern const char kFileOperationsNotImplemented[];
extern const char kImageBurnerError[];
extern const char kImageMD5[];
extern const char kImageNotFound[];
extern const char kImageSize[];
extern const char kInvalidUrl[];
extern const char kMultiFileZip[];
extern const char kNoOperationInProgress[];
extern const char kOpenDevice[];
extern const char kOpenImage[];
extern const char kOperationAlreadyInProgress[];
extern const char kPrematureEndOfFile[];
extern const char kReadImage[];
extern const char kTempDir[];
extern const char kTempFile[];
extern const char kUnsupportedOperation[];
extern const char kUnzip[];
extern const char kWriteHash[];
extern const char kWriteImage[];

} // namespace error
} // namespace image_writer
} // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_IMAGE_WRITER_PRIVATE_ERROR_MESSAGES_H_
