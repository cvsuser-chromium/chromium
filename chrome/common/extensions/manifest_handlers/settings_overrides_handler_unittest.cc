// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/manifest_handlers/settings_overrides_handler.h"

#include "base/json/json_string_value_serializer.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/features/feature_channel.h"
#include "chrome/common/extensions/manifest_url_handler.h"
#include "extensions/common/manifest_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kManifest[] = "{"
    " \"version\" : \"1.0.0.0\","
    " \"name\" : \"Test\","
    " \"chrome_settings_overrides\" : {"
    "   \"homepage\" : \"http://www.homepage.com\","
    "   \"search_provider\" : {"
    "        \"name\" : \"first\","
    "        \"keyword\" : \"firstkey\","
    "        \"search_url\" : \"http://www.foo.com/s?q={searchTerms}\","
    "        \"favicon_url\" : \"http://www.foo.com/favicon.ico\","
    "        \"suggest_url\" : \"http://www.foo.com/s?q={searchTerms}\","
    "        \"encoding\" : \"UTF-8\","
    "        \"is_default\" : true"
    "    },"
    "   \"startup_pages\" : [\"http://www.startup.com\"]"
    "  }"
    "}";

using extensions::api::manifest_types::ChromeSettingsOverrides;
using extensions::Extension;
using extensions::Manifest;
using extensions::SettingsOverrides;
namespace manifest_keys = extensions::manifest_keys;

class DeclarativeSettingsTest : public testing::Test {
};


TEST_F(DeclarativeSettingsTest, ParseManifest) {
  extensions::ScopedCurrentChannel channel(chrome::VersionInfo::CHANNEL_DEV);
  std::string manifest(kManifest);
  JSONStringValueSerializer json(&manifest);
  std::string error;
  scoped_ptr<base::Value> root(json.Deserialize(NULL, &error));
  ASSERT_TRUE(root);
  ASSERT_TRUE(root->IsType(base::Value::TYPE_DICTIONARY));
  scoped_refptr<Extension> extension = Extension::Create(
      base::FilePath(FILE_PATH_LITERAL("//nonexistent")),
      Manifest::INVALID_LOCATION,
      *static_cast<base::DictionaryValue*>(root.get()),
      Extension::NO_FLAGS,
      &error);
  ASSERT_TRUE(extension);
  ASSERT_TRUE(extension->manifest()->HasPath(manifest_keys::kSettingsOverride));

  SettingsOverrides* settings_override = static_cast<SettingsOverrides*>(
        extension->GetManifestData(manifest_keys::kSettingsOverride));
  ASSERT_TRUE(settings_override);
  ASSERT_TRUE(settings_override->search_engine);
  EXPECT_TRUE(settings_override->search_engine->is_default);
  const ChromeSettingsOverrides::Search_provider* search_engine =
      settings_override->search_engine.get();
  EXPECT_EQ("first", search_engine->name);
  EXPECT_EQ("firstkey", search_engine->keyword);
  EXPECT_EQ("http://www.foo.com/s?q={searchTerms}", search_engine->search_url);
  EXPECT_EQ("http://www.foo.com/favicon.ico", search_engine->favicon_url);
  EXPECT_EQ("http://www.foo.com/s?q={searchTerms}",
            *search_engine->suggest_url);
  EXPECT_EQ("UTF-8", search_engine->encoding);

  EXPECT_EQ(std::vector<GURL>(1, GURL("http://www.startup.com")),
            settings_override->startup_pages);

  ASSERT_TRUE(settings_override->homepage);
  EXPECT_EQ(GURL("http://www.homepage.com"), *settings_override->homepage);
}

}  // namespace
