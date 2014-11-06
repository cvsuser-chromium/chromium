# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Impact HTML5 Gaming benchmark.

Tests one very specific use case: smooth running games rendered with the
<canvas> element. The score for the HTML5-Benchmark takes the total time the
browser spent rendering frames (formula is 1000000/(sqrt(totalTime) + lagTime *
0.1)). The benchmark automatically runs at a reasonable screen size. Final
score is a indicator for the browser's ability to smoothly run HTML5 games."""

import os

from telemetry import test
from telemetry.page import page_measurement
from telemetry.page import page_set


class _HTML5GamingMeasurement(page_measurement.PageMeasurement):
  def MeasurePage(self, _, tab, results):
    tab.ExecuteJavaScript('benchmark();')
    # Default value of score element is 87485, its value is updated with actual
    # score when test finish.
    tab.WaitForJavaScriptExpression(
        'document.getElementById("score").innerHTML != "87485"', 200)
    result = int(tab.EvaluateJavaScript(
        'document.getElementById("score").innerHTML'))
    results.Add('Score', 'score', result)


class HTML5Gaming(test.Test):
  """Imapct HTML5 smooth running games benchmark suite."""
  test = _HTML5GamingMeasurement
  def CreatePageSet(self, options):
    return page_set.PageSet.FromDict({
        'archive_data_file': '../page_sets/data/html5gaming.json',
        'make_javascript_deterministic': False,
        'pages': [
          { 'url':
              'http://html5-benchmark.com/'}
           ]
        }, os.path.abspath(__file__))

