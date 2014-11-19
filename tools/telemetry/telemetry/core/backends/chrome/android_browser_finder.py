# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Finds android browsers that can be controlled by telemetry."""

import os
import logging as real_logging
import re
import subprocess
import sys

from telemetry.core import browser
from telemetry.core import possible_browser
from telemetry.core import util
from telemetry.core.backends import adb_commands
from telemetry.core.backends.chrome import android_browser_backend
from telemetry.core.platform import android_platform_backend

CHROME_PACKAGE_NAMES = {
  'android-content-shell':
      ['org.chromium.content_shell_apk',
       android_browser_backend.ContentShellBackendSettings,
       'ContentShell.apk'],
  'android-chromium-testshell':
      ['org.chromium.chrome.testshell',
       android_browser_backend.ChromiumTestShellBackendSettings,
       'ChromiumTestShell.apk'],
  'android-webview':
      ['com.android.webview.chromium.shell',
       android_browser_backend.WebviewBackendSettings,
       None],
  'android-chrome':
      ['com.google.android.apps.chrome',
       android_browser_backend.ChromeBackendSettings,
       'Chrome.apk'],
  'android-chrome-beta':
      ['com.chrome.beta',
       android_browser_backend.ChromeBackendSettings,
       None],
  'android-chrome-dev':
      ['com.google.android.apps.chrome_dev',
       android_browser_backend.ChromeBackendSettings,
       None],
  'android-jb-system-chrome':
      ['com.android.chrome',
       android_browser_backend.ChromeBackendSettings,
       None]
}

ALL_BROWSER_TYPES = CHROME_PACKAGE_NAMES.keys()

# adb shell pm list packages
# adb
# intents to run (pass -D url for the rest)
#   com.android.chrome/.Main
#   com.google.android.apps.chrome/.Main

class PossibleAndroidBrowser(possible_browser.PossibleBrowser):
  """A launchable android browser instance."""
  def __init__(self, browser_type, finder_options, backend_settings, apk_name):
    super(PossibleAndroidBrowser, self).__init__(browser_type, finder_options)
    assert browser_type in ALL_BROWSER_TYPES, \
        'Please add %s to ALL_BROWSER_TYPES' % browser_type
    self._backend_settings = backend_settings
    self._local_apk = None

    chrome_root = util.GetChromiumSrcDir()
    if apk_name:
      candidate_apks = []
      for build_dir, build_type in util.GetBuildDirectories():
        apk_full_name = os.path.join(chrome_root, build_dir, build_type, 'apks',
                                     apk_name)
        if os.path.exists(apk_full_name):
          last_changed = os.path.getmtime(apk_full_name)
          candidate_apks.append((last_changed, apk_full_name))

      if candidate_apks:
        # Find the canadidate .apk with the latest modification time.
        newest_apk_path = sorted(candidate_apks)[-1][1]
        self._local_apk = newest_apk_path


  def __repr__(self):
    return 'PossibleAndroidBrowser(browser_type=%s)' % self.browser_type

  def Create(self):
    backend = android_browser_backend.AndroidBrowserBackend(
        self.finder_options.browser_options, self._backend_settings,
        self.finder_options.android_rndis,
        output_profile_path=self.finder_options.output_profile_path,
        extensions_to_load=self.finder_options.extensions_to_load)
    platform_backend = android_platform_backend.AndroidPlatformBackend(
        self._backend_settings.adb.Adb(),
        self.finder_options.no_performance_mode)
    b = browser.Browser(backend, platform_backend)
    return b

  def SupportsOptions(self, finder_options):
    if len(finder_options.extensions_to_load) != 0:
      return False
    return True

  def HaveLocalAPK(self):
    return self._local_apk and os.path.exists(self._local_apk)

  def UpdateExecutableIfNeeded(self):
    if self.HaveLocalAPK():
      real_logging.warn(
          'Refreshing %s on device if needed.' % self._local_apk)
      self._backend_settings.adb.Install(self._local_apk)

  def last_modification_time(self):
    if self.HaveLocalAPK():
      return os.path.getmtime(self._local_apk)
    return -1

