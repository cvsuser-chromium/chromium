// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/translate/translate_script.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/chrome_switches.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/url_request/test_url_fetcher_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

class TranslateScriptTest : public testing::Test {
 public:
  TranslateScriptTest() : testing::Test() {}

 protected:
  virtual void SetUp() {
    script_.reset(new TranslateScript);
    DCHECK(script_.get());
  }

  virtual void TearDown() {
    script_.reset();
  }

  void Request() {
    script_->Request(
        base::Bind(&TranslateScriptTest::OnComplete, base::Unretained(this)));
  }

  net::TestURLFetcher* GetTestURLFetcher() {
    return url_fetcher_factory_.GetFetcherByID(TranslateScript::kFetcherId);
  }

 private:
  void OnComplete(bool success, const std::string& script) {
  }

  scoped_ptr<TranslateScript> script_;
  net::TestURLFetcherFactory url_fetcher_factory_;

  DISALLOW_COPY_AND_ASSIGN(TranslateScriptTest);
};

TEST_F(TranslateScriptTest, CheckScriptParameters) {
  Request();
  net::TestURLFetcher* fetcher = GetTestURLFetcher();
  ASSERT_TRUE(fetcher);

  GURL expected_url(TranslateScript::kScriptURL);
  GURL url = fetcher->GetOriginalURL();
  EXPECT_TRUE(url.is_valid());
  EXPECT_EQ(expected_url.GetOrigin().spec(), url.GetOrigin().spec());
  EXPECT_EQ(expected_url.path(), url.path());

  int load_flags = fetcher->GetLoadFlags();
  EXPECT_EQ(net::LOAD_DO_NOT_SEND_COOKIES,
            load_flags & net::LOAD_DO_NOT_SEND_COOKIES);
  EXPECT_EQ(net::LOAD_DO_NOT_SAVE_COOKIES,
            load_flags & net::LOAD_DO_NOT_SAVE_COOKIES);

  std::string expected_extra_headers =
      base::StringPrintf("%s\r\n\r\n", TranslateScript::kRequestHeader);
  net::HttpRequestHeaders extra_headers;
  fetcher->GetExtraRequestHeaders(&extra_headers);
  EXPECT_EQ(expected_extra_headers, extra_headers.ToString());

  std::string always_use_ssl;
  net::GetValueForKeyInQuery(
      url, TranslateScript::kAlwaysUseSslQueryName, &always_use_ssl);
  EXPECT_EQ(std::string(TranslateScript::kAlwaysUseSslQueryValue),
            always_use_ssl);

  std::string callback;
  net::GetValueForKeyInQuery(
      url, TranslateScript::kCallbackQueryName, &callback);
  EXPECT_EQ(std::string(TranslateScript::kCallbackQueryValue), callback);

  std::string css_loader_callback;
  net::GetValueForKeyInQuery(
      url, TranslateScript::kCssLoaderCallbackQueryName, &css_loader_callback);
  EXPECT_EQ(std::string(TranslateScript::kCssLoaderCallbackQueryValue),
            css_loader_callback);

  std::string javascript_loader_callback;
  net::GetValueForKeyInQuery(
      url,
      TranslateScript::kJavascriptLoaderCallbackQueryName,
      &javascript_loader_callback);
  EXPECT_EQ(std::string(TranslateScript::kJavascriptLoaderCallbackQueryValue),
            javascript_loader_callback);
}

TEST_F(TranslateScriptTest, CheckScriptURL) {
  const std::string script_url("http://www.tamurayukari.com/mero-n.js");
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  command_line->AppendSwitchASCII(switches::kTranslateScriptURL,
                                  script_url);

  Request();
  net::TestURLFetcher* fetcher = GetTestURLFetcher();
  ASSERT_TRUE(fetcher);

  GURL expected_url(script_url);
  GURL url = fetcher->GetOriginalURL();
  EXPECT_TRUE(url.is_valid());
  EXPECT_EQ(expected_url.GetOrigin().spec(), url.GetOrigin().spec());
  EXPECT_EQ(expected_url.path(), url.path());
}
