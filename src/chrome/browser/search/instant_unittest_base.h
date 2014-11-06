// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_INSTANT_UNITTEST_BASE_H_
#define CHROME_BROWSER_SEARCH_INSTANT_UNITTEST_BASE_H_

#include <string>

#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/test/base/browser_with_test_window_test.h"

class InstantServiceObserver;

// This class provides an extension on top of BrowserWithTestWindowTest, and
// adds some utility methods which can be useful for various unit tests for
// Embedded Search / Instant implementation classes.
class InstantUnitTestBase : public BrowserWithTestWindowTest {
 protected:
  virtual void SetUp() OVERRIDE;
  virtual void TearDown() OVERRIDE;

  // Adds and sets the default search provider using the base_url.
  // The base_url should have the http[s]:// prefix and a trailing / after the
  // TLD.
  // It will always use an instant-enabled configuration using a
  // search_terms_replacement_key.
  void SetDefaultSearchProvider(const std::string& base_url);

  // Simulates a Google Base URL change as would happen in event of
  // search-domain-check. Note that the GoogleURLTrackerFactory is disabled for
  // tests, so this is required.
  void NotifyGoogleBaseURLUpdate(const std::string& new_google_base_url);

  bool IsInstantServiceObserver(InstantServiceObserver* observer);

  InstantService* instant_service_;
  TemplateURLService* template_url_service_;
};

#endif  // CHROME_BROWSER_SEARCH_INSTANT_UNITTEST_BASE_H_
