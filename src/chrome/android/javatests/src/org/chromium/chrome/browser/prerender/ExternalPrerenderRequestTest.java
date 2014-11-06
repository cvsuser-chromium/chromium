// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.prerender;

import android.test.UiThreadTest;
import android.test.suitebuilder.annotation.MediumTest;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.ContentViewUtil;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.chrome.testshell.ChromiumTestShellTestBase;

import java.util.concurrent.Callable;

/**
 * Tests for adding and removing prerenders using the {@link ExternalPrerenderHandler}
 */
public class ExternalPrerenderRequestTest extends ChromiumTestShellTestBase {
    private static final String GOOGLE_URL =
            TestHttpServerClient.getUrl("chrome/test/data/android/prerender/google.html");
    private static final String YOUTUBE_URL =
            TestHttpServerClient.getUrl("chrome/test/data/android/prerender/youtube.html");
    private static final int PRERENDER_DELAY_MS = 500;

    private ExternalPrerenderHandler mHandler;
    private Profile mProfile;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        clearAppData();
        launchChromiumTestShellWithBlankPage();
        assertTrue(waitForActiveShellToBeDoneLoading());
        mHandler = new ExternalPrerenderHandler();
        final Callable<Profile> profileCallable = new Callable<Profile>() {
            @Override
            public Profile call() throws Exception {
                return Profile.getLastUsedProfile();
            }
        };
        mProfile = ThreadUtils.runOnUiThreadBlocking(profileCallable);
    }

    @MediumTest
    @UiThreadTest
    @Feature({"Prerender"})
    /**
     * Test adding a prerender and canceling that to add a new one.
     */
    public void testAddPrerenderAndCancel() throws InterruptedException {
        int webContentsPtr = mHandler.addPrerender(mProfile, GOOGLE_URL, "", 0, 0);
        assertTrue(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, GOOGLE_URL, webContentsPtr));

        mHandler.cancelCurrentPrerender();
        assertFalse(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, GOOGLE_URL, webContentsPtr));
        ContentViewUtil.destroyNativeWebContents(webContentsPtr);
        Thread.sleep(PRERENDER_DELAY_MS);
        webContentsPtr = mHandler.addPrerender(mProfile, YOUTUBE_URL, "", 0, 0);
        assertTrue(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, YOUTUBE_URL, webContentsPtr));

    }

    @SmallTest
    @UiThreadTest
    @Feature({"Prerender"})
    /**
     * Test calling cancel without any added prerenders.
     */
    public void testCancelPrerender() {
        mHandler.cancelCurrentPrerender();
        int webContentsPtr = mHandler.addPrerender(mProfile, GOOGLE_URL, "", 0, 0);
        assertTrue(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, GOOGLE_URL, webContentsPtr));
    }

    @MediumTest
    @UiThreadTest
    @Feature({"Prerender"})
    /**
     * Test adding two prerenders without canceling the first one.
     */
    public void testAddingPrerendersInaRow() throws InterruptedException {
        int webContentsPtr = mHandler.addPrerender(mProfile, GOOGLE_URL, "", 0, 0);
        assertTrue(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, GOOGLE_URL, webContentsPtr));
        Thread.sleep(PRERENDER_DELAY_MS);
        int newWebContentsPtr = mHandler.addPrerender(mProfile, YOUTUBE_URL, "", 0, 0);
        assertTrue(ExternalPrerenderHandler.hasPrerenderedUrl(
                mProfile, YOUTUBE_URL, newWebContentsPtr));
    }
}
