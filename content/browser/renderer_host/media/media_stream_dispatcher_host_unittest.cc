// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <queue>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "content/browser/browser_thread_impl.h"
#include "content/browser/renderer_host/media/media_stream_dispatcher_host.h"
#include "content/browser/renderer_host/media/media_stream_manager.h"
#include "content/browser/renderer_host/media/media_stream_ui_proxy.h"
#include "content/common/media/media_stream_messages.h"
#include "content/common/media/media_stream_options.h"
#include "content/public/test/mock_resource_context.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/test/test_content_browser_client.h"
#include "content/test/test_content_client.h"
#include "ipc/ipc_message_macros.h"
#include "media/audio/audio_manager.h"
#include "media/video/capture/fake_video_capture_device.h"
#include "net/url_request/url_request_context.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::DeleteArg;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

const int kProcessId = 5;
const int kRenderId = 6;
const int kPageRequestId = 7;

namespace content {

class MockMediaStreamDispatcherHost : public MediaStreamDispatcherHost,
                                      public TestContentBrowserClient {
 public:
  MockMediaStreamDispatcherHost(
      const scoped_refptr<base::MessageLoopProxy>& message_loop,
      MediaStreamManager* manager)
      : MediaStreamDispatcherHost(kProcessId, manager),
        message_loop_(message_loop) {}

  // A list of mock methods.
  MOCK_METHOD4(OnStreamGenerated,
               void(int routing_id, int request_id, int audio_array_size,
                    int video_array_size));
  MOCK_METHOD2(OnStreamGenerationFailed, void(int routing_id, int request_id));
  MOCK_METHOD1(OnStopGeneratedStreamFromBrowser,
               void(int routing_id));
  MOCK_METHOD2(OnDeviceOpened,
               void(int routing_id, int request_id));

  // Accessor to private functions.
  void OnGenerateStream(int render_view_id,
                        int page_request_id,
                        const StreamOptions& components,
                        const base::Closure& quit_closure) {
    quit_closures_.push(quit_closure);
    MediaStreamDispatcherHost::OnGenerateStream(
        render_view_id, page_request_id, components, GURL());
  }

  void OnStopStreamDevice(int render_view_id,
                          const std::string& device_id) {
    MediaStreamDispatcherHost::OnStopStreamDevice(render_view_id, device_id);
  }

  void OnOpenDevice(int render_view_id,
                    int page_request_id,
                    const std::string& device_id,
                    MediaStreamType type,
                    const base::Closure& quit_closure) {
    quit_closures_.push(quit_closure);
    MediaStreamDispatcherHost::OnOpenDevice(
        render_view_id, page_request_id, device_id, type, GURL());
  }

  bool FindExistingRequestedDeviceInfo(const std::string& device_id,
                                       MediaStreamRequestType request_type,
                                       StreamDeviceInfo* device_info) {
    MediaRequestState request_state;
    return media_stream_manager_->FindExistingRequestedDeviceInfo(
        kProcessId, kRenderId, request_type, device_id, device_info,
        &request_state);
  }

  std::string label_;
  StreamDeviceInfoArray audio_devices_;
  StreamDeviceInfoArray video_devices_;
  StreamDeviceInfo opened_device_;

 private:
  virtual ~MockMediaStreamDispatcherHost() {}

  // This method is used to dispatch IPC messages to the renderer. We intercept
  // these messages here and dispatch to our mock methods to verify the
  // conversation between this object and the renderer.
  virtual bool Send(IPC::Message* message) OVERRIDE {
    CHECK(message);

    // In this method we dispatch the messages to the according handlers as if
    // we are the renderer.
    bool handled = true;
    IPC_BEGIN_MESSAGE_MAP(MockMediaStreamDispatcherHost, *message)
      IPC_MESSAGE_HANDLER(MediaStreamMsg_StreamGenerated, OnStreamGenerated)
      IPC_MESSAGE_HANDLER(MediaStreamMsg_StreamGenerationFailed,
                          OnStreamGenerationFailed)
      IPC_MESSAGE_HANDLER(MediaStreamMsg_StopGeneratedStream,
                          OnStopGeneratedStreamFromBrowser)
      IPC_MESSAGE_HANDLER(MediaStreamMsg_DeviceOpened, OnDeviceOpened)
      IPC_MESSAGE_UNHANDLED(handled = false)
    IPC_END_MESSAGE_MAP()
    EXPECT_TRUE(handled);

    delete message;
    return true;
  }

  // These handler methods do minimal things and delegate to the mock methods.
  void OnStreamGenerated(
      const IPC::Message& msg,
      int request_id,
      std::string label,
      StreamDeviceInfoArray audio_device_list,
      StreamDeviceInfoArray video_device_list) {
    OnStreamGenerated(msg.routing_id(), request_id, audio_device_list.size(),
        video_device_list.size());
    // Notify that the event have occurred.
    base::Closure quit_closure = quit_closures_.front();
    quit_closures_.pop();
    message_loop_->PostTask(FROM_HERE, base::ResetAndReturn(&quit_closure));

    label_ = label;
    audio_devices_ = audio_device_list;
    video_devices_ = video_device_list;
  }

  void OnStreamGenerationFailed(const IPC::Message& msg, int request_id) {
    OnStreamGenerationFailed(msg.routing_id(), request_id);
    if (!quit_closures_.empty()) {
      base::Closure quit_closure = quit_closures_.front();
      quit_closures_.pop();
      message_loop_->PostTask(FROM_HERE, base::ResetAndReturn(&quit_closure));
    }

    label_= "";
  }

  void OnStopGeneratedStreamFromBrowser(const IPC::Message& msg,
                                        const std::string& label) {
    OnStopGeneratedStreamFromBrowser(msg.routing_id());
    // Notify that the event have occurred.
    if (!quit_closures_.empty()) {
      base::Closure quit_closure = quit_closures_.front();
      quit_closures_.pop();
      message_loop_->PostTask(FROM_HERE, base::ResetAndReturn(&quit_closure));
    }
    label_ = "";
  }

  void OnDeviceOpened(const IPC::Message& msg,
                      int request_id,
                      const std::string& label,
                      const StreamDeviceInfo& device) {
    base::Closure quit_closure = quit_closures_.front();
    quit_closures_.pop();
    message_loop_->PostTask(FROM_HERE, base::ResetAndReturn(&quit_closure));
    label_ = label;
    opened_device_ = device;
  }

  scoped_refptr<base::MessageLoopProxy> message_loop_;

  std::queue<base::Closure> quit_closures_;
};

class MockMediaStreamUIProxy : public FakeMediaStreamUIProxy {
 public:
  MOCK_METHOD1(OnStarted, void(const base::Closure& stop));
};

class MediaStreamDispatcherHostTest : public testing::Test {
 public:
  MediaStreamDispatcherHostTest()
      : old_browser_client_(NULL),
        thread_bundle_(content::TestBrowserThreadBundle::IO_MAINLOOP) {
    // Create our own MediaStreamManager.
    audio_manager_.reset(media::AudioManager::Create());
    media_stream_manager_.reset(new MediaStreamManager(audio_manager_.get()));
    // Make sure we use fake devices to avoid long delays.
    media_stream_manager_->UseFakeDevice();

    host_ = new MockMediaStreamDispatcherHost(base::MessageLoopProxy::current(),
                                              media_stream_manager_.get());

    // Use the fake content client and browser.
    content_client_.reset(new TestContentClient());
    SetContentClient(content_client_.get());
    old_browser_client_ = SetBrowserClientForTesting(host_.get());
  }

  virtual ~MediaStreamDispatcherHostTest() {
    // Recover the old browser client and content client.
    SetBrowserClientForTesting(old_browser_client_);
    content_client_.reset();
    media_stream_manager_->WillDestroyCurrentMessageLoop();
  }

  virtual void TearDown() OVERRIDE {
    host_->OnChannelClosing();
  }

 protected:
  virtual void SetupFakeUI(bool expect_started) {
    scoped_ptr<MockMediaStreamUIProxy> stream_ui(new MockMediaStreamUIProxy());
    if (expect_started) {
      EXPECT_CALL(*stream_ui, OnStarted(_));
    }
    media_stream_manager_->UseFakeUI(
        stream_ui.PassAs<FakeMediaStreamUIProxy>());
  }

  void GenerateStreamAndWaitForResult(int render_view_id,
                                      int page_request_id,
                                      const StreamOptions& options) {
    base::RunLoop run_loop;
    host_->OnGenerateStream(render_view_id, page_request_id, options,
                            run_loop.QuitClosure());
    run_loop.Run();
  }

  void OpenVideoDeviceAndWaitForResult(int render_view_id,
                                       int page_request_id,
                                       const std::string& device_id) {
    base::RunLoop run_loop;
    host_->OnOpenDevice(render_view_id, page_request_id, device_id,
                        MEDIA_DEVICE_VIDEO_CAPTURE,
                        run_loop.QuitClosure());
    run_loop.Run();
  }

