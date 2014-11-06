// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/url_blacklist_policy_handler.h"

#include "base/memory/scoped_ptr.h"
#include "base/prefs/pref_value_map.h"
#include "base/values.h"
#include "chrome/browser/policy/policy_error_map.h"
#include "chrome/browser/policy/policy_map.h"
#include "chrome/common/pref_names.h"
#include "grit/generated_resources.h"
#include "policy/policy_constants.h"

namespace policy {

URLBlacklistPolicyHandler::URLBlacklistPolicyHandler() {}

URLBlacklistPolicyHandler::~URLBlacklistPolicyHandler() {}

bool URLBlacklistPolicyHandler::CheckPolicySettings(const PolicyMap& policies,
                                                    PolicyErrorMap* errors) {
  const base::Value* disabled_schemes =
      policies.GetValue(key::kDisabledSchemes);
  const base::Value* url_blacklist = policies.GetValue(key::kURLBlacklist);

  if (disabled_schemes && !disabled_schemes->IsType(base::Value::TYPE_LIST)) {
    errors->AddError(key::kDisabledSchemes,
                     IDS_POLICY_TYPE_ERROR,
                     ValueTypeToString(base::Value::TYPE_LIST));
  }

  if (url_blacklist && !url_blacklist->IsType(base::Value::TYPE_LIST)) {
    errors->AddError(key::kURLBlacklist,
                     IDS_POLICY_TYPE_ERROR,
                     ValueTypeToString(base::Value::TYPE_LIST));
  }

  return true;
}

void URLBlacklistPolicyHandler::ApplyPolicySettings(const PolicyMap& policies,
                                                    PrefValueMap* prefs) {
  const base::Value* url_blacklist_policy =
      policies.GetValue(key::kURLBlacklist);
  const base::ListValue* url_blacklist = NULL;
  if (url_blacklist_policy)
    url_blacklist_policy->GetAsList(&url_blacklist);
  const base::Value* disabled_schemes_policy =
      policies.GetValue(key::kDisabledSchemes);
  const base::ListValue* disabled_schemes = NULL;
  if (disabled_schemes_policy)
    disabled_schemes_policy->GetAsList(&disabled_schemes);

  scoped_ptr<base::ListValue> merged_url_blacklist(new base::ListValue());

  // We start with the DisabledSchemes because we have size limit when
  // handling URLBlacklists.
  if (disabled_schemes) {
    for (base::ListValue::const_iterator entry(disabled_schemes->begin());
         entry != disabled_schemes->end(); ++entry) {
      std::string entry_value;
      if ((*entry)->GetAsString(&entry_value)) {
        entry_value.append("://*");
        merged_url_blacklist->AppendString(entry_value);
      }
    }
  }

  if (url_blacklist) {
    for (base::ListValue::const_iterator entry(url_blacklist->begin());
         entry != url_blacklist->end(); ++entry) {
      if ((*entry)->IsType(Value::TYPE_STRING))
        merged_url_blacklist->Append((*entry)->DeepCopy());
    }
  }

  if (disabled_schemes || url_blacklist)
    prefs->SetValue(prefs::kUrlBlacklist, merged_url_blacklist.release());
}

}  // namespace policy
