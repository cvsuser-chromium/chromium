# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from metrics import startup_metric
from telemetry.page import page_measurement

class Startup(page_measurement.PageMeasurement):
  """Performs a measurement of Chromium's startup performance.

  This test must be invoked with either --warm or --cold on the command line. A
  cold start means none of the Chromium files are in the disk cache. A warm
  start assumes the OS has already cached much of Chromium's content. For warm
  tests, you should repeat the page set to ensure it's cached.
  """

  def __init__(self):
    super(Startup, self).__init__(needs_browser_restart_after_each_run=True)

  def AddCommandLineOptions(self, parser):
    parser.add_option('--cold', action='store_true',
                      help='Clear the OS disk cache before performing the test')
    parser.add_option('--warm', action='store_true',
                      help='Start up with everything already cached')

  def CustomizeBrowserOptions(self, options):
    # TODO: Once the bots start running benchmarks, enforce that either --warm
    # or --cold is explicitly specified.
    # assert options.warm != options.cold, \
    #     "You must specify either --warm or --cold"
    if options.cold:
      browser_options = options.browser_options
      browser_options.clear_sytem_cache_for_browser_and_profile_on_start = True
    else:
      self.discard_first_result = True

    options.AppendExtraBrowserArgs([
        '--enable-stats-collection-bindings'
    ])

  def MeasurePage(self, page, tab, results):
    startup_metric.StartupMetric().AddResults(tab, results)
