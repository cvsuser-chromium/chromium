// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/prefs/pref_value_map.h"
#include "chrome/browser/extensions/external_policy_loader.h"
#include "chrome/browser/extensions/policy_handlers.h"
#include "chrome/browser/policy/policy_error_map.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/common/pref_names.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

const char kTestPref[] = "unit_test.test_pref";

TEST(ExtensionListPolicyHandlerTest, CheckPolicySettings) {
  base::ListValue list;
  policy::PolicyMap policy_map;
  policy::PolicyErrorMap errors;
  ExtensionListPolicyHandler handler(
      policy::key::kExtensionInstallBlacklist, kTestPref, true);

  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("abcdefghijklmnopabcdefghijklmnop"));
  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("*"));
  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("invalid"));
  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_FALSE(errors.empty());
  EXPECT_FALSE(
      errors.GetErrors(policy::key::kExtensionInstallBlacklist).empty());
}

TEST(ExtensionListPolicyHandlerTest, ApplyPolicySettings) {
  base::ListValue policy;
  base::ListValue expected;
  policy::PolicyMap policy_map;
  PrefValueMap prefs;
  base::Value* value = NULL;
  ExtensionListPolicyHandler handler(
      policy::key::kExtensionInstallBlacklist, kTestPref, false);

  policy.Append(Value::CreateStringValue("abcdefghijklmnopabcdefghijklmnop"));
  expected.Append(Value::CreateStringValue("abcdefghijklmnopabcdefghijklmnop"));

  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 policy.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_TRUE(prefs.GetValue(kTestPref, &value));
  EXPECT_TRUE(base::Value::Equals(&expected, value));

  policy.Append(Value::CreateStringValue("invalid"));
  policy_map.Set(policy::key::kExtensionInstallBlacklist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 policy.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_TRUE(prefs.GetValue(kTestPref, &value));
  EXPECT_TRUE(base::Value::Equals(&expected, value));
}

TEST(ExtensionInstallForcelistPolicyHandlerTest, CheckPolicySettings) {
  base::ListValue list;
  policy::PolicyMap policy_map;
  policy::PolicyErrorMap errors;
  ExtensionInstallForcelistPolicyHandler handler;

  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.AppendString("abcdefghijklmnopabcdefghijklmnop;http://example.com");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  // Add an erroneous entry. This should generate an error, but the good
  // entry should still be translated successfully.
  list.AppendString("adfasdf;http://example.com");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_EQ(1U, errors.size());

  // Add an entry with bad URL, which should generate another error.
  list.AppendString("abcdefghijklmnopabcdefghijklmnop;nourl");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_EQ(2U, errors.size());

  // Just an extension ID should also generate an error.
  list.AppendString("abcdefghijklmnopabcdefghijklmnop");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_EQ(3U, errors.size());
}

TEST(ExtensionInstallForcelistPolicyHandlerTest, ApplyPolicySettings) {
  base::ListValue policy;
  base::DictionaryValue expected;
  policy::PolicyMap policy_map;
  PrefValueMap prefs;
  base::Value* value = NULL;
  ExtensionInstallForcelistPolicyHandler handler;

  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_FALSE(prefs.GetValue(prefs::kExtensionInstallForceList, &value));
  EXPECT_FALSE(value);

  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 policy.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_TRUE(prefs.GetValue(prefs::kExtensionInstallForceList, &value));
  EXPECT_TRUE(base::Value::Equals(&expected, value));

  policy.AppendString("abcdefghijklmnopabcdefghijklmnop;http://example.com");
  extensions::ExternalPolicyLoader::AddExtension(
      &expected, "abcdefghijklmnopabcdefghijklmnop", "http://example.com");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 policy.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_TRUE(prefs.GetValue(prefs::kExtensionInstallForceList, &value));
  EXPECT_TRUE(base::Value::Equals(&expected, value));

  policy.AppendString("invalid");
  policy_map.Set(policy::key::kExtensionInstallForcelist,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 policy.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  EXPECT_TRUE(prefs.GetValue(prefs::kExtensionInstallForceList, &value));
  EXPECT_TRUE(base::Value::Equals(&expected, value));
}

TEST(ExtensionURLPatternListPolicyHandlerTest, CheckPolicySettings) {
  base::ListValue list;
  policy::PolicyMap policy_map;
  policy::PolicyErrorMap errors;
  ExtensionURLPatternListPolicyHandler handler(
      policy::key::kExtensionInstallSources, kTestPref);

  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("http://*.google.com/*"));
  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("<all_urls>"));
  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_TRUE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_TRUE(errors.empty());

  list.Append(Value::CreateStringValue("invalid"));
  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_FALSE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_FALSE(errors.empty());
  EXPECT_FALSE(errors.GetErrors(policy::key::kExtensionInstallSources).empty());

  // URLPattern syntax has a different way to express 'all urls'. Though '*'
  // would be compatible today, it would be brittle, so we disallow.
  list.Append(Value::CreateStringValue("*"));
  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  errors.Clear();
  EXPECT_FALSE(handler.CheckPolicySettings(policy_map, &errors));
  EXPECT_FALSE(errors.empty());
  EXPECT_FALSE(errors.GetErrors(policy::key::kExtensionInstallSources).empty());
}

TEST(ExtensionURLPatternListPolicyHandlerTest, ApplyPolicySettings) {
  base::ListValue list;
  policy::PolicyMap policy_map;
  PrefValueMap prefs;
  base::Value* value = NULL;
  ExtensionURLPatternListPolicyHandler handler(
      policy::key::kExtensionInstallSources, kTestPref);

  list.Append(Value::CreateStringValue("https://corp.monkey.net/*"));
  policy_map.Set(policy::key::kExtensionInstallSources,
                 policy::POLICY_LEVEL_MANDATORY,
                 policy::POLICY_SCOPE_USER,
                 list.DeepCopy(),
                 NULL);
  handler.ApplyPolicySettings(policy_map, &prefs);
  ASSERT_TRUE(prefs.GetValue(kTestPref, &value));
  EXPECT_TRUE(base::Value::Equals(&list, value));
}

}  // namespace extensions