def SelectDefaultBrowser(possible_browsers):
  local_builds_by_date = sorted(possible_browsers,
                                key=lambda b: b.last_modification_time())

  if local_builds_by_date:
    newest_browser = local_builds_by_date[-1]
    return newest_browser
  return None

adb_works = None
def CanFindAvailableBrowsers(logging=real_logging):
  if not adb_commands.IsAndroidSupported():
    return False

  global adb_works

  if adb_works == None:
    try:
      with open(os.devnull, 'w') as devnull:
        proc = subprocess.Popen(['adb', 'devices'],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                stdin=devnull)
        stdout, _ = proc.communicate()
        if re.search(re.escape('????????????\tno permissions'), stdout) != None:
          logging.warn(
              ('adb devices reported a permissions error. Consider '
               'restarting adb as root:'))
          logging.warn('  adb kill-server')
          logging.warn('  sudo `which adb` devices\n\n')
        adb_works = True
    except OSError:
      platform_tools_path = os.path.join(util.GetChromiumSrcDir(),
          'third_party', 'android_tools', 'sdk', 'platform-tools')
      if (sys.platform.startswith('linux') and
          os.path.exists(os.path.join(platform_tools_path, 'adb'))):
        os.environ['PATH'] = os.pathsep.join([platform_tools_path,
                                              os.environ['PATH']])
        adb_works = True
      else:
        adb_works = False
  return adb_works

def FindAllAvailableBrowsers(finder_options, logging=real_logging):
  """Finds all the desktop browsers available on this machine."""
  if not CanFindAvailableBrowsers(logging=logging):
    logging.info('No adb command found. ' +
                 'Will not try searching for Android browsers.')
    return []

  device = None
  if finder_options.android_device:
    devices = [finder_options.android_device]
  else:
    devices = adb_commands.GetAttachedDevices()

  if len(devices) == 0:
    logging.info('No android devices found.')
    return []

  if len(devices) > 1:
    logging.warn('Multiple devices attached. ' +
                 'Please specify a device explicitly.')
    return []

  device = devices[0]

  adb = adb_commands.AdbCommands(device=device)

  if sys.platform.startswith('linux'):
    # Host side workaround for crbug.com/268450 (adb instability)
    # The adb server has a race which is mitigated by binding to a single core.
    import psutil  # pylint: disable=F0401
    pids  = [p.pid for p in psutil.process_iter() if 'adb' in p.name]
    with open(os.devnull, 'w') as devnull:
      for pid in pids:
        ret = subprocess.call(['taskset', '-p', '-c', '0', str(pid)],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              stdin=devnull)
        if ret:
          logging.warn('Failed to taskset %d (%s)', pid, ret)

    # Experimental device side workaround for crbug.com/268450 (adb instability)
    # The /sbin/adbd process on the device appears to hang which causes adb to
    # report that the device is offline. Our working theory is that killing
    # the process and allowing it to be automatically relaunched will allow us
    # to run for longer before it hangs.
    if not finder_options.keep_test_server_ports:
      # This would break forwarder connections, so we cannot do this if
      # instructed to keep server ports open.
      logging.info('Killing adbd on device')
      adb.KillAll('adbd')
      logging.info('Waiting for adbd to restart')
      adb.Adb().Adb().SendCommand('wait-for-device')

  packages = adb.RunShellCommand('pm list packages')
  possible_browsers = []

  for name, package_info in CHROME_PACKAGE_NAMES.iteritems():
    [package, backend_settings, local_apk] = package_info
    b = PossibleAndroidBrowser(
        name,
        finder_options,
        backend_settings(adb, package),
        local_apk)

    if 'package:' + package in packages or b.HaveLocalAPK():
      possible_browsers.append(b)

  # See if the "forwarder" is installed -- we need this to host content locally
  # but make it accessible to the device.
  if (len(possible_browsers) and not finder_options.android_rndis and
      not adb_commands.HasForwarder()):
    logging.warn('telemetry detected an android device. However,')
    logging.warn('Chrome\'s port-forwarder app is not available.')
    logging.warn('Falling back to prebuilt binaries, but to build locally: ')
    logging.warn('  ninja -C out/Release forwarder2 md5sum')
    logging.warn('')
    logging.warn('')
    if not adb_commands.SetupPrebuiltTools(device):
      return []
  return possible_browsers
