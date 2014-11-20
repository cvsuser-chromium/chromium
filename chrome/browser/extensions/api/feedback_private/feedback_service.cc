// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/feedback_private/feedback_service.h"

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace extensions {

// static
void FeedbackService::PopulateSystemInfo(
    SystemInformationList* sys_info_list,
    const std::string& key,
    const std::string& value) {
  base::DictionaryValue sys_info_value;
  sys_info_value.Set("key", new base::StringValue(key));
  sys_info_value.Set("value", new base::StringValue(value));

  linked_ptr<SystemInformation> sys_info(new SystemInformation());
  SystemInformation::Populate(sys_info_value, sys_info.get());

  sys_info_list->push_back(sys_info);
}

FeedbackService::FeedbackService() {
}

FeedbackService::~FeedbackService() {
}

void FeedbackService::SendFeedback(
    Profile* profile,
    scoped_refptr<FeedbackData> feedback_data,
    const SendFeedbackCallback& callback) {
  send_feedback_callback_ = callback;
  feedback_data_ = feedback_data;

  if (!feedback_data_->attached_file_uuid().empty()) {
    // Self-deleting object.
    BlobReader* attached_file_reader = new BlobReader(
        profile, feedback_data_->attached_file_uuid(),
        base::Bind(&FeedbackService::AttachedFileCallback,
                   GetWeakPtr()));
    attached_file_reader->Start();
  }

  if (!feedback_data_->screenshot_uuid().empty()) {
    // Self-deleting object.
    BlobReader* screenshot_reader = new BlobReader(
        profile, feedback_data_->screenshot_uuid(),
        base::Bind(&FeedbackService::ScreenshotCallback,
                   GetWeakPtr()));
    screenshot_reader->Start();
  }

  CompleteSendFeedback();
}

void FeedbackService::AttachedFileCallback(scoped_ptr<std::string> data) {
  if (!data.get())
    feedback_data_->set_attached_file_uuid(std::string());
  else
    feedback_data_->AttachAndCompressFileData(data.Pass());

  CompleteSendFeedback();
}

void FeedbackService::ScreenshotCallback(scoped_ptr<std::string> data) {
  if (!data.get())
    feedback_data_->set_screenshot_uuid(std::string());
  else
    feedback_data_->set_image(data.Pass());

  CompleteSendFeedback();
}

void FeedbackService::CompleteSendFeedback() {
  // A particular data collection is considered completed if,
  // a.) The blob URL is invalid - this will either happen because we never had
  //     a URL and never needed to read this data, or that the data read failed
  //     and we set it to invalid in the data read callback.
  // b.) The associated data object exists, meaning that the data has been read
  //     and the read callback has updated the associated data on the feedback
  //     object.
  bool attached_file_completed =
      feedback_data_->attached_file_uuid().empty() ||
      feedback_data_->attached_filedata();
  bool screenshot_completed =
      feedback_data_->screenshot_uuid().empty() ||
      feedback_data_->image();

  if (screenshot_completed && attached_file_completed) {
    // Signal the feedback object that the data from the feedback page has been
    // filled - the object will manage sending of the actual report.
    feedback_data_->OnFeedbackPageDataComplete();
    // TODO(rkc): Change this once we have FeedbackData/Util refactored to
    // report the status of the report being sent.
    send_feedback_callback_.Run(true);
  }
}

}  // namespace extensions
