# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Defines a set of constants shared by test runners and other scripts."""

import collections
import os
import subprocess
import sys


DIR_SOURCE_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                               os.pardir, os.pardir, os.pardir))
ISOLATE_DEPS_DIR = os.path.join(DIR_SOURCE_ROOT, 'isolate_deps_dir')

CHROMIUM_TEST_SHELL_HOST_DRIVEN_DIR = os.path.join(
    DIR_SOURCE_ROOT, 'chrome', 'android')


PackageInfo = collections.namedtuple('PackageInfo',
    ['package', 'activity', 'cmdline_file', 'devtools_socket',
     'test_package'])

PACKAGE_INFO = {
    'chrome': PackageInfo(
        'com.google.android.apps.chrome',
        'com.google.android.apps.chrome.Main',
        '/data/local/chrome-command-line',
        'chrome_devtools_remote',
        'com.google.android.apps.chrome.tests'),
    'chrome_beta': PackageInfo(
        'com.chrome.beta',
        'com.google.android.apps.chrome.Main',
        '/data/local/chrome-command-line',
        'chrome_devtools_remote',
        None),
    'chrome_stable': PackageInfo(
        'com.android.chrome',
        'com.google.android.apps.chrome.Main',
        '/data/local/chrome-command-line',
        'chrome_devtools_remote',
        None),
    'chrome_dev': PackageInfo(
        'com.google.android.apps.chrome_dev',
        'com.google.android.apps.chrome.Main',
        '/data/local/chrome-command-line',
        'chrome_devtools_remote',
        None),
    'legacy_browser': PackageInfo(
        'com.google.android.browser',
        'com.android.browser.BrowserActivity',
        None,
        None,
        None),
    'content_shell': PackageInfo(
        'org.chromium.content_shell_apk',
        'org.chromium.content_shell_apk.ContentShellActivity',
        '/data/local/tmp/content-shell-command-line',
        None,
        'org.chromium.content_shell_apk.tests'),
    'chromium_test_shell': PackageInfo(
        'org.chromium.chrome.testshell',
        'org.chromium.chrome.testshell.ChromiumTestShellActivity',
        '/data/local/tmp/chromium-testshell-command-line',
        'chromium_testshell_devtools_remote',
        'org.chromium.chrome.testshell.tests'),
    'android_webview_shell': PackageInfo(
        'org.chromium.android_webview.shell',
        'org.chromium.android_webview.shell.AwShellActivity',
        None,
        None,
        'org.chromium.android_webview.test'),
    'gtest': PackageInfo(
        'org.chromium.native_test',
        'org.chromium.native_test.ChromeNativeTestActivity',
        '/data/local/tmp/chrome-native-tests-command-line',
        None,
        None),
    'content_browsertests': PackageInfo(
        'org.chromium.content_browsertests_apk',
        'org.chromium.content_browsertests_apk.ContentBrowserTestsActivity',
        '/data/local/tmp/content-browser-tests-command-line',
        None,
        None),
    'chromedriver_webview_shell': PackageInfo(
        'org.chromium.chromedriver_webview_shell',
        'org.chromium.chromedriver_webview_shell.Main',
        None,
        None,
        None),
}


# Ports arrangement for various test servers used in Chrome for Android.
# Lighttpd server will attempt to use 9000 as default port, if unavailable it
# will find a free port from 8001 - 8999.
LIGHTTPD_DEFAULT_PORT = 9000
LIGHTTPD_RANDOM_PORT_FIRST = 8001
LIGHTTPD_RANDOM_PORT_LAST = 8999
TEST_SYNC_SERVER_PORT = 9031
TEST_SEARCH_BY_IMAGE_SERVER_PORT = 9041

# The net test server is started from port 10201.
# TODO(pliard): http://crbug.com/239014. Remove this dirty workaround once
# http://crbug.com/239014 is fixed properly.
TEST_SERVER_PORT_FIRST = 10201
TEST_SERVER_PORT_LAST = 30000
# A file to record next valid port of test server.
TEST_SERVER_PORT_FILE = '/tmp/test_server_port'
TEST_SERVER_PORT_LOCKFILE = '/tmp/test_server_port.lock'

TEST_EXECUTABLE_DIR = '/data/local/tmp'
# Directories for common java libraries for SDK build.
# These constants are defined in build/android/ant/common.xml
SDK_BUILD_JAVALIB_DIR = 'lib.java'
SDK_BUILD_TEST_JAVALIB_DIR = 'test.lib.java'
SDK_BUILD_APKS_DIR = 'apks'

PERF_OUTPUT_DIR = os.path.join(DIR_SOURCE_ROOT, 'out', 'step_results')
# The directory on the device where perf test output gets saved to.
DEVICE_PERF_OUTPUT_DIR = (
    '/data/data/' + PACKAGE_INFO['chrome'].package + '/files')

SCREENSHOTS_DIR = os.path.join(DIR_SOURCE_ROOT, 'out_screenshots')

ANDROID_SDK_VERSION = 18
ANDROID_SDK_ROOT = os.path.join(DIR_SOURCE_ROOT,
                                'third_party/android_tools/sdk')
ANDROID_NDK_ROOT = os.path.join(DIR_SOURCE_ROOT,
                                'third_party/android_tools/ndk')

EMULATOR_SDK_ROOT = os.path.join(DIR_SOURCE_ROOT, 'android_emulator_sdk')

UPSTREAM_FLAKINESS_SERVER = 'test-results.appspot.com'


def GetBuildType():
  try:
    return os.environ['BUILDTYPE']
  except KeyError:
    raise Exception('The BUILDTYPE environment variable has not been set')


def SetBuildType(build_type):
  os.environ['BUILDTYPE'] = build_type


def GetOutDirectory(build_type=None):
  """Returns the out directory where the output binaries are built.

  Args:
    build_type: Build type, generally 'Debug' or 'Release'. Defaults to the
      globally set build type environment variable BUILDTYPE.
  """
  return os.path.abspath(os.path.join(
      DIR_SOURCE_ROOT, os.environ.get('CHROMIUM_OUT_DIR', 'out'),
      GetBuildType() if build_type is None else build_type))


def _GetADBPath():
  if os.environ.get('ANDROID_SDK_ROOT'):
    return 'adb'
  # If envsetup.sh hasn't been sourced and there's no adb in the path,
  # set it here.
  try:
    with file(os.devnull, 'w') as devnull:
      subprocess.call(['adb', 'version'], stdout=devnull, stderr=devnull)
    return 'adb'
  except OSError:
    print >> sys.stderr, 'No adb found in $PATH, fallback to checked in binary.'
    return os.path.join(ANDROID_SDK_ROOT, 'platform-tools', 'adb')


ADB_PATH = _GetADBPath()

# Exit codes
ERROR_EXIT_CODE = 1
WARNING_EXIT_CODE = 88
