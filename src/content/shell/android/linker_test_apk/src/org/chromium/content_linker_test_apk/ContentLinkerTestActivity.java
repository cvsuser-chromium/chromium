// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_linker_test_apk;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;

import org.chromium.content.app.LibraryLoader;
import org.chromium.content.app.Linker;
import org.chromium.content.browser.BrowserStartupController;
import org.chromium.content.browser.ContentView;
import org.chromium.content.browser.ContentViewClient;
import org.chromium.content.common.CommandLine;
import org.chromium.content.common.ProcessInitException;
import org.chromium.content_shell.Shell;
import org.chromium.content_shell.ShellManager;
import org.chromium.ui.ActivityWindowAndroid;
import org.chromium.ui.WindowAndroid;

public class ContentLinkerTestActivity extends Activity {
    public static final String COMMAND_LINE_FILE =
            "/data/local/tmp/content-linker-test-command-line";

    private static final String TAG = "ContentLinkerTestActivity";

    public static final String COMMAND_LINE_ARGS_KEY = "commandLineArgs";

    // Use this on the command-line to simulate a low-memory device, otherwise
    // a regular device is simulated by this test, independently from what the
    // target device running the test really is.
    private static final String LOW_MEMORY_DEVICE = "--low-memory-device";

    private ShellManager mShellManager;
    private WindowAndroid mWindowAndroid;

    @Override
    public void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Initializing the command line must occur before loading the library.
        if (!CommandLine.isInitialized()) {
            CommandLine.initFromFile(COMMAND_LINE_FILE);
            String[] commandLineParams = getCommandLineParamsFromIntent(getIntent());
            if (commandLineParams != null) {
                CommandLine.getInstance().appendSwitchesAndArguments(commandLineParams);
            }
        }
        waitForDebuggerIfNeeded();

        // CommandLine.getInstance().hasSwitch() doesn't work here for some funky
        // reason, so parse the command-line differently here:
        boolean hasLowMemoryDeviceSwitch = false;
        String[] cmdline = CommandLine.getJavaSwitchesOrNull();
        if (cmdline == null)
            Log.i(TAG, "Command line is null");
        else {
            Log.i(TAG, "Command line is:");
            for (int n = 0; n < cmdline.length; ++n) {
                Log.i(TAG, "  '" + cmdline[n] + "'");
                if (cmdline[n].equals(LOW_MEMORY_DEVICE))
                    hasLowMemoryDeviceSwitch = true;
            }
        }

        // Determine which kind of device to simulate from the command-line.
        int memoryDeviceConfig = Linker.MEMORY_DEVICE_CONFIG_NORMAL;
        if (hasLowMemoryDeviceSwitch)
            memoryDeviceConfig = Linker.MEMORY_DEVICE_CONFIG_LOW;
        Linker.setMemoryDeviceConfig(memoryDeviceConfig);

        // Register the test runner class by name.
        Linker.setTestRunnerClassName(LinkerTests.class.getName());

        // Load the library in the browser process, this will also run the test
        // runner in this process.
        try {
            LibraryLoader.ensureInitialized();
        } catch (ProcessInitException e) {
            Log.i(TAG, "Cannot load content_linker_test:" +  e);
        }

        // Now, start a new renderer process by creating a new view.
        // This will run the test runner in the renderer process.

        BrowserStartupController.get(getApplicationContext()).initChromiumBrowserProcessForTests();

        LayoutInflater inflater =
                (LayoutInflater) getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        View view = inflater.inflate(R.layout.test_activity, null);
        mShellManager = (ShellManager) view.findViewById(R.id.shell_container);
        mWindowAndroid = new ActivityWindowAndroid(this);
        mShellManager.setWindow(mWindowAndroid);

        mShellManager.setStartupUrl("about:blank");

        BrowserStartupController.get(this).startBrowserProcessesAsync(
                new BrowserStartupController.StartupCallback() {
            @Override
            public void onSuccess(boolean alreadyStarted) {
                finishInitialization(savedInstanceState);
            }

            @Override
            public void onFailure() {
                initializationFailed();
            }
        });

        // TODO(digit): Ensure that after the content view is initialized,
        // the program finishes().
    }

    private void finishInitialization(Bundle savedInstanceState) {
        String shellUrl = ShellManager.DEFAULT_SHELL_URL;
        mShellManager.launchShell(shellUrl);
        getActiveContentView().setContentViewClient(new ContentViewClient());
    }

    private void initializationFailed() {
        Log.e(TAG, "ContentView initialization failed.");
        finish();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        mWindowAndroid.saveInstanceState(outState);
    }

    private void waitForDebuggerIfNeeded() {
        if (CommandLine.getInstance().hasSwitch(CommandLine.WAIT_FOR_JAVA_DEBUGGER)) {
            Log.e(TAG, "Waiting for Java debugger to connect...");
            android.os.Debug.waitForDebugger();
            Log.e(TAG, "Java debugger connected. Resuming execution.");
        }
    }

    @Override
    protected void onStop() {
        super.onStop();

        ContentView view = getActiveContentView();
        if (view != null) view.onHide();
    }

    @Override
    protected void onStart() {
        super.onStart();

        ContentView view = getActiveContentView();
        if (view != null) view.onShow();
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        mWindowAndroid.onActivityResult(requestCode, resultCode, data);
    }

    private static String getUrlFromIntent(Intent intent) {
        return intent != null ? intent.getDataString() : null;
    }

    private static String[] getCommandLineParamsFromIntent(Intent intent) {
        return intent != null ? intent.getStringArrayExtra(COMMAND_LINE_ARGS_KEY) : null;
    }

    /**
     * @return The {@link ContentView} owned by the currently visible {@link Shell} or null if one
     *         is not showing.
     */
    public ContentView getActiveContentView() {
        if (mShellManager == null)
            return null;

        Shell shell = mShellManager.getActiveShell();
        if (shell == null)
            return null;

        return shell.getContentView();
    }
}
