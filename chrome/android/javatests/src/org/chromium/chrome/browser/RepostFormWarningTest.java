// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.app.AlertDialog;
import android.app.Dialog;
import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.base.test.util.EnormousTest;
import org.chromium.base.test.util.Feature;
import org.chromium.chrome.browser.RepostFormWarningDialog;
import org.chromium.chrome.test.util.TestHttpServerClient;
import org.chromium.chrome.testshell.ChromiumTestShellTestBase;
import org.chromium.chrome.testshell.TabShellTabUtils;
import org.chromium.chrome.testshell.TestShellTab;
import org.chromium.content.browser.test.util.TestCallbackHelperContainer;

import java.util.concurrent.TimeoutException;

/**
 * Integration tests verifying that form resubmission dialogs are correctly displayed and handled.
 */
public class RepostFormWarningTest extends ChromiumTestShellTestBase {
    // Active tab.
    private TestShellTab mTab;
    // Callback helper that manages waiting for pageloads to finish.
    private TestCallbackHelperContainer mCallbackHelper;

    @Override
    public void setUp() throws Exception {
        super.setUp();

        mTab = launchChromiumTestShellWithBlankPage().getActiveTab();
        mCallbackHelper = TabShellTabUtils.getTestCallbackHelperContainer(mTab);

        // Wait for the initial load of about://blank to finish.
        mCallbackHelper.getOnPageFinishedHelper().waitForCallback(0);
    }

    /** Verifies that the form resubmission warning is not displayed upon first POST navigation. */
    @MediumTest
    @Feature({"Navigation"})
    public void testFormFirstNavigation() throws Throwable {
        // Load the url posting data for the first time.
        postNavigation();
        mCallbackHelper.getOnPageFinishedHelper().waitForCallback(1);
        getInstrumentation().waitForIdleSync();

        // Verify that the form resubmission warning was not shown.
        assertNull("Form resubmission warning shown upon first load.",
                RepostFormWarningDialog.getCurrentDialog());
    }

    /** Verifies that confirming the form reload performs the reload. */
    @MediumTest
    @Feature({"Navigation"})
    public void testFormResubmissionContinue() throws Throwable {
        // Load the url posting data for the first time.
        postNavigation();
        mCallbackHelper.getOnPageFinishedHelper().waitForCallback(1);

        // Trigger a reload and wait for the warning to be displayed.
        reload();
        getInstrumentation().waitForIdleSync();
        AlertDialog dialog = (AlertDialog)RepostFormWarningDialog.getCurrentDialog();
        assertNotNull("Form resubmission warning not shown upon reload.", dialog);

        // Click "Continue" and verify that the page is reloaded.
        clickButton(dialog, AlertDialog.BUTTON_POSITIVE);
        mCallbackHelper.getOnPageFinishedHelper().waitForCallback(2);

        // Verify that the reference to the dialog in RepostFormWarningDialog was cleared.
        assertNull("Form resubmission warning dialog was not dismissed correctly.",
                RepostFormWarningDialog.getCurrentDialog());
    }

    /**
     * Verifies that cancelling the form reload prevents it from happening. Currently the test waits
     * after the "Cancel" button is clicked to verify that the load was not triggered, which blocks
     * for CallbackHelper's default timeout upon each execution.
     */
    @EnormousTest
    @Feature({"Navigation"})
    public void testFormResubmissionCancel() throws Throwable {
        // Load the url posting data for the first time.
        postNavigation();
        mCallbackHelper.getOnPageFinishedHelper().waitForCallback(1);

        // Trigger a reload and wait for the warning to be displayed.
        reload();
        getInstrumentation().waitForIdleSync();
        AlertDialog dialog = (AlertDialog)RepostFormWarningDialog.getCurrentDialog();
        assertNotNull("Form resubmission warning not shown upon reload.", dialog);

        // Click "Cancel" and verify that the page is not reloaded.
        clickButton(dialog, AlertDialog.BUTTON_NEGATIVE);
        boolean timedOut = false;
        try {
            mCallbackHelper.getOnPageFinishedHelper().waitForCallback(2);
        } catch (TimeoutException ex) {
            timedOut = true;
        }
        assertTrue("Page was reloaded despite selecting Cancel.", timedOut);

        // Verify that the reference to the dialog in RepostFormWarningDialog was cleared.
        assertNull("Form resubmission warning dialog was not dismissed correctly.",
                RepostFormWarningDialog.getCurrentDialog());
    }

    /** Performs a POST navigation in mTab. */
    private void postNavigation() throws Throwable {
        final String url = "chrome/test/data/empty.html";
        final byte[] postData = new byte[] { 42 };

        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mTab.loadUrlWithSanitization(TestHttpServerClient.getUrl(url), postData);
            }
        });
    }

    /** Reloads mTab. */
    private void reload() throws Throwable {
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                mTab.reload();
            }
        });
    }

    /** Clicks the given button in the given dialog. */
    private void clickButton(final AlertDialog dialog, final int buttonId) throws Throwable {
        runTestOnUiThread(new Runnable() {
            @Override
            public void run() {
                dialog.getButton(buttonId).performClick();
            }
        });
    }
}
