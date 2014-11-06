// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.testshell.sync;

import android.app.Application;
import android.content.Context;

import org.chromium.chrome.browser.sync.ChromiumSyncAdapter;
import org.chromium.chrome.browser.sync.ChromiumSyncAdapterService;

public class ChromiumTestShellSyncAdapterService extends ChromiumSyncAdapterService {
    @Override
    protected ChromiumSyncAdapter createChromiumSyncAdapter(
            Context context, Application application) {
        return new ChromiumTestShellSyncAdapter(context, getApplication());
    }
}
