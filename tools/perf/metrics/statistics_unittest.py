# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import random

from metrics import statistics


def Relax(samples, iterations=10):
  """Lloyd relaxation in 1D.

  Keeps the position of the first and last sample.
  """
  for _ in xrange(0, iterations):
    voronoi_boundaries = []
    for i in xrange(1, len(samples)):
      voronoi_boundaries.append((samples[i] + samples[i-1]) * 0.5)

    relaxed_samples = []
    relaxed_samples.append(samples[0])
    for i in xrange(1, len(samples)-1):
      relaxed_samples.append(
          (voronoi_boundaries[i-1] + voronoi_boundaries[i]) * 0.5)
    relaxed_samples.append(samples[-1])
    samples = relaxed_samples
  return samples


class StatisticsUnitTest(unittest.TestCase):

  def testNormalizeSamples(self):
    samples = []
    normalized_samples, scale = statistics.NormalizeSamples(samples)
    self.assertEquals(normalized_samples, samples)
    self.assertEquals(scale, 1.0)

    samples = [0.0, 0.0]
    normalized_samples, scale = statistics.NormalizeSamples(samples)
    self.assertEquals(normalized_samples, samples)
    self.assertEquals(scale, 1.0)

    samples = [0.0, 1.0/3.0, 2.0/3.0, 1.0]
    normalized_samples, scale = statistics.NormalizeSamples(samples)
    self.assertEquals(normalized_samples, [1.0/8.0, 3.0/8.0, 5.0/8.0, 7.0/8.0])
    self.assertEquals(scale, 0.75)

    samples = [1.0/8.0, 3.0/8.0, 5.0/8.0, 7.0/8.0]
    normalized_samples, scale = statistics.NormalizeSamples(samples)
    self.assertEquals(normalized_samples, samples)
    self.assertEquals(scale, 1.0)

  def testDiscrepancyRandom(self):
    """Tests NormalizeSamples and Discrepancy with random samples.

    Generates 10 sets of 10 random samples, computes the discrepancy,
    relaxes the samples using Llloyd's algorithm in 1D, and computes the
    discrepancy of the relaxed samples. Discrepancy of the relaxed samples
    must be less than or equal to the discrepancy of the original samples.
    """
    random.seed(1234567)
    for _ in xrange(0, 10):
      samples = []
      num_samples = 10
      clock = 0.0
      samples.append(clock)
      for _ in xrange(1, num_samples):
        clock += random.random()
        samples.append(clock)
      samples = statistics.NormalizeSamples(samples)[0]
      d = statistics.Discrepancy(samples)

      relaxed_samples = Relax(samples)
      d_relaxed = statistics.Discrepancy(relaxed_samples)

      self.assertTrue(d_relaxed <= d)

  def testDiscrepancyAnalytic(self):
    """Computes discrepancy for sample sets with known statistics."""
    interval_multiplier = 100000

    samples = []
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertEquals(d, 1.0)

    samples = [0.5]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertEquals(round(d), 1.0)

    samples = [0.0, 1.0]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertAlmostEquals(round(d, 2), 1.0)

    samples = [0.5, 0.5, 0.5]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertAlmostEquals(d, 1.0)

    samples = [1.0/8.0, 3.0/8.0, 5.0/8.0, 7.0/8.0]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertAlmostEquals(round(d, 2), 0.25)

    samples = [0.0, 1.0/3.0, 2.0/3.0, 1.0]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertAlmostEquals(round(d, 2), 0.5)

    samples = statistics.NormalizeSamples(samples)[0]
    d = statistics.Discrepancy(samples, interval_multiplier)
    self.assertAlmostEquals(round(d, 2), 0.25)

    time_stamps_a = [0, 1, 2, 3, 5, 6]
    time_stamps_b = [0, 1, 2, 3, 5, 7]
    time_stamps_c = [0, 2, 3, 4]
    time_stamps_d = [0, 2, 3, 4, 5]
    d_abs_a = statistics.FrameDiscrepancy(time_stamps_a, True,
                                           interval_multiplier)
    d_abs_b = statistics.FrameDiscrepancy(time_stamps_b, True,
                                           interval_multiplier)
    d_abs_c = statistics.FrameDiscrepancy(time_stamps_c, True,
                                           interval_multiplier)
    d_abs_d = statistics.FrameDiscrepancy(time_stamps_d, True,
                                           interval_multiplier)
    d_rel_a = statistics.FrameDiscrepancy(time_stamps_a, False,
                                           interval_multiplier)
    d_rel_b = statistics.FrameDiscrepancy(time_stamps_b, False,
                                           interval_multiplier)
    d_rel_c = statistics.FrameDiscrepancy(time_stamps_c, False,
                                           interval_multiplier)
    d_rel_d = statistics.FrameDiscrepancy(time_stamps_d, False,
                                           interval_multiplier)

    self.assertTrue(d_abs_a < d_abs_b)
    self.assertTrue(d_rel_a < d_rel_b)
    self.assertTrue(d_rel_d < d_rel_c)
    self.assertEquals(round(d_abs_d, 2), round(d_abs_c, 2))

  def testPercentile(self):
    # The 50th percentile is the median value.
    self.assertEquals(3, statistics.Percentile([4, 5, 1, 3, 2], 50))
    self.assertEquals(2.5, statistics.Percentile([5, 1, 3, 2], 50))
    # When the list of values is empty, 0 is returned.
    self.assertEquals(0, statistics.Percentile([], 50))
    # When the given percentage is very low, the lowest value is given.
    self.assertEquals(1, statistics.Percentile([2, 1, 5, 4, 3], 5))
    # When the given percentage is very high, the highest value is given.
    self.assertEquals(5, statistics.Percentile([5, 2, 4, 1, 3], 95))
    # Linear interpolation between closest ranks is used. Using the example
    # from <http://en.wikipedia.org/wiki/Percentile>:
    self.assertEquals(27.5, statistics.Percentile([15, 20, 35, 40, 50], 40))

  def testArithmeticMean(self):
    # The ArithmeticMean function computes the simple average.
    self.assertAlmostEquals(40/3.0, statistics.ArithmeticMean([10, 10, 20], 3))
    self.assertAlmostEquals(15.0, statistics.ArithmeticMean([10, 20], 2))
    # Both lists of values or single values can be given for either argument.
    self.assertAlmostEquals(40/3.0, statistics.ArithmeticMean(40, [1, 1, 1]))
    # If the 'count' is zero, then zero is returned.
    self.assertEquals(0, statistics.ArithmeticMean(4.0, 0))
    self.assertEquals(0, statistics.ArithmeticMean(4.0, []))

