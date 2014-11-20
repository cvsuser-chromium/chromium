// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_POLICY_TEST_UTILS_H_
#define CHROME_BROWSER_POLICY_POLICY_TEST_UTILS_H_

#include <ostream>

#include "chrome/browser/policy/policy_map.h"
#include "chrome/browser/policy/policy_service.h"
#include "chrome/browser/policy/policy_types.h"

namespace policy {

class PolicyBundle;
struct PolicyNamespace;

// Returns true if |service| is not serving any policies. Otherwise logs the
// current policies and returns false.
bool PolicyServiceIsEmpty(const PolicyService* service);

}  // namespace policy

std::ostream& operator<<(std::ostream& os, const policy::PolicyBundle& bundle);
std::ostream& operator<<(std::ostream& os, policy::PolicyScope scope);
std::ostream& operator<<(std::ostream& os, policy::PolicyLevel level);
std::ostream& operator<<(std::ostream& os, policy::PolicyDomain domain);
std::ostream& operator<<(std::ostream& os, const policy::PolicyMap& policies);
std::ostream& operator<<(std::ostream& os, const policy::PolicyMap::Entry& e);
std::ostream& operator<<(std::ostream& os, const policy::PolicyNamespace& ns);

#endif  // CHROME_BROWSER_POLICY_POLICY_TEST_UTILS_H_
