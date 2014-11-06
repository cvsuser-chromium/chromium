# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""PeaceKeeper benchmark suite.

Peacekeeper measures browser's performance by testing its JavaScript
functionality. JavaScript is a widely used programming language used in the
creation of modern websites to provide features such as animation, navigation,
forms and other common requirements. By measuring a browser's ability to handle
commonly used JavaScript functions Peacekeeper can evaluate its performance.
Peacekeeper scores are measured in operations per second or rendered frames per
second depending on the test. Final Score is computed by calculating geometric
mean of individual tests scores.
"""

import os

from metrics import statistics
from telemetry import test
from telemetry.page import page_measurement
from telemetry.page import page_set


class PeaceKeeperMeasurement(page_measurement.PageMeasurement):

  def WillNavigateToPage(self, page, tab):
    page.script_to_evaluate_on_commit = """
        var __results = {};
        var _done = false;
        var __real_log = window.console.log;
        var test_frame = null;
        var benchmark = null;
        window.console.log = function(msg) {
          if (typeof(msg) == "string" && (msg.indexOf("benchmark")) == 0) {
            test_frame = document.getElementById("testFrame");
            benchmark =  test_frame.contentWindow.benchmark;
            test_frame.contentWindow.onbeforeunload = {};
            if ((msg.indexOf("Submit ok.")) != -1) {
              _done = true;
              var __data = {};
              __results["test"] = benchmark.testObjectName;
              __results["score"] = benchmark.test.result;
              if (typeof(benchmark.test.unit) != "undefined") {
                __results["unit"] = benchmark.test.unit;
              } else {
                __results["unit"] = benchmark.test.isFps ? "fps" : "ops";
              }
            }
          }
          __real_log.apply(this, [msg]);
        }
        """

  def MeasurePage(self, _, tab, results):
    tab.WaitForJavaScriptExpression('_done', 600)
    result = tab.EvaluateJavaScript('__results')

    results.Add('Score', 'score', int(result['score']), result['test'],
                'unimportant')

  def DidRunTest(self, browser, results):
    # Calculate geometric mean as the total for the combined tests.
    scores = []
    for result in results.page_results:
      scores.append(result['Score'].output_value)
    total = statistics.GeometricMean(scores)
    results.AddSummary('Score', 'score', total, 'Total')


class PeaceKeeperBenchmark(test.Test):
  """A base class for Peackeeper benchmarks."""
  test = PeaceKeeperMeasurement

  def CreatePageSet(self, options):
    """Makes a PageSet for PeaceKeeper benchmarks."""
    # Subclasses are expected to define a class member called query_param.
    if not hasattr(self, 'test_param'):
      raise NotImplementedError('test_param not in PeaceKeeper benchmark.')

    # The docstring of benchmark classes may also be used as a description
    # when 'run_benchmarks list' is run.
    description = self.__doc__ or 'PeaceKeeper Benchmark'
    test_urls = []
    for test_name in self.test_param:
      test_urls.append(
        {"url": ("http://peacekeeper.futuremark.com/run.action?debug=true&"
                 "repeat=false&forceSuiteName=%s&forceTestName=%s") %
                 (self.tag, test_name)
        })

    page_set_dict = {
        'description': description,
        'archive_data_file': '../page_sets/data/peacekeeper_%s.json' % self.tag,
        'make_javascript_deterministic': False,
        'pages': test_urls,
    }
    return page_set.PageSet.FromDict(page_set_dict, os.path.abspath(__file__))


class PeaceKeeperRender(PeaceKeeperBenchmark):
  """PeaceKeeper rendering benchmark suite.

  These tests measure your browser's ability to render and modify specific
  elements used in typical web pages. Rendering tests manipulate the DOM tree in
  real-time. The tests measure display updating speed (frames per seconds).
  """
  tag = 'render'
  test_param = ['renderGrid01',
                'renderGrid02',
                'renderGrid03',
                'renderPhysics'
               ]


class PeaceKeeperData(PeaceKeeperBenchmark):
  """PeaceKeeper Data operations benchmark suite.

  These tests measure your browser's ability to add, remove and modify data
  stored in an array. The Data suite consists of two tests:
  1. arrayCombined: This test uses all features of the JavaScript Array object.
  This is a technical test that is not based on profiled data.
  The source data are different sized arrays of numbers.
  2. arrayWeighted: This test is similar to 'arrayCombined', but the load is
  balanced based on profiled data. The source data is a list of all the
  countries in the world.
  """

  tag = 'array'
  test_param = ['arrayCombined01',
                'arrayWeighted'
               ]


class PeaceKeeperDom(PeaceKeeperBenchmark):
  """PeaceKeeper DOM operations benchmark suite.

  These tests emulate the methods used to create typical dynamic webpages.
  The DOM tests are based on development experience and the capabilities of the
  jQuery framework.
  1. domGetElements: This test uses native DOM methods getElementById and
    getElementsByName. The elements are not modified.
  2. domDynamicCreationCreateElement: A common use of DOM is to dynamically
    create content with JavaScript, this test measures creating objects
    individually and then appending them to DOM.
  3. domDynamicCreationInnerHTML: This test is similarl to the previous one,
    but uses the innerHTML-method.
  4. domJQueryAttributeFilters: This test does a DOM query with jQuery.
    It searches elements with specific attributes.
  5. domJQueryBasicFilters: This test uses filters to query elements from DOM.
  6. domJQueryBasics: This test queries elements from DOM with basic methods.
    It is similar to domGetElements, but uses jQuery rather than native methods.
  7. domJQueryContentFilters: Query elements based on content. This does string
    searching and these methods are assumed to be time consuming.
  8. domJQueryHierarchy: Query elements based on hierarchy, such as getting
    sibling, parent or child nodes from a DOM tree.
  9. domQueryselector: QuerySelector, which allows JavaScript to search elements
    from the DOM tree directly without the need to iterate the whole tree
    through domGetElements.
  """

  tag = 'dom'
  test_param = ['domGetElements',
                'domDynamicCreationCreateElement',
                'domDynamicCreationInnerHTML',
                'domJQueryAttributeFilters',
                'domJQueryBasicFilters',
                'domJQueryBasics',
                'domJQueryContentFilters',
                'domJQueryHierarchy',
                'domQueryselector'
               ]


class PeaceKeeperTextParsing(PeaceKeeperBenchmark):
  """PeaceKeeper Text Parsing benchmark suite.

  These tests measure your browser's performance in typical text manipulations
  such as using a profanity filter for chats, browser detection and form
  validation.
  1. stringChat: This test removes swearing from artificial chat messages.
    Test measures looping and string replace-method.
  2. stringDetectBrowser: This test uses string indexOf-method to detect browser
    and operating system.
  3. stringFilter: This test filters a list of movies with a given keyword.
    The behaviour is known as filtering select or continuous filter. It's used
    to give real time suggestions while a user is filling input fields.
    The test uses simple regular expressions.
  4. stringValidateForm: This test uses complex regular expressions to validate
    user input.
  5. stringWeighted: This is an artificial test. Methods used and their
    intensities are chosen based on profiled data.
  """

  tag = 'string'
  test_param = ['stringChat',
                'stringDetectBrowser',
                'stringFilter',
                'stringWeighted',
                'stringValidateForm'
               ]

