// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/scoped_native_library.h"
#include "base/strings/stringprintf.h"
#include "base/win/windows_version.h"
#include "chrome/browser/media/webrtc_browsertest_base.h"
#include "chrome/browser/media/webrtc_browsertest_common.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
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

static const base::FilePath::CharType kToolsPath[] =
    FILE_PATH_LITERAL("pyauto_private/media/tools");

static const char kMainWebrtcTestHtmlPage[] =
    "files/webrtc/webrtc_audio_quality_test.html";

static base::FilePath GetTestDataDir() {
  base::FilePath source_dir;
  PathService::Get(chrome::DIR_TEST_DATA, &source_dir);
  return source_dir;
}

// Test we can set up a WebRTC call and play audio through it.
//
// You must have the src-internal solution in your .gclient to put the required
// pyauto_private directory into chrome/test/data/.
//
// This test will only work on machines that have been configured to record
// their own input.
//
// On Linux:
// 1. # sudo apt-get install pavucontrol
// 2. For the user who will run the test: # pavucontrol
// 3. In a separate terminal, # arecord dummy
// 4. In pavucontrol, go to the recording tab.
// 5. For the ALSA plug-in [aplay]: ALSA Capture from, change from <x> to
//    <Monitor of x>, where x is whatever your primary sound device is called.
// 6. Try launching chrome as the target user on the target machine, try
//    playing, say, a YouTube video, and record with # arecord -f dat tmp.dat.
//    Verify the recording with aplay (should have recorded what you played
//    from chrome).
//
// Note: the volume for ALL your input devices will be forced to 100% by
//       running this test on Linux.
//
// On Windows 7:
// 1. Control panel > Sound > Manage audio devices.
// 2. In the recording tab, right-click in an empty space in the pane with the
//    devices. Tick 'show disabled devices'.
// 3. You should see a 'stero mix' device - this is what your speakers output.
//    Right click > Properties.
// 4. In the Listen tab for the mix device, check the 'listen to this device'
//    checkbox. Ensure the mix device is the default recording device.
// 5. Launch chrome and try playing a video with sound. You should see
//    in the volume meter for the mix device. Configure the mix device to have
//    50 / 100 in level. Also go into the playback tab, right-click Speakers,
//    and set that level to 50 / 100. Otherwise you will get distortion in
//    the recording.
class WebrtcAudioQualityBrowserTest : public WebRtcTestBase {
 public:
  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE {
    PeerConnectionServerRunner::KillAllPeerConnectionServersOnCurrentSystem();
  }

  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    // This test expects real device handling and requires a real webcam / audio
    // device; it will not work with fake devices.
    EXPECT_FALSE(command_line->HasSwitch(
        switches::kUseFakeDeviceForMediaStream));
    EXPECT_FALSE(command_line->HasSwitch(
        switches::kUseFakeUIForMediaStream));
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

  // Ensures we didn't get any errors asynchronously (e.g. while no javascript
  // call from this test was outstanding).
  // TODO(phoglund): this becomes obsolete when we switch to communicating with
  // the DOM message queue.
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

  base::FilePath CreateTemporaryWaveFile() {
    base::FilePath filename;
    EXPECT_TRUE(file_util::CreateTemporaryFile(&filename));
    base::FilePath wav_filename =
        filename.AddExtension(FILE_PATH_LITERAL(".wav"));
    EXPECT_TRUE(base::Move(filename, wav_filename));
    return wav_filename;
  }

  PeerConnectionServerRunner peerconnection_server_;
};

class AudioRecorder {
 public:
  AudioRecorder(): recording_application_(base::kNullProcessHandle) {}
  ~AudioRecorder() {}

