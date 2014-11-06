// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/network_portal_detector.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/net/network_portal_detector_impl.h"
#include "chrome/browser/chromeos/net/network_portal_detector_test_impl.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/chromeos_switches.h"

namespace chromeos {

namespace {

NetworkPortalDetector* g_network_portal_detector = NULL;
bool g_network_portal_detector_set_for_testing = false;

bool IsTestMode() {
  return CommandLine::ForCurrentProcess()->HasSwitch(::switches::kTestType);
}

// Stub implementation of NetworkPortalDetector.
class NetworkPortalDetectorStubImpl : public NetworkPortalDetector {
 protected:
  // NetworkPortalDetector implementation:
  virtual void AddObserver(Observer* /* observer */) OVERRIDE {}
  virtual void AddAndFireObserver(Observer* observer) OVERRIDE {
    if (observer)
      observer->OnPortalDetectionCompleted(NULL, CaptivePortalState());
  }
  virtual void RemoveObserver(Observer* /* observer */) OVERRIDE {}
  virtual CaptivePortalState GetCaptivePortalState(
      const NetworkState* /* network */) OVERRIDE {
    return CaptivePortalState();
  }
  virtual bool IsEnabled() OVERRIDE { return false; }
  virtual void Enable(bool /* start_detection */) OVERRIDE {}
  virtual bool StartDetectionIfIdle() OVERRIDE { return false; }
  virtual void EnableLazyDetection() OVERRIDE {}
  virtual void DisableLazyDetection() OVERRIDE {}
};

}  // namespace

void NetworkPortalDetector::InitializeForTesting(
    NetworkPortalDetector* network_portal_detector) {
  CHECK(!g_network_portal_detector)
      << "NetworkPortalDetector::InitializeForTesting() is called after "
      << "Initialize()";
  CHECK(network_portal_detector);
  g_network_portal_detector = network_portal_detector;
  g_network_portal_detector_set_for_testing = true;
}

// static
void NetworkPortalDetector::Initialize() {
  if (g_network_portal_detector_set_for_testing)
    return;
  CHECK(!g_network_portal_detector)
      << "NetworkPortalDetector::Initialize() is called twice";
  if (IsTestMode()) {
    g_network_portal_detector = new NetworkPortalDetectorStubImpl();
  } else {
    CHECK(g_browser_process);
    CHECK(g_browser_process->system_request_context());
    g_network_portal_detector = new NetworkPortalDetectorImpl(
        g_browser_process->system_request_context());
  }
}

// static
void NetworkPortalDetector::Shutdown() {
  CHECK(g_network_portal_detector || g_network_portal_detector_set_for_testing)
      << "NetworkPortalDetectorImpl::Shutdown() is called "
      << "without previous call to Initialize()";
  if (g_network_portal_detector) {
    delete g_network_portal_detector;
    g_network_portal_detector = NULL;
  }
}

// static
NetworkPortalDetector* NetworkPortalDetector::Get() {
  CHECK(g_network_portal_detector)
      << "NetworkPortalDetector::Get() called before Initialize()";
  return g_network_portal_detector;
}

}  // namespace chromeos
