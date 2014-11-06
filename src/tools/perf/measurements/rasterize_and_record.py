# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import sys
import time

from metrics import rendering_stats
from telemetry.page import page_measurement
from telemetry.core.timeline.model import MarkerMismatchError
from telemetry.core.timeline.model import MarkerOverlapError

TIMELINE_MARKER = 'RasterizeAndRecord'


class RasterizeAndRecord(page_measurement.PageMeasurement):
  def __init__(self):
    super(RasterizeAndRecord, self).__init__('', True)
    self._metrics = None
    self._compositing_features_enabled = False

  def AddCommandLineOptions(self, parser):
    parser.add_option('--raster-record-repeat', dest='raster_record_repeat',
                      default=20,
                      help='Repetitions in raster and record loops.' +
                      'Higher values reduce variance, but can cause' +
                      'instability (timeouts, event buffer overflows, etc.).')
    parser.add_option('--start-wait-time', dest='start_wait_time',
                      default=5,
                      help='Wait time before the benchmark is started ' +
                      '(must be long enought to load all content)')
    parser.add_option('--stop-wait-time', dest='stop_wait_time',
                      default=15,
                      help='Wait time before measurement is taken ' +
                      '(must be long enough to render one frame)')

  def CustomizeBrowserOptions(self, options):
    # Run each raster task N times. This allows us to report the time for the
    # best run, effectively excluding cache effects and time when the thread is
    # de-scheduled.
    options.AppendExtraBrowserArgs([
        '--enable-gpu-benchmarking',
        '--slow-down-raster-scale-factor=%d' % int(
            options.raster_record_repeat),
        # Enable impl-side-painting. Current version of benchmark only works for
        # this mode.
        '--enable-impl-side-painting',
        '--force-compositing-mode',
        '--enable-threaded-compositing'
    ])

  def DidStartBrowser(self, browser):
    # Check if the we actually have threaded forced compositing enabled.
    system_info = browser.GetSystemInfo()
    if (system_info.gpu.feature_status
        and system_info.gpu.feature_status.get(
            'compositing', None) == 'enabled_force_threaded'):
      self._compositing_features_enabled = True

  def MeasurePage(self, page, tab, results):
    # Exit if threaded forced compositing is not enabled.
    if (not self._compositing_features_enabled):
      logging.warning('Warning: compositing feature status unknown or not '+
                      'forced and threaded. Skipping measurement.')
      sys.exit(0)

    # TODO(ernstm): Remove this temporary workaround when reference build has
    # been updated to branch 1671 or later.
    backend = tab.browser._browser_backend # pylint: disable=W0212
    if (not hasattr(backend, 'chrome_branch_number') or
        (sys.platform != 'android' and backend.chrome_branch_number < 1671)):
      print ('Warning: rasterize_and_record requires Chrome branch 1671 or '
             'later. Skipping measurement.')
      sys.exit(0)

    # Rasterize only what's visible.
    tab.ExecuteJavaScript(
        'chrome.gpuBenchmarking.setRasterizeOnlyVisibleContent();')

    # Wait until the page has loaded and come to a somewhat steady state.
    # Needs to be adjusted for every device (~2 seconds for workstation).
    time.sleep(float(self.options.start_wait_time))

    # Render one frame before we start gathering a trace. On some pages, the
    # first frame requested has more variance in the number of pixels
    # rasterized.
    tab.ExecuteJavaScript(
        'window.__rafFired = false;'
        'window.webkitRequestAnimationFrame(function() {'
          'chrome.gpuBenchmarking.setNeedsDisplayOnAllLayers();'
          'window.__rafFired  = true;'
        '});')

    time.sleep(float(self.options.stop_wait_time))
    tab.browser.StartTracing('webkit.console,benchmark', 60)

    tab.ExecuteJavaScript(
        'window.__rafFired = false;'
        'window.webkitRequestAnimationFrame(function() {'
          'chrome.gpuBenchmarking.setNeedsDisplayOnAllLayers();'
          'console.time("' + TIMELINE_MARKER + '");'
          'window.__rafFired  = true;'
        '});')
    # Wait until the frame was drawn.
    # Needs to be adjusted for every device and for different
    # raster_record_repeat counts.
    # TODO(ernstm): replace by call-back.
    time.sleep(float(self.options.stop_wait_time))
    tab.ExecuteJavaScript(
        'console.timeEnd("' + TIMELINE_MARKER + '")')

    timeline = tab.browser.StopTracing().AsTimelineModel()
    try:
      timeline_markers = timeline.FindTimelineMarkers(TIMELINE_MARKER)
    except (MarkerMismatchError, MarkerOverlapError) as e:
      raise page_measurement.MeasurementFailure(str(e))
    renderer_process = timeline.GetRendererProcessFromTab(tab)
    stats = rendering_stats.RenderingStats(renderer_process, timeline_markers)

    results.Add('rasterize_time', 'ms',
                max(stats.rasterize_time))
    results.Add('record_time', 'ms',
                max(stats.record_time))
    results.Add('rasterized_pixels', 'pixels',
                max(stats.rasterized_pixel_count))
    results.Add('recorded_pixels', 'pixels',
                max(stats.recorded_pixel_count))