  scoped_refptr<MockMediaStreamDispatcherHost> host_;
  scoped_ptr<media::AudioManager> audio_manager_;
  scoped_ptr<MediaStreamManager> media_stream_manager_;
  ContentBrowserClient* old_browser_client_;
  scoped_ptr<ContentClient> content_client_;
  content::TestBrowserThreadBundle thread_bundle_;
};

TEST_F(MediaStreamDispatcherHostTest, GenerateStreamWithVideoOnly) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
}

// This test generates two streams with video only using the same render view
// id. The same capture device  with the same device and session id is expected
// to be used.
TEST_F(MediaStreamDispatcherHostTest, GenerateStreamsFromSameRenderId) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  // Generate first stream.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  // Check the latest generated stream.
  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
  const std::string label1 = host_->label_;
  const std::string device_id1 = host_->video_devices_.front().device.id;
  const int session_id1 = host_->video_devices_.front().session_id;

  // Generate second stream.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(),
              OnStreamGenerated(kRenderId, kPageRequestId + 1, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId + 1, options);

  // Check the latest generated stream.
  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
  const std::string label2 = host_->label_;
  const std::string device_id2 = host_->video_devices_.front().device.id;
  int session_id2 = host_->video_devices_.front().session_id;
  EXPECT_EQ(device_id1, device_id2);
  EXPECT_EQ(session_id1, session_id2);
  EXPECT_NE(label1, label2);
}

TEST_F(MediaStreamDispatcherHostTest,
       GenerateStreamAndOpenDeviceFromSameRenderId) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  // Generate first stream.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
  const std::string label1 = host_->label_;
  const std::string device_id1 = host_->video_devices_.front().device.id;
  const int session_id1 = host_->video_devices_.front().session_id;

  // Generate second stream.
  OpenVideoDeviceAndWaitForResult(kRenderId, kPageRequestId, device_id1);

  const std::string device_id2 = host_->opened_device_.device.id;
  const int session_id2 = host_->opened_device_.session_id;
  const std::string label2 = host_->label_;

  EXPECT_EQ(device_id1, device_id2);
  EXPECT_NE(session_id1, session_id2);
  EXPECT_NE(label1, label2);
}


// This test generates two streams with video only using two separate render
// view ids. The same device id but different session ids are expected.
TEST_F(MediaStreamDispatcherHostTest, GenerateStreamsDifferentRenderId) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  // Generate first stream.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  // Check the latest generated stream.
  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
  const std::string label1 = host_->label_;
  const std::string device_id1 = host_->video_devices_.front().device.id;
  const int session_id1 = host_->video_devices_.front().session_id;

  // Generate second stream from another render view.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(),
              OnStreamGenerated(kRenderId+1, kPageRequestId + 1, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId+1, kPageRequestId + 1, options);

  // Check the latest generated stream.
  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);
  const std::string label2 = host_->label_;
  const std::string device_id2 = host_->video_devices_.front().device.id;
  const int session_id2 = host_->video_devices_.front().session_id;
  EXPECT_EQ(device_id1, device_id2);
  EXPECT_NE(session_id1, session_id2);
  EXPECT_NE(label1, label2);
}

