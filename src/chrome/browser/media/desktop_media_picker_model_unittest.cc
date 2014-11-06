// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/desktop_media_picker_model.h"

#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "content/public/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_frame.h"
#include "third_party/webrtc/modules/desktop_capture/screen_capturer.h"
#include "third_party/webrtc/modules/desktop_capture/window_capturer.h"

using testing::_;
using testing::DoAll;

namespace {

class MockObserver : public DesktopMediaPickerModel::Observer {
 public:
  MOCK_METHOD1(OnSourceAdded, void(int index));
  MOCK_METHOD1(OnSourceRemoved, void(int index));
  MOCK_METHOD1(OnSourceNameChanged, void(int index));
  MOCK_METHOD1(OnSourceThumbnailChanged, void(int index));
};

class FakeScreenCapturer : public webrtc::ScreenCapturer {
 public:
  FakeScreenCapturer() {}
  virtual ~FakeScreenCapturer() {}

  // webrtc::ScreenCapturer implementation.
  virtual void Start(Callback* callback) OVERRIDE {
    callback_ = callback;
  }

  virtual void Capture(const webrtc::DesktopRegion& region) OVERRIDE {
    DCHECK(callback_);
    webrtc::DesktopFrame* frame =
        new webrtc::BasicDesktopFrame(webrtc::DesktopSize(10, 10));
    memset(frame->data(), 0, frame->stride() * frame->size().height());
    callback_->OnCaptureCompleted(frame);
  }

  virtual void SetMouseShapeObserver(
      MouseShapeObserver* mouse_shape_observer) OVERRIDE {
    NOTIMPLEMENTED();
  }

 protected:
  Callback* callback_;

  DISALLOW_COPY_AND_ASSIGN(FakeScreenCapturer);
};

class FakeWindowCapturer : public webrtc::WindowCapturer {
 public:
  FakeWindowCapturer()
      : callback_(NULL) {
  }
  virtual ~FakeWindowCapturer() {}

  void SetWindowList(const WindowList& list) {
    base::AutoLock lock(window_list_lock_);
    window_list_ = list;
  }

  // Sets |value| thats going to be used to memset() content of the frames
  // generated for |window_id|. By default generated frames are set to zeros.
  void SetNextFrameValue(WindowId window_id, int8_t value) {
    base::AutoLock lock(frame_values_lock_);
    frame_values_[window_id] = value;
  }

  // webrtc::WindowCapturer implementation.
  virtual void Start(Callback* callback) OVERRIDE {
    callback_ = callback;
  }

  virtual void Capture(const webrtc::DesktopRegion& region) OVERRIDE {
    DCHECK(callback_);

    base::AutoLock lock(frame_values_lock_);

    std::map<WindowId, int8_t>::iterator it =
        frame_values_.find(selected_window_id_);
    int8_t value = (it != frame_values_.end()) ? it->second : 0;
    webrtc::DesktopFrame* frame =
        new webrtc::BasicDesktopFrame(webrtc::DesktopSize(10, 10));
    memset(frame->data(), value, frame->stride() * frame->size().height());
    callback_->OnCaptureCompleted(frame);
  }

  virtual bool GetWindowList(WindowList* windows) OVERRIDE {
    base::AutoLock lock(window_list_lock_);
    *windows = window_list_;
    return true;
  }

  virtual bool SelectWindow(WindowId id) OVERRIDE {
    selected_window_id_ = id;
    return true;
  }

 private:
  Callback* callback_;
  WindowList window_list_;
  base::Lock window_list_lock_;

  WindowId selected_window_id_;

  // Frames to be captured per window.
  std::map<WindowId, int8_t> frame_values_;
  base::Lock frame_values_lock_;

  DISALLOW_COPY_AND_ASSIGN(FakeWindowCapturer);
};

class DesktopMediaPickerModelTest : public testing::Test {
 public:
  DesktopMediaPickerModelTest()
      : window_capturer_(NULL),
        ui_thread_(content::BrowserThread::UI,
                   &message_loop_) {
  }

  void CreateWithDefaultCapturers() {
    window_capturer_ = new FakeWindowCapturer();
    model_.reset(new DesktopMediaPickerModelImpl(
        scoped_ptr<webrtc::ScreenCapturer>(new FakeScreenCapturer()),
        scoped_ptr<webrtc::WindowCapturer>(window_capturer_)));

    // Set update period to reduce the time it takes to run tests.
    model_->SetUpdatePeriod(base::TimeDelta::FromMilliseconds(0));
  }

 protected:
  // Must be listed before |model_|, so it's destroyed last.
  MockObserver observer_;

  // Owned by |model_|;
  FakeWindowCapturer* window_capturer_;

  scoped_ptr<DesktopMediaPickerModelImpl> model_;

  base::MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;

  DISALLOW_COPY_AND_ASSIGN(DesktopMediaPickerModelTest);
};

ACTION_P2(CheckListSize, model, expected_list_size) {
  EXPECT_EQ(expected_list_size, model->source_count());
}

ACTION_P(QuitMessageLoop, message_loop) {
  message_loop->PostTask(FROM_HERE, base::MessageLoop::QuitClosure());
}

TEST_F(DesktopMediaPickerModelTest, InitialSourceList) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 0;
  window.title = "Test window";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  EXPECT_EQ(model_->source(0).id.type, content::DesktopMediaID::TYPE_SCREEN);
  EXPECT_EQ(model_->source(0).id.id, 0);
  EXPECT_EQ(model_->source(1).id.type, content::DesktopMediaID::TYPE_WINDOW);
  EXPECT_EQ(model_->source(1).id.id, 0);
  EXPECT_EQ(model_->source(1).name, UTF8ToUTF16(window.title));
}

