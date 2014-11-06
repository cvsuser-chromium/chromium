# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import unittest

from telemetry.core.platform import android_platform_backend
from telemetry.unittest import system_stub


class MockAdbCommands(object):
  def __init__(self, mock_content):
    self.mock_content = mock_content

  def CanAccessProtectedFileContents(self):
    return True

  # pylint: disable=W0613
  def GetProtectedFileContents(self, file_name, log_result):
    return self.mock_content


class AndroidPlatformBackendTest(unittest.TestCase):
  def setUp(self):
    self._stubs = system_stub.Override(android_platform_backend,
                                       ['perf_control', 'thermal_throttle'])

  def tearDown(self):
    self._stubs.Restore()

  def testGetCpuStats(self):
    proc_stat_content = [
        '7702 (.android.chrome) S 167 167 0 0 -1 1077936448 '
        '3247 0 0 0 4 1 0 0 20 0 9 0 5603962 337379328 5867 '
        '4294967295 1074458624 1074463824 3197495984 3197494152 '
        '1074767676 0 4612 0 38136 4294967295 0 0 17 0 0 0 0 0 0 '
        '1074470376 1074470912 1102155776']
    adb_valid_proc_content = MockAdbCommands(proc_stat_content)
    backend = android_platform_backend.AndroidPlatformBackend(
        adb_valid_proc_content, False)
    cpu_stats = backend.GetCpuStats('7702')
    self.assertEquals(cpu_stats, {'CpuProcessTime': 5.0})

  def testGetCpuStatsInvalidPID(self):
    # Mock an empty /proc/pid/stat.
    adb_empty_proc_stat = MockAdbCommands([])
    backend = android_platform_backend.AndroidPlatformBackend(
        adb_empty_proc_stat, False)
    cpu_stats = backend.GetCpuStats('7702')
    self.assertEquals(cpu_stats, {})