  // Starts the recording program for the specified duration. Returns true
  // on success.
  bool StartRecording(int duration_sec, const base::FilePath& output_file,
                      bool mono) {
    EXPECT_EQ(base::kNullProcessHandle, recording_application_)
        << "Tried to record, but is already recording.";

    CommandLine command_line(CommandLine::NO_PROGRAM);
#if defined(OS_WIN)
    // This disable is required to run SoundRecorder.exe on 64-bit Windows
    // from a 32-bit binary. We need to load the wow64 disable function from
    // the DLL since it doesn't exist on Windows XP.
    // TODO(phoglund): find some cleaner solution than using SoundRecorder.exe.
    base::ScopedNativeLibrary kernel32_lib(base::FilePath(L"kernel32"));
    if (kernel32_lib.is_valid()) {
      typedef BOOL (WINAPI* Wow64DisableWow64FSRedirection)(PVOID*);
      Wow64DisableWow64FSRedirection wow_64_disable_wow_64_fs_redirection;
      wow_64_disable_wow_64_fs_redirection =
          reinterpret_cast<Wow64DisableWow64FSRedirection>(
              kernel32_lib.GetFunctionPointer(
                  "Wow64DisableWow64FsRedirection"));
      if (wow_64_disable_wow_64_fs_redirection != NULL) {
        PVOID* ignored = NULL;
        wow_64_disable_wow_64_fs_redirection(ignored);
      }
    }

    char duration_in_hms[128] = {0};
    struct tm duration_tm = {0};
    duration_tm.tm_sec = duration_sec;
    EXPECT_NE(0u, strftime(duration_in_hms, arraysize(duration_in_hms),
                           "%H:%M:%S", &duration_tm));

    command_line.SetProgram(
        base::FilePath(FILE_PATH_LITERAL("SoundRecorder.exe")));
    command_line.AppendArg("/FILE");
    command_line.AppendArgPath(output_file);
    command_line.AppendArg("/DURATION");
    command_line.AppendArg(duration_in_hms);
#else
    int num_channels = mono ? 1 : 2;
    command_line.SetProgram(base::FilePath("arecord"));
    command_line.AppendArg("-d");
    command_line.AppendArg(base::StringPrintf("%d", duration_sec));
    command_line.AppendArg("-f");
    command_line.AppendArg("dat");
    command_line.AppendArg("-c");
    command_line.AppendArg(base::StringPrintf("%d", num_channels));
    command_line.AppendArgPath(output_file);
#endif

    LOG(INFO) << "Running " << command_line.GetCommandLineString();
    return base::LaunchProcess(command_line, base::LaunchOptions(),
                               &recording_application_);
  }

  // Joins the recording program. Returns true on success.
  bool WaitForRecordingToEnd() {
    int exit_code = -1;
    base::WaitForExitCode(recording_application_, &exit_code);
    return exit_code == 0;
  }
 private:
  base::ProcessHandle recording_application_;
};

bool ForceMicrophoneVolumeTo100Percent() {
#if defined(OS_WIN)
  CommandLine command_line(GetTestDataDir().Append(kToolsPath).Append(
      FILE_PATH_LITERAL("force_mic_volume_max.exe")));
  LOG(INFO) << "Running " << command_line.GetCommandLineString();
  std::string result;
  if (!base::GetAppOutput(command_line, &result)) {
    LOG(ERROR) << "Failed to set source volume: output was " << result;
    return false;
  }
#else
  // Just force the volume of, say the first 5 devices. A machine will rarely
  // have more input sources than that. This is way easier than finding the
  // input device we happen to be using.
  for (int device_index = 0; device_index < 5; ++device_index) {
    std::string result;
    const std::string kHundredPercentVolume = "65536";
    CommandLine command_line(base::FilePath(FILE_PATH_LITERAL("pacmd")));
    command_line.AppendArg("set-source-volume");
    command_line.AppendArg(base::StringPrintf("%d", device_index));
    command_line.AppendArg(kHundredPercentVolume);
    LOG(INFO) << "Running " << command_line.GetCommandLineString();
    if (!base::GetAppOutput(command_line, &result)) {
      LOG(ERROR) << "Failed to set source volume: output was " << result;
      return false;
    }
  }
#endif
  return true;
}