// This test request two streams with video only without waiting for the first
// stream to be generated before requesting the second.
// The same device id and session ids are expected.
TEST_F(MediaStreamDispatcherHostTest, GenerateStreamsWithoutWaiting) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  // Generate first stream.
  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));

  // Generate second stream.
  EXPECT_CALL(*host_.get(),
              OnStreamGenerated(kRenderId, kPageRequestId + 1, 0, 1));

  base::RunLoop run_loop1;
  base::RunLoop run_loop2;
  host_->OnGenerateStream(kRenderId, kPageRequestId, options,
                          run_loop1.QuitClosure());
  host_->OnGenerateStream(kRenderId, kPageRequestId + 1, options,
                          run_loop2.QuitClosure());

  run_loop1.Run();
  run_loop2.Run();
}

TEST_F(MediaStreamDispatcherHostTest, StopDeviceInStream) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  SetupFakeUI(true);
  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  const std::string device_id = host_->video_devices_.front().device.id;
  const int session_id =  host_->video_devices_.front().session_id;
  StreamDeviceInfo video_device_info;
  EXPECT_TRUE(host_->FindExistingRequestedDeviceInfo(device_id,
                                                     MEDIA_GENERATE_STREAM,
                                                     &video_device_info));
  EXPECT_EQ(video_device_info.device.id, device_id);
  EXPECT_EQ(video_device_info.session_id, session_id);

  OpenVideoDeviceAndWaitForResult(kRenderId, kPageRequestId, device_id);

  host_->OnStopStreamDevice(kRenderId, device_id);

  EXPECT_FALSE(host_->FindExistingRequestedDeviceInfo(device_id,
                                                      MEDIA_GENERATE_STREAM,
                                                      &video_device_info));
  EXPECT_TRUE(host_->FindExistingRequestedDeviceInfo(device_id,
                                                     MEDIA_OPEN_DEVICE,
                                                     &video_device_info));
}

TEST_F(MediaStreamDispatcherHostTest, CancelPendingStreamsOnChannelClosing) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  base::RunLoop run_loop;

  // Create multiple GenerateStream requests.
  size_t streams = 5;
  for (size_t i = 1; i <= streams; ++i) {
    host_->OnGenerateStream(kRenderId, kPageRequestId + i, options,
                            run_loop.QuitClosure());
  }

  // Calling OnChannelClosing() to cancel all the pending requests.
  host_->OnChannelClosing();
  run_loop.RunUntilIdle();
}

TEST_F(MediaStreamDispatcherHostTest, StopGeneratedStreamsOnChannelClosing) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  // Create first group of streams.
  size_t generated_streams = 3;
  for (size_t i = 0; i < generated_streams; ++i) {
    SetupFakeUI(true);
    EXPECT_CALL(*host_.get(),
                OnStreamGenerated(kRenderId, kPageRequestId + i, 0, 1));
    GenerateStreamAndWaitForResult(kRenderId, kPageRequestId + i, options);
  }

  // Calling OnChannelClosing() to cancel all the pending/generated streams.
  host_->OnChannelClosing();
  base::RunLoop().RunUntilIdle();
}

TEST_F(MediaStreamDispatcherHostTest, CloseFromUI) {
  StreamOptions options(MEDIA_NO_SERVICE, MEDIA_DEVICE_VIDEO_CAPTURE);

  base::Closure close_callback;
  scoped_ptr<MockMediaStreamUIProxy> stream_ui(new MockMediaStreamUIProxy());
  EXPECT_CALL(*stream_ui, OnStarted(_))
      .WillOnce(SaveArg<0>(&close_callback));
  media_stream_manager_->UseFakeUI(stream_ui.PassAs<FakeMediaStreamUIProxy>());

  EXPECT_CALL(*host_.get(), OnStreamGenerated(kRenderId, kPageRequestId, 0, 1));
  EXPECT_CALL(*host_.get(), OnStopGeneratedStreamFromBrowser(kRenderId));
  GenerateStreamAndWaitForResult(kRenderId, kPageRequestId, options);

  EXPECT_EQ(host_->audio_devices_.size(), 0u);
  EXPECT_EQ(host_->video_devices_.size(), 1u);

  ASSERT_FALSE(close_callback.is_null());
  close_callback.Run();
  base::RunLoop().RunUntilIdle();
}

};  // namespace content
