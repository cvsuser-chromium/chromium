// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/ui/browser_dialogs.h"

namespace chrome {

void ShowAboutIPCDialog() {
  // TODO(beng):
  NOTIMPLEMENTED();
}

}  // namespace chrome

#if !defined(OS_CHROMEOS) && !defined(OS_WIN)
// static
void ExternalProtocolHandler::RunExternalProtocolDialog(
    const GURL& url,
    int render_process_host_id,
    int routing_id) {
}
#endif
