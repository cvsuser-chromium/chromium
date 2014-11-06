// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "chrome/browser/managed_mode/managed_mode_url_filter.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

class ManagedModeURLFilterTest : public ::testing::Test,
                                 public ManagedModeURLFilter::Observer {
 public:
  ManagedModeURLFilterTest() : filter_(new ManagedModeURLFilter) {
    filter_->SetDefaultFilteringBehavior(ManagedModeURLFilter::BLOCK);
    filter_->AddObserver(this);
  }

  virtual ~ManagedModeURLFilterTest() {
    filter_->RemoveObserver(this);
  }

  // ManagedModeURLFilter::Observer:
  virtual void OnSiteListUpdated() OVERRIDE {
    run_loop_.Quit();
  }

 protected:
  bool IsURLWhitelisted(const std::string& url) {
    return filter_->GetFilteringBehaviorForURL(GURL(url)) ==
           ManagedModeURLFilter::ALLOW;
  }

  base::MessageLoop message_loop_;
  base::RunLoop run_loop_;
  scoped_refptr<ManagedModeURLFilter> filter_;
};

TEST_F(ManagedModeURLFilterTest, Basic) {
  std::vector<std::string> list;
  // Allow domain and all subdomains, for any filtered scheme.
  list.push_back("google.com");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("http://google.com"));
  EXPECT_TRUE(IsURLWhitelisted("http://google.com/"));
  EXPECT_TRUE(IsURLWhitelisted("http://google.com/whatever"));
  EXPECT_TRUE(IsURLWhitelisted("https://google.com/"));
  EXPECT_FALSE(IsURLWhitelisted("http://notgoogle.com/"));
  EXPECT_TRUE(IsURLWhitelisted("http://mail.google.com"));
  EXPECT_TRUE(IsURLWhitelisted("http://x.mail.google.com"));
  EXPECT_TRUE(IsURLWhitelisted("https://x.mail.google.com/"));
  EXPECT_TRUE(IsURLWhitelisted("http://x.y.google.com/a/b"));
  EXPECT_FALSE(IsURLWhitelisted("http://youtube.com/"));
  EXPECT_TRUE(IsURLWhitelisted("bogus://youtube.com/"));
  EXPECT_TRUE(IsURLWhitelisted("chrome://youtube.com/"));
}

TEST_F(ManagedModeURLFilterTest, Inactive) {
  filter_->SetDefaultFilteringBehavior(ManagedModeURLFilter::ALLOW);

  std::vector<std::string> list;
  list.push_back("google.com");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  // If the filter is inactive, every URL should be whitelisted.
  EXPECT_TRUE(IsURLWhitelisted("http://google.com"));
  EXPECT_TRUE(IsURLWhitelisted("https://www.example.com"));
}

TEST_F(ManagedModeURLFilterTest, Scheme) {
  std::vector<std::string> list;
  // Filter only http, ftp and ws schemes.
  list.push_back("http://secure.com");
  list.push_back("ftp://secure.com");
  list.push_back("ws://secure.com");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("http://secure.com"));
  EXPECT_TRUE(IsURLWhitelisted("http://secure.com/whatever"));
  EXPECT_TRUE(IsURLWhitelisted("ftp://secure.com/"));
  EXPECT_TRUE(IsURLWhitelisted("ws://secure.com"));
  EXPECT_FALSE(IsURLWhitelisted("https://secure.com/"));
  EXPECT_FALSE(IsURLWhitelisted("wss://secure.com"));
  EXPECT_TRUE(IsURLWhitelisted("http://www.secure.com"));
  EXPECT_FALSE(IsURLWhitelisted("https://www.secure.com"));
  EXPECT_FALSE(IsURLWhitelisted("wss://www.secure.com"));
}

TEST_F(ManagedModeURLFilterTest, Path) {
  std::vector<std::string> list;
  // Filter only a certain path prefix.
  list.push_back("path.to/ruin");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("http://path.to/ruin"));
  EXPECT_TRUE(IsURLWhitelisted("https://path.to/ruin"));
  EXPECT_TRUE(IsURLWhitelisted("http://path.to/ruins"));
  EXPECT_TRUE(IsURLWhitelisted("http://path.to/ruin/signup"));
  EXPECT_TRUE(IsURLWhitelisted("http://www.path.to/ruin"));
  EXPECT_FALSE(IsURLWhitelisted("http://path.to/fortune"));
}

TEST_F(ManagedModeURLFilterTest, PathAndScheme) {
  std::vector<std::string> list;
  // Filter only a certain path prefix and scheme.
  list.push_back("https://s.aaa.com/path");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("https://s.aaa.com/path"));
  EXPECT_TRUE(IsURLWhitelisted("https://s.aaa.com/path/bbb"));
  EXPECT_FALSE(IsURLWhitelisted("http://s.aaa.com/path"));
  EXPECT_FALSE(IsURLWhitelisted("https://aaa.com/path"));
  EXPECT_FALSE(IsURLWhitelisted("https://x.aaa.com/path"));
  EXPECT_FALSE(IsURLWhitelisted("https://s.aaa.com/bbb"));
  EXPECT_FALSE(IsURLWhitelisted("https://s.aaa.com/"));
}

TEST_F(ManagedModeURLFilterTest, Host) {
  std::vector<std::string> list;
  // Filter only a certain hostname, without subdomains.
  list.push_back(".www.example.com");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("http://www.example.com"));
  EXPECT_FALSE(IsURLWhitelisted("http://example.com"));
  EXPECT_FALSE(IsURLWhitelisted("http://subdomain.example.com"));
}

