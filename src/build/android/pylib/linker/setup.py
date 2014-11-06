# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Setup for linker tests."""

import os
import sys
import types

import test_case
import test_runner

from pylib import constants

sys.path.insert(0,
                os.path.join(constants.DIR_SOURCE_ROOT, 'build', 'util', 'lib',
                             'common'))
import unittest_util

def Setup(options, devices):
  """Creates a list of test cases and a runner factory.

  Returns:
    A tuple of (TestRunnerFactory, tests).
  """
  test_cases = [
      test_case.LinkerLibraryAddressTest,
      test_case.LinkerSharedRelroTest,
      test_case.LinkerRandomizationTest ]

  low_memory_modes = [False, True]
  all_tests = [t(is_low_memory=m) for t in test_cases for m in low_memory_modes]

  if options.test_filter:
    all_test_names = [ test.qualified_name for test in all_tests ]
    filtered_test_names = unittest_util.FilterTestNames(all_test_names,
                                                        options.test_filter)
    all_tests = [t for t in all_tests \
                 if t.qualified_name in filtered_test_names]

  def TestRunnerFactory(device, shard_index):
    return test_runner.LinkerTestRunner(
        device, options.tool, options.push_deps,
        options.cleanup_test_files)

  return (TestRunnerFactory, all_tests)
