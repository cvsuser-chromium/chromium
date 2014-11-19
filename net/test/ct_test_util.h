// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CT_TEST_UTIL_H_
#define NET_CERT_CT_TEST_UTIL_H_

#include <string>

namespace net {

class X509Certificate;

namespace ct {

struct LogEntry;
struct SignedCertificateTimestamp;

// Fills |entry| with test data for an X.509 entry.
void GetX509CertLogEntry(LogEntry* entry);

// Fills |entry| with test data for a Precertificate entry.
void GetPrecertLogEntry(LogEntry* entry);

// Returns the binary representation of a test DigitallySigned
std::string GetTestDigitallySigned();

// Returns the binary representation of a test serialized SCT.
std::string GetTestSignedCertificateTimestamp();

}  // namespace ct

}  // namespace net

#endif  // NET_CERT_CT_TEST_UTIL_H_