TEST_F(ManagedModeURLFilterTest, IPAddress) {
  std::vector<std::string> list;
  // Filter an ip address.
  list.push_back("123.123.123.123");
  filter_->SetFromPatterns(list);
  run_loop_.Run();

  EXPECT_TRUE(IsURLWhitelisted("http://123.123.123.123/"));
  EXPECT_FALSE(IsURLWhitelisted("http://123.123.123.124/"));
}

TEST_F(ManagedModeURLFilterTest, Canonicalization) {
  // We assume that the hosts and URLs are already canonicalized.
  std::map<std::string, bool> hosts;
  hosts["www.moose.org"] = true;
  hosts["www.xn--n3h.net"] = true;
  std::map<GURL, bool> urls;
  urls[GURL("http://www.example.com/foo/")] = true;
  urls[GURL("http://www.example.com/%C3%85t%C3%B8mstr%C3%B6m")] = true;
  filter_->SetManualHosts(&hosts);
  filter_->SetManualURLs(&urls);

  // Base cases.
  EXPECT_TRUE(IsURLWhitelisted("http://www.example.com/foo/"));
  EXPECT_TRUE(IsURLWhitelisted(
      "http://www.example.com/%C3%85t%C3%B8mstr%C3%B6m"));

  // Verify that non-URI characters are escaped.
  EXPECT_TRUE(IsURLWhitelisted(
      "http://www.example.com/\xc3\x85t\xc3\xb8mstr\xc3\xb6m"));

  // Verify that unnecessary URI escapes are unescaped.
  EXPECT_TRUE(IsURLWhitelisted("http://www.example.com/%66%6F%6F/"));

  // Verify that the default port are removed.
  EXPECT_TRUE(IsURLWhitelisted("http://www.example.com:80/foo/"));

  // Verify that scheme and hostname are lowercased.
  EXPECT_TRUE(IsURLWhitelisted("htTp://wWw.eXamPle.com/foo/"));
  EXPECT_TRUE(IsURLWhitelisted("HttP://WwW.mOOsE.orG/blurp/"));

  // Verify that UTF-8 in hostnames are converted to punycode.
  EXPECT_TRUE(IsURLWhitelisted("http://www.\xe2\x98\x83\x0a.net/bla/"));

  // Verify that query and ref are stripped.
  EXPECT_TRUE(IsURLWhitelisted("http://www.example.com/foo/?bar=baz#ref"));
}

TEST_F(ManagedModeURLFilterTest, HasStandardScheme) {
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("http://example.com")));
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("https://example.com")));
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("ftp://example.com")));
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("gopher://example.com")));
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("ws://example.com")));
  EXPECT_TRUE(
      ManagedModeURLFilter::HasStandardScheme(GURL("wss://example.com")));
  EXPECT_FALSE(
      ManagedModeURLFilter::HasStandardScheme(GURL("wtf://example.com")));
}

TEST_F(ManagedModeURLFilterTest, HostMatchesPattern) {
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com",
                                               "*.google.com"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("google.com", "*.google.com"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("accounts.google.com",
                                               "*.google.com"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.de",
                                               "*.google.com"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("notgoogle.com",
                                               "*.google.com"));


  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com",
                                               "www.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.de",
                                               "www.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.co.uk",
                                               "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.blogspot.com",
                                               "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google", "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("google.com", "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("mail.google.com",
                                               "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.googleplex.com",
                                               "www.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.googleco.uk",
                                               "www.google.*"));


  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("google.com", "*.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("accounts.google.com",
                                               "*.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("mail.google.com",
                                               "*.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.de",
                                               "*.google.*"));
  EXPECT_TRUE(
      ManagedModeURLFilter::HostMatchesPattern("google.de",
                                               "*.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("google.blogspot.com",
                                               "*.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("google", "*.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("notgoogle.com", "*.google.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.googleplex.com",
                                               "*.google.*"));

  // Now test a few invalid patterns. They should never match.
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", ""));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "."));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", ".*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*."));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google..com", "*..*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*.*.com"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "www.*.*"));
  EXPECT_FALSE(ManagedModeURLFilter::HostMatchesPattern("www.google.com",
                                                        "*.goo.*le.*"));
  EXPECT_FALSE(
      ManagedModeURLFilter::HostMatchesPattern("www.google.com", "*google*"));
  EXPECT_FALSE(ManagedModeURLFilter::HostMatchesPattern("www.google.com",
                                                        "www.*.google.com"));
}

TEST_F(ManagedModeURLFilterTest, Patterns) {
  std::map<std::string, bool> hosts;

  // Initally, the second rule is ignored because has the same value as the
  // default (block). When we change the default to allow, the first rule is
  // ignored instead.
  hosts["*.google.com"] = true;
  hosts["www.google.*"] = false;

  hosts["accounts.google.com"] = false;
  hosts["mail.google.com"] = true;
  filter_->SetManualHosts(&hosts);

  // Initially, the default filtering behavior is BLOCK.
  EXPECT_TRUE(IsURLWhitelisted("http://www.google.com/foo/"));
  EXPECT_FALSE(IsURLWhitelisted("http://accounts.google.com/bar/"));
  EXPECT_FALSE(IsURLWhitelisted("http://www.google.co.uk/blurp/"));
  EXPECT_TRUE(IsURLWhitelisted("http://mail.google.com/moose/"));

  filter_->SetDefaultFilteringBehavior(ManagedModeURLFilter::ALLOW);
  EXPECT_FALSE(IsURLWhitelisted("http://www.google.com/foo/"));
  EXPECT_FALSE(IsURLWhitelisted("http://accounts.google.com/bar/"));
  EXPECT_FALSE(IsURLWhitelisted("http://www.google.co.uk/blurp/"));
  EXPECT_TRUE(IsURLWhitelisted("http://mail.google.com/moose/"));
}
