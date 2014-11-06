# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A collection of statistical utility functions to be used by metrics."""

import bisect
import math


def Clamp(value, low=0.0, high=1.0):
  """Clamp a value between some low and high value."""
  return min(max(value, low), high)


def NormalizeSamples(samples):
  """Sorts the samples, and map them linearly to the range [0,1].

  They're mapped such that for the N samples, the first sample is 0.5/N and the
  last sample is (N-0.5)/N.

  Background: The discrepancy of the sample set i/(N-1); i=0, ..., N-1 is 2/N,
  twice the discrepancy of the sample set (i+1/2)/N; i=0, ..., N-1. In our case
  we don't want to distinguish between these two cases, as our original domain
  is not bounded (it is for Monte Carlo integration, where discrepancy was
  first used).
  """
  if not samples:
    return samples, 1.0
  samples = sorted(samples)
  low = min(samples)
  high = max(samples)
  new_low = 0.5 / len(samples)
  new_high = (len(samples)-0.5) / len(samples)
  if high-low == 0.0:
    return samples, 1.0
  scale = (new_high - new_low) / (high - low)
  for i in xrange(0, len(samples)):
    samples[i] = float(samples[i] - low) * scale + new_low
  return samples, scale


def Discrepancy(samples, interval_multiplier=10000):
  """Computes the discrepancy of a set of 1D samples from the interval [0,1].

  The samples must be sorted.

  http://en.wikipedia.org/wiki/Low-discrepancy_sequence
  http://mathworld.wolfram.com/Discrepancy.html
  """
  if not samples:
    return 1.0

  max_local_discrepancy = 0
  locations = []
  # For each location, stores the number of samples less than that location.
  left = []
  # For each location, stores the number of samples less than or equal to that
  # location.
  right = []

  interval_count = len(samples) * interval_multiplier
  # Compute number of locations the will roughly result in the requested number
  # of intervals.
  location_count = int(math.ceil(math.sqrt(interval_count*2)))
  inv_sample_count = 1.0 / len(samples)

  # Generate list of equally spaced locations.
  for i in xrange(0, location_count):
    location = float(i) / (location_count-1)
    locations.append(location)
    left.append(bisect.bisect_left(samples, location))
    right.append(bisect.bisect_right(samples, location))

  # Iterate over the intervals defined by any pair of locations.
  for i in xrange(0, len(locations)):
    for j in xrange(i, len(locations)):
      # Compute length of interval and number of samples in the interval.
      length = locations[j] - locations[i]
      count = right[j] - left[i]

      # Compute local discrepancy and update max_local_discrepancy.
      local_discrepancy = abs(float(count)*inv_sample_count - length)
      max_local_discrepancy = max(local_discrepancy, max_local_discrepancy)

  return max_local_discrepancy


def FrameDiscrepancy(frame_timestamps, absolute=True,
                     interval_multiplier=10000):
  """A discrepancy based metric for measuring jank.

  FrameDiscrepancy quantifies the largest area of jank observed in a series
  of timestamps.  Note that this is different form metrics based on the
  max_frame_time. For example, the time stamp series A = [0,1,2,3,5,6] and
  B = [0,1,2,3,5,7] have the same max_frame_time = 2, but
  Discrepancy(B) > Discrepancy(A).

  Two variants of discrepancy can be computed:

  Relative discrepancy is following the original definition of
  discrepancy. It characterized the largest area of jank, relative to the
  duration of the entire time stamp series.  We normalize the raw results,
  because the best case discrepancy for a set of N samples is 1/N (for
  equally spaced samples), and we want our metric to report 0.0 in that
  case.

  Absolute discrepancy also characterizes the largest area of jank, but its
  value wouldn't change (except for imprecisions due to a low
  interval_multiplier) if additional 'good' frames were added to an
  exisiting list of time stamps.  Its range is [0,inf] and the unit is
  milliseconds.

  The time stamp series C = [0,2,3,4] and D = [0,2,3,4,5] have the same
  absolute discrepancy, but D has lower relative discrepancy than C.
  """
  if not frame_timestamps:
    return 1.0
  samples, sample_scale = NormalizeSamples(frame_timestamps)
  discrepancy = Discrepancy(samples, interval_multiplier)
  inv_sample_count = 1.0 / len(samples)
  if absolute:
    # Compute absolute discrepancy
    discrepancy /= sample_scale
  else:
    # Compute relative discrepancy
    discrepancy = Clamp((discrepancy-inv_sample_count) / (1.0-inv_sample_count))
  return discrepancy


def ArithmeticMean(numerator, denominator):
  """Calculates arithmetic mean.

  Both numerator and denominator can be given as either individual
  values or lists of values which will be summed.

  Args:
    numerator: A quantity that represents a sum total value.
    denominator: A quantity that represents a count of the number of things.

  Returns:
    The arithmetic mean value, or 0 if the denominator value was 0.
  """
  numerator_total = Total(numerator)
  denominator_total = Total(denominator)
  return DivideIfPossibleOrZero(numerator_total, denominator_total)


def Total(data):
  """Returns the float value of a number or the sum of a list."""
  if type(data) == float:
    total = data
  elif type(data) == int:
    total = float(data)
  elif type(data) == list:
    total = float(sum(data))
  else:
    raise TypeError
  return total


def DivideIfPossibleOrZero(numerator, denominator):
  """Returns the quotient, or zero if the denominator is zero."""
  if not denominator:
    return 0.0
  else:
    return numerator / denominator


def GeneralizedMean(values, exponent):
  """See http://en.wikipedia.org/wiki/Generalized_mean"""
  if not values:
    return 0.0
  sum_of_powers = 0.0
  for v in values:
    sum_of_powers += v ** exponent
  return (sum_of_powers / len(values)) ** (1.0/exponent)


def Median(values):
  """Gets the median of a list of values."""
  return Percentile(values, 50)


def Percentile(values, percentile):
  """Calculates the value below which a given percentage of values fall.

  For example, if 17% of the values are less than 5.0, then 5.0 is the 17th
  percentile for this set of values. When the percentage doesn't exactly
  match a rank in the list of values, the percentile is computed using linear
  interpolation between closest ranks.

  Args:
    values: A list of numerical values.
    percentile: A number between 0 and 100.

  Returns:
    The Nth percentile for the list of values, where N is the given percentage.
  """
  if not values:
    return 0.0
  sorted_values = sorted(values)
  n = len(values)
  percentile /= 100.0
  if percentile <= 0.5 / n:
    return sorted_values[0]
  elif percentile >= (n - 0.5) / n:
    return sorted_values[-1]
  else:
    floor_index = int(math.floor(n * percentile -  0.5))
    floor_value = sorted_values[floor_index]
    ceil_value = sorted_values[floor_index+1]
    alpha = n * percentile - 0.5 - floor_index
    return floor_value + alpha * (ceil_value - floor_value)


def GeometricMean(values):
  """Compute a rounded geometric mean from an array of values."""
  if not values:
    return None
  # To avoid infinite value errors, make sure no value is less than 0.001.
  new_values = []
  for value in values:
    if value > 0.001:
      new_values.append(value)
    else:
      new_values.append(0.001)
  # Compute the sum of the log of the values.
  log_sum = sum(map(math.log, new_values))
  # Raise e to that sum over the number of values.
  mean = math.pow(math.e, (log_sum / len(new_values)))
  # Return the rounded mean.
  return int(round(mean))

