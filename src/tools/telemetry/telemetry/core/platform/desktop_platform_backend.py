# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess

from telemetry.core import util
from telemetry.core.platform import platform_backend


class DesktopPlatformBackend(platform_backend.PlatformBackend):

  # This is an abstract class. It is OK to have abstract methods.
  # pylint: disable=W0223

  def GetFlushUtilityName(self):
    return NotImplementedError()

  def FlushSystemCacheForDirectory(self, directory, ignoring=None):
    assert directory and os.path.exists(directory), \
        'Target directory %s must exist' % directory
    flush_command = util.FindSupportBinary(self.GetFlushUtilityName())
    assert flush_command, \
        'You must build %s first' % self.GetFlushUtilityName()

    args = []
    directory_contents = os.listdir(directory)
    for item in directory_contents:
      if not ignoring or item not in ignoring:
        args.append(os.path.join(directory, item))

    if not args:
      return

    # According to msdn:
    # http://msdn.microsoft.com/en-us/library/ms682425%28VS.85%29.aspx
    # there's a maximum allowable command line of 32,768 characters on windows.
    while args:
      # Small note about [:256] and [256:]
      # [:N] will return a list with the first N elements, ie.
      # with [1,2,3,4,5], [:2] -> [1,2], and [2:] -> [3,4,5]
      # with [1,2,3,4,5], [:5] -> [1,2,3,4,5] and [5:] -> []
      p = subprocess.Popen([flush_command, '--recurse'] + args[:256])
      p.wait()
      assert p.returncode == 0, 'Failed to flush system cache'
      args = args[256:]