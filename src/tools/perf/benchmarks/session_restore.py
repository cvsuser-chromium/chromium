# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from telemetry import test

from measurements import session_restore


class SessionRestoreColdTypical25(test.Test):
  tag = 'cold'
  test = session_restore.SessionRestore
  page_set = 'page_sets/typical_25.json'
  options = {'cold': True,
             'pageset_repeat_iters': 5}


class SessionRestoreWarmTypical25(test.Test):
  tag = 'warm'
  test = session_restore.SessionRestore
  page_set = 'page_sets/typical_25.json'
  options = {'warm': True,
             'pageset_repeat_iters': 20}