// Verifies that the window specified with SetViewDialogWindowId() is filtered
// from the results.
TEST_F(DesktopMediaPickerModelTest, Filtering) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;

  window.id = 0;
  window.title = "Test window";
  list.push_back(window);

  window.id = 1;
  list.push_back(window);

  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }

  model_->SetViewDialogWindowId(0);

  model_->StartUpdating(&observer_);
  message_loop_.Run();

  EXPECT_EQ(model_->source(0).id.type, content::DesktopMediaID::TYPE_SCREEN);
  EXPECT_EQ(model_->source(0).id.id, 0);
  EXPECT_EQ(model_->source(1).id.type, content::DesktopMediaID::TYPE_WINDOW);
  EXPECT_EQ(model_->source(1).id.id, 1);
  EXPECT_EQ(model_->source(1).name, UTF8ToUTF16(window.title));
}

TEST_F(DesktopMediaPickerModelTest, WindowsOnly) {
  window_capturer_ = new FakeWindowCapturer();
  model_.reset(new DesktopMediaPickerModelImpl(
      scoped_ptr<webrtc::ScreenCapturer>(),
      scoped_ptr<webrtc::WindowCapturer>(window_capturer_)));

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 0;
  window.title = "Test window";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  EXPECT_EQ(model_->source(0).id.type, content::DesktopMediaID::TYPE_WINDOW);
}

TEST_F(DesktopMediaPickerModelTest, ScreenOnly) {
  model_.reset(new DesktopMediaPickerModelImpl(
      scoped_ptr<webrtc::ScreenCapturer>(new FakeScreenCapturer),
      scoped_ptr<webrtc::WindowCapturer>()));

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  EXPECT_EQ(model_->source(0).id.type, content::DesktopMediaID::TYPE_SCREEN);
}

TEST_F(DesktopMediaPickerModelTest, AddWindow) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 1;
  window.title = "Test window 1";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  testing::Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, OnSourceAdded(1))
    .WillOnce(DoAll(CheckListSize(model_.get(), 3),
                    QuitMessageLoop(&message_loop_)));

  window.id = 0;
  window.title = "Test window 0";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  message_loop_.Run();

  EXPECT_EQ(model_->source(1).id.type, content::DesktopMediaID::TYPE_WINDOW);
  EXPECT_EQ(model_->source(1).id.id, 0);
}

TEST_F(DesktopMediaPickerModelTest, RemoveWindow) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 0;
  window.title = "Test window 0";
  list.push_back(window);
  window.id = 1;
  window.title = "Test window 1";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceAdded(2))
      .WillOnce(CheckListSize(model_.get(), 3));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(2))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  testing::Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, OnSourceRemoved(1))
    .WillOnce(DoAll(CheckListSize(model_.get(), 2),
                    QuitMessageLoop(&message_loop_)));

  list.erase(list.begin());
  window_capturer_->SetWindowList(list);

  message_loop_.Run();
}

TEST_F(DesktopMediaPickerModelTest, UpdateTitle) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 0;
  window.title = "Test window";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  testing::Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, OnSourceNameChanged(1))
    .WillOnce(QuitMessageLoop(&message_loop_));

  const std::string kTestTitle = "New Title";

  list[0].title = kTestTitle;
  window_capturer_->SetWindowList(list);

  message_loop_.Run();

  EXPECT_EQ(model_->source(1).name, base::UTF8ToUTF16(kTestTitle));
}

TEST_F(DesktopMediaPickerModelTest, UpdateThumbnail) {
  CreateWithDefaultCapturers();

  webrtc::WindowCapturer::WindowList list;
  webrtc::WindowCapturer::Window window;
  window.id = 0;
  window.title = "Test window 1";
  list.push_back(window);
  window.id = 1;
  window.title = "Test window 2";
  list.push_back(window);
  window_capturer_->SetWindowList(list);

  {
    testing::InSequence dummy;
    EXPECT_CALL(observer_, OnSourceAdded(0))
      .WillOnce(CheckListSize(model_.get(), 1));
    EXPECT_CALL(observer_, OnSourceAdded(1))
      .WillOnce(CheckListSize(model_.get(), 2));
    EXPECT_CALL(observer_, OnSourceAdded(2))
      .WillOnce(CheckListSize(model_.get(), 3));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(0));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(1));
    EXPECT_CALL(observer_, OnSourceThumbnailChanged(2))
      .WillOnce(QuitMessageLoop(&message_loop_));
  }
  model_->StartUpdating(&observer_);

  message_loop_.Run();

  testing::Mock::VerifyAndClearExpectations(&observer_);

  EXPECT_CALL(observer_, OnSourceThumbnailChanged(1))
    .WillOnce(QuitMessageLoop(&message_loop_));

  // Update frame for the window and verify that we get notification about it.
  window_capturer_->SetNextFrameValue(0, 1);

  message_loop_.Run();
}

}  // namespace
