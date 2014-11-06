// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.testshell.sync;

import android.app.Application;
import android.content.Context;

import org.chromium.chrome.browser.sync.ChromiumSyncAdapter;
import org.chromium.chrome.testshell.ChromiumTestShellApplication;

public class ChromiumTestShellSyncAdapter extends ChromiumSyncAdapter {
    public ChromiumTestShellSyncAdapter(Context appContext, Application application) {
        super(appContext, application);
    }

    @Override
    protected boolean useAsyncStartup() {
        return true;
    }

    @Override
    protected void initCommandLine() {
        ChromiumTestShellApplication.initCommandLine();
    }
}
