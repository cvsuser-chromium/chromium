// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>

#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/media/webrtc_browsertest_base.h"
#include "chrome/browser/media/webrtc_browsertest_common.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/ui/ui_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/perf/perf_test.h"

static const base::FilePath::CharType kReferenceFile[] =
#if defined (OS_WIN)
    FILE_PATH_LITERAL("pyauto_private/webrtc/human-voice-win.wav");
#else
    FILE_PATH_LITERAL("pyauto_private/webrtc/human-voice-linux.wav");
#endif

// The javascript will load the reference file relative to its location,
// which is in /webrtc on the web server. Therefore, prepend a '..' traversal.
static const char kReferenceFileRelativeUrl[] =
#if defined (OS_WIN)
    "../pyauto_private/webrtc/human-voice-win.wav";
#else
    "../pyauto_private/webrtc/human-voice-linux.wav";
#endif

static const char kMainWebrtcTestHtmlPage[] =
    "files/webrtc/webrtc_audio_quality_test.html";

static base::FilePath GetTestDataDir() {
  base::FilePath source_dir;
  PathService::Get(chrome::DIR_TEST_DATA, &source_dir);
  return source_dir;
}

// Test that the typing detection feature works.
// You must have the src-internal solution in your .gclient to put the required
// pyauto_private directory into chrome/test/data/.
class WebrtcTypingDetectionBrowserTest : public WebRtcTestBase {
 public:
  // TODO(phoglund): clean up duplication from audio quality browser test when
  // this test is complete and is proven to work.
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    PeerConnectionServerRunner::KillAllPeerConnectionServersOnCurrentSystem();
  }

  bool HasAllRequiredResources() {
    base::FilePath reference_file =
        GetTestDataDir().Append(kReferenceFile);
    if (!base::PathExists(reference_file)) {
      LOG(ERROR) << "Cannot find the reference file to be used for audio "
                 << "quality comparison: " << reference_file.value();
      return false;
    }
    return true;
  }

  void AddAudioFile(const std::string& input_file_relative_url,
                    content::WebContents* tab_contents) {
    EXPECT_EQ("ok-added", ExecuteJavascript(
        "addAudioFile('" + input_file_relative_url + "')", tab_contents));
  }

  void PlayAudioFile(content::WebContents* tab_contents) {
    EXPECT_EQ("ok-playing", ExecuteJavascript("playAudioFile()", tab_contents));
  }

  void MixLocalStreamWithPreviouslyLoadedAudioFile(
      content::WebContents* tab_contents) {
    EXPECT_EQ("ok-mixed-in", ExecuteJavascript(
        "mixLocalStreamWithPreviouslyLoadedAudioFile()", tab_contents));
  }

  // Ensures we didn't get any errors asynchronously (e.g. while no javascript
  // call from this test was outstanding).
  void AssertNoAsynchronousErrors(content::WebContents* tab_contents) {
    EXPECT_EQ("ok-no-errors",
              ExecuteJavascript("getAnyTestFailures()", tab_contents));
  }

  void EstablishCall(content::WebContents* from_tab,
                     content::WebContents* to_tab) {
    EXPECT_EQ("ok-negotiating",
              ExecuteJavascript("negotiateCall()", from_tab));

    // Ensure the call gets up on both sides.
    EXPECT_TRUE(PollingWaitUntil("getPeerConnectionReadyState()",
                                 "active", from_tab));
    EXPECT_TRUE(PollingWaitUntil("getPeerConnectionReadyState()",
                                 "active", to_tab));
  }

  void HangUp(content::WebContents* from_tab) {
    EXPECT_EQ("ok-call-hung-up", ExecuteJavascript("hangUp()", from_tab));
  }

  void WaitUntilHangupVerified(content::WebContents* tab_contents) {
    EXPECT_TRUE(PollingWaitUntil("getPeerConnectionReadyState()",
                                 "no-peer-connection", tab_contents));
  }

  PeerConnectionServerRunner peerconnection_server_;
};

// TODO(phoglund): enable when fully implemented.
IN_PROC_BROWSER_TEST_F(WebrtcTypingDetectionBrowserTest,
                       DISABLED_MANUAL_TestTypingDetection) {
  // TODO(phoglund): make this use embedded_test_server when that test server
  // can handle files > ~400Kb.
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(peerconnection_server_.Start());

  ui_test_utils::NavigateToURL(
      browser(), test_server()->GetURL(kMainWebrtcTestHtmlPage));
  content::WebContents* left_tab =
      browser()->tab_strip_model()->GetActiveWebContents();

  chrome::AddBlankTabAt(browser(), -1, true);
  content::WebContents* right_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ui_test_utils::NavigateToURL(
      browser(), test_server()->GetURL(kMainWebrtcTestHtmlPage));

  ConnectToPeerConnectionServer("peer 1", left_tab);
  ConnectToPeerConnectionServer("peer 2", right_tab);

  GetUserMediaWithSpecificConstraintsAndAccept(left_tab,
                                               kAudioOnlyCallConstraints);
  EXPECT_EQ("ok-peerconnection-created",
            ExecuteJavascript("preparePeerConnection()", left_tab));

  AddAudioFile(kReferenceFileRelativeUrl, left_tab);
  MixLocalStreamWithPreviouslyLoadedAudioFile(left_tab);

  EstablishCall(left_tab, right_tab);

  // Note: the media flow isn't necessarily established on the connection just
  // because the ready state is ok on both sides. We sleep a bit between call
  // establishment and playing to avoid cutting of the beginning of the audio
  // file.
  SleepInJavascript(left_tab, 2000);

  PlayAudioFile(left_tab);

  // TODO(phoglund): simulate key presses, look for changes in typing detection
  // state.
  SleepInJavascript(left_tab, 10000);

  AssertNoAsynchronousErrors(left_tab);
  AssertNoAsynchronousErrors(right_tab);

  HangUp(left_tab);
  WaitUntilHangupVerified(left_tab);
  WaitUntilHangupVerified(right_tab);

  AssertNoAsynchronousErrors(left_tab);
  AssertNoAsynchronousErrors(right_tab);

  ASSERT_TRUE(peerconnection_server_.Stop());
}