// Removes silence from beginning and end of the |input_audio_file| and writes
// the result to the |output_audio_file|. Returns true on success.
bool RemoveSilence(const base::FilePath& input_file,
                   const base::FilePath& output_file) {
  // SOX documentation for silence command: http://sox.sourceforge.net/sox.html
  // To remove the silence from both beginning and end of the audio file, we
  // call sox silence command twice: once on normal file and again on its
  // reverse, then we reverse the final output.
  // Silence parameters are (in sequence):
  // ABOVE_PERIODS: The period for which silence occurs. Value 1 is used for
  //                 silence at beginning of audio.
  // DURATION: the amount of time in seconds that non-silence must be detected
  //           before sox stops trimming audio.
  // THRESHOLD: value used to indicate what sample value is treates as silence.
  const char* kAbovePeriods = "1";
  const char* kDuration = "2";
  const char* kTreshold = "5%";

#if defined(OS_WIN)
  CommandLine command_line(GetTestDataDir().Append(kToolsPath).Append(
      FILE_PATH_LITERAL("sox.exe")));
#else
  CommandLine command_line(base::FilePath(FILE_PATH_LITERAL("sox")));
#endif
  command_line.AppendArgPath(input_file);
  command_line.AppendArgPath(output_file);
  command_line.AppendArg("silence");
  command_line.AppendArg(kAbovePeriods);
  command_line.AppendArg(kDuration);
  command_line.AppendArg(kTreshold);
  command_line.AppendArg("reverse");
  command_line.AppendArg("silence");
  command_line.AppendArg(kAbovePeriods);
  command_line.AppendArg(kDuration);
  command_line.AppendArg(kTreshold);
  command_line.AppendArg("reverse");

  LOG(INFO) << "Running " << command_line.GetCommandLineString();
  std::string result;
  bool ok = base::GetAppOutput(command_line, &result);
  LOG(INFO) << "Output was:\n\n" << result;
  return ok;
}

bool CanParseAsFloat(const std::string& value) {
  return atof(value.c_str()) != 0 || value == "0";
}

// Runs PESQ to compare |reference_file| to a |actual_file|. The |sample_rate|
// can be either 16000 or 8000.
//
// PESQ is only mono-aware, so the files should preferably be recorded in mono.
// Furthermore it expects the file to be 16 rather than 32 bits, even though
// 32 bits might work. The audio bandwidth of the two files should be the same
// e.g. don't compare a 32 kHz file to a 8 kHz file.
//
// The raw score in MOS is written to |raw_mos|, whereas the MOS-LQO score is
// written to mos_lqo. The scores are returned as floats in string form (e.g.
// "3.145", etc). Returns true on success.
bool RunPesq(const base::FilePath& reference_file,
             const base::FilePath& actual_file,
             int sample_rate, std::string* raw_mos, std::string* mos_lqo) {
  // PESQ will break if the paths are too long (!).
  EXPECT_LT(reference_file.value().length(), 128u);
  EXPECT_LT(actual_file.value().length(), 128u);

#if defined(OS_WIN)
  base::FilePath pesq_path =
      GetTestDataDir().Append(kToolsPath).Append(FILE_PATH_LITERAL("pesq.exe"));
#else
  base::FilePath pesq_path =
      GetTestDataDir().Append(kToolsPath).Append(FILE_PATH_LITERAL("pesq"));
#endif

  if (!base::PathExists(pesq_path)) {
    LOG(ERROR) << "Missing PESQ binary in " << pesq_path.value();
    return false;
  }

  CommandLine command_line(pesq_path);
  command_line.AppendArg(base::StringPrintf("+%d", sample_rate));
  command_line.AppendArgPath(reference_file);
  command_line.AppendArgPath(actual_file);

  LOG(INFO) << "Running " << command_line.GetCommandLineString();
  std::string result;
  if (!base::GetAppOutput(command_line, &result)) {
    LOG(ERROR) << "Failed to run PESQ.";
    return false;
  }
  LOG(INFO) << "Output was:\n\n" << result;

  const std::string result_anchor = "Prediction (Raw MOS, MOS-LQO):  = ";
  std::size_t anchor_pos = result.find(result_anchor);
  if (anchor_pos == std::string::npos) {
    LOG(ERROR) << "PESQ was not able to compute a score; we probably recorded "
        << "only silence.";
    return false;
  }

  // There are two tab-separated numbers on the format x.xxx, e.g. 5 chars each.
  std::size_t first_number_pos = anchor_pos + result_anchor.length();
  *raw_mos = result.substr(first_number_pos, 5);
  EXPECT_TRUE(CanParseAsFloat(*raw_mos)) << "Failed to parse raw MOS number.";
  *mos_lqo = result.substr(first_number_pos + 5 + 1, 5);
  EXPECT_TRUE(CanParseAsFloat(*mos_lqo)) << "Failed to parse MOS LQO number.";

  return true;
}

#if defined(OS_LINUX) || defined(OS_WIN)
// Only implemented on Linux and Windows for now.
#define MAYBE_MANUAL_TestAudioQuality MANUAL_TestAudioQuality
#else
#define MAYBE_MANUAL_TestAudioQuality DISABLED_MANUAL_TestAudioQuality
#endif

IN_PROC_BROWSER_TEST_F(WebrtcAudioQualityBrowserTest,
                       MAYBE_MANUAL_TestAudioQuality) {
#if defined(OS_WIN)
  if (base::win::GetVersion() < base::win::VERSION_VISTA) {
    // It would take work to implement this on XP; not prioritized right now.
    LOG(ERROR) << "This test is not implemented for Windows XP.";
    return;
  }
#endif
  ASSERT_TRUE(HasAllRequiredResources());
  // TODO(phoglund): make this use embedded_test_server when that test server
  // can handle files > ~400Kb.
  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(peerconnection_server_.Start());

  ASSERT_TRUE(ForceMicrophoneVolumeTo100Percent());

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

  EXPECT_EQ("ok-peerconnection-created",
            ExecuteJavascript("preparePeerConnection()", left_tab));

  AddAudioFile(kReferenceFileRelativeUrl, left_tab);

  EstablishCall(left_tab, right_tab);

  // Note: the media flow isn't necessarily established on the connection just
  // because the ready state is ok on both sides. We sleep a bit between call
  // establishment and playing to avoid cutting of the beginning of the audio
  // file.
  SleepInJavascript(left_tab, 2000);

  base::FilePath recording = CreateTemporaryWaveFile();

  // Note: the sound clip is about 10 seconds: record for 15 seconds to get some
  // safety margins on each side.
  AudioRecorder recorder;
  static int kRecordingTimeSeconds = 15;
  ASSERT_TRUE(recorder.StartRecording(kRecordingTimeSeconds, recording, true));

  PlayAudioFile(left_tab);

  ASSERT_TRUE(recorder.WaitForRecordingToEnd());
  LOG(INFO) << "Done recording to " << recording.value() << std::endl;

  AssertNoAsynchronousErrors(left_tab);
  AssertNoAsynchronousErrors(right_tab);

  HangUp(left_tab);
  WaitUntilHangupVerified(left_tab);
  WaitUntilHangupVerified(right_tab);

  AssertNoAsynchronousErrors(left_tab);
  AssertNoAsynchronousErrors(right_tab);

  base::FilePath trimmed_recording = CreateTemporaryWaveFile();

  ASSERT_TRUE(RemoveSilence(recording, trimmed_recording));
  LOG(INFO) << "Trimmed silence: " << trimmed_recording.value() << std::endl;

  std::string raw_mos;
  std::string mos_lqo;
  base::FilePath reference_file_in_test_dir =
      GetTestDataDir().Append(kReferenceFile);
  ASSERT_TRUE(RunPesq(reference_file_in_test_dir, trimmed_recording, 16000,
                      &raw_mos, &mos_lqo));

  perf_test::PrintResult("audio_pesq", "", "raw_mos", raw_mos, "score", true);
  perf_test::PrintResult("audio_pesq", "", "mos_lqo", mos_lqo, "score", true);

  EXPECT_TRUE(base::DeleteFile(recording, false));
  EXPECT_TRUE(base::DeleteFile(trimmed_recording, false));

  ASSERT_TRUE(peerconnection_server_.Stop());
}
