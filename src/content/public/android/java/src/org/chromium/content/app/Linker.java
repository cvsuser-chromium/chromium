// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.app;

import java.io.File;
import java.io.FileInputStream;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;

import android.content.Context;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import org.chromium.base.SysUtils;

/*
 * Technical note:
 *
 * The point of this class is to provide an alternative to System.loadLibrary()
 * to load native shared libraries. One specific feature that it supports is the
 * ability to save RAM by sharing the ELF RELRO sections between renderer
 * processes.
 *
 * When two processes load the same native library at the _same_ memory address,
 * the content of their RELRO section (which includes C++ vtables or any
 * constants that contain pointers) will be largely identical [1].
 *
 * By default, the RELRO section is backed by private RAM in each process,
 * which is still significant on mobile (e.g. 1.28 MB / process on Chrome 30 for
 * Android).
 *
 * However, it is possible to save RAM by creating a shared memory region,
 * copy the RELRO content into it, then have each process swap its private,
 * regular RELRO, with a shared, read-only, mapping of the shared one.
 *
 * This trick saves 98% of the RELRO section size per extra process, after the
 * first one. On the other hand, this requires careful communication between
 * the process where the shared RELRO is created and the one(s) where it is used.
 *
 * Note that swapping the regular RELRO with the shared one is not an atomic
 * operation. Care must be taken that no other thread tries to run native code
 * that accesses it during it. In practice, this means the swap must happen
 * before library native code is executed.
 *
 * [1] The exceptions are pointers to external, randomized, symbols, like
 * those from some system libraries, but these are very few in practice.
 */

/*
 * Security considerations:
 *
 * - Whether the browser process loads its native libraries at the same
 *   addresses as the service ones (to save RAM by sharing the RELRO too)
 *   depends on the configuration variable BROWSER_SHARED_RELRO_CONFIG below.
 *
 *   Not using fixed library addresses in the browser process is preferred
 *   for regular devices since it maintains the efficacy of ASLR as an
 *   exploit mitigation across the render <-> browser privilege boundary.
 *
 * - The shared RELRO memory region is always forced read-only after creation,
 *   which means it is impossible for a compromised service process to map
 *   it read-write (e.g. by calling mmap() or mprotect()) and modify its
 *   content, altering values seen in other service processes.
 *
 * - Unfortunately, certain Android systems use an old, buggy kernel, that
 *   doesn't check Ashmem region permissions correctly. See CVE-2011-1149
 *   for details. This linker probes the system on startup and will completely
 *   disable shared RELROs if it detects the problem. For the record, this is
 *   common for Android emulator system images (which are still based on 2.6.29)
 *
 * - Once the RELRO ashmem region is mapped into a service process' address
 *   space, the corresponding file descriptor is immediately closed. The
 *   file descriptor is kept opened in the browser process, because a copy needs
 *   to be sent to each new potential service process.
 *
 * - The common library load addresses are randomized for each instance of
 *   the program on the device. See computeRandomBaseLoadAddress() for more
 *   details on how this is computed.
 *
 * - When loading several libraries in service processes, a simple incremental
 *   approach from the original random base load address is used. This is
 *   sufficient to deal correctly with component builds (which can use dozens
 *   of shared libraries), while regular builds always embed a single shared
 *   library per APK.
 */

/*
 * Here's an explanation of how this class is supposed to be used:
 *
 *  - Native shared libraries should be loaded with Linker.loadLibrary(),
 *    instead of System.loadLibrary(). The two functions take the same parameter
 *    and should behave the same (at a high level).
 *
 *  - Before loading any library, prepareLibraryLoad() should be called.
 *
 *  - After loading all libraries, finishLibraryLoad() should be called, before
 *    running any native code from any of the libraries (except their static
 *    constructors, which can't be avoided).
 *
 *  - A service process shall call either initServiceProcess() or
 *    disableSharedRelros() early (i.e. before any loadLibrary() call).
 *    Otherwise, the linker considers that it is running inside the browser
 *    process. This is because various content-based projects have vastly
 *    different initialization paths.
 *
 *    disableSharedRelros() completely disables shared RELROs, and loadLibrary()
 *    will behave exactly like System.loadLibrary().
 *
 *    initServiceProcess(baseLoadAddress) indicates that shared RELROs are to be
 *    used in this process.
 *
 *  - The browser is in charge of deciding where in memory each library should
 *    be loaded. This address must be passed to each service process (see
 *    LinkerParams.java for a helper class to do so).
 *
 *  - The browser will also generate shared RELROs for each library it loads.
 *    More specifically, by default when in the browser process, the linker
 *    will:
 *
 *       - Load libraries randomly (just like System.loadLibrary()).
 *       - Compute the fixed address to be used to load the same library
 *         in service processes.
 *       - Create a shared memory region populated with the RELRO region
 *         content pre-relocated for the specific fixed address above.
 *
 *    Note that these shared RELRO regions cannot be used inside the browser
 *    process. They are also never mapped into it.
 *
 *    This behaviour is altered by the BROWSER_SHARED_RELRO_CONFIG configuration
 *    variable below, which may force the browser to load the libraries at
 *    fixed addresses to.
 *
 *  - Once all libraries are loaded in the browser process, one can call
 *    getSharedRelros() which returns a Bundle instance containing a map that
 *    links each loaded library to its shared RELRO region.
 *
 *    This Bundle must be passed to each service process, for example through
 *    a Binder call (note that the Bundle includes file descriptors and cannot
 *    be added as an Intent extra).
 *
 *  - In a service process, finishLibraryLoad() will block until the RELRO
 *    section Bundle is received. This is typically done by calling
 *    useSharedRelros() from another thread.
 *
 *    This method also ensures the process uses the shared RELROs.
 */
public class Linker {

    // Log tag for this class. This must match the name of the linker's native library.
    private static final String TAG = "content_android_linker";

    // Set to true to enable debug logs.
    private static final boolean DEBUG = false;

    // Constants used to control the behaviour of the browser process with
    // regards to the shared RELRO section.
    //   NEVER        -> The browser never uses it itself.
    //   LOW_RAM_ONLY -> It is only used on devices with low RAM.
    //   ALWAYS       -> It is always used.
    // NOTE: These names are known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG_NEVER = 0;
    public static final int BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY = 1;
    public static final int BROWSER_SHARED_RELRO_CONFIG_ALWAYS = 2;

    // Configuration variable used to control how the browser process uses the
    // shared RELRO. Only change this while debugging linker-related issues.
    // NOTE: This variable's name is known and expected by the Linker test scripts.
    public static final int BROWSER_SHARED_RELRO_CONFIG =
            BROWSER_SHARED_RELRO_CONFIG_ALWAYS;

    // Constants used to control the value of sMemoryDeviceConfig.
    //   INIT         -> Value is undetermined (will check at runtime).
    //   LOW          -> This is a low-memory device.
    //   NORMAL       -> This is not a low-memory device.
    public static final int MEMORY_DEVICE_CONFIG_INIT = 0;
    public static final int MEMORY_DEVICE_CONFIG_LOW = 1;
    public static final int MEMORY_DEVICE_CONFIG_NORMAL = 2;

    // Indicates if this is a low-memory device or not. The default is to
    // determine this by probing the system at runtime, but this can be forced
    // for testing by calling setMemoryDeviceConfig().
    private static int sMemoryDeviceConfig = MEMORY_DEVICE_CONFIG_INIT;

    // Becomes true after linker initialization.
    private static boolean sInitialized = false;

    // Set to true to indicate that the system supports safe sharing of RELRO sections.
    private static boolean sRelroSharingSupported = false;

    // Set to true if this runs in the browser process. Disabled by initServiceProcess().
    private static boolean sInBrowserProcess = true;

    // Becomes true to indicate this process needs to wait for a shared RELRO in
    // finishLibraryLoad().
    private static boolean sWaitForSharedRelros = false;

    // Becomes true when initialization determines that the browser process can use the
    // shared RELRO.
    private static boolean sBrowserUsesSharedRelro = false;

    // The map of all RELRO sections either created or used in this process.
    private static Bundle sSharedRelros = null;

    // Current common random base load address.
    private static long sBaseLoadAddress = 0;

    // Current fixed-location load address for the next library called by loadLibrary().
    private static long sCurrentLoadAddress = 0;

    // Becomes true if any library fails to load at a given, non-0, fixed address.
    private static boolean sLoadAtFixedAddressFailed = false;

    // Becomes true once prepareLibraryLoad() has been called.
    private static boolean sPrepareLibraryLoadCalled = false;

    // Used internally to initialize the linker's static data. Assume lock is held.
    private static void ensureInitializedLocked() {
        assert Thread.holdsLock(Linker.class);

        if (!sInitialized) {
            sRelroSharingSupported = false;
            if (NativeLibraries.USE_LINKER) {
                if (DEBUG) Log.i(TAG, "Loading lib" + TAG + ".so");
                try {
                    System.loadLibrary(TAG);
                } catch (UnsatisfiedLinkError  e) {
                    // In a component build, the ".cr" suffix is added to each library name.
                    System.loadLibrary(TAG + ".cr");
                }
                sRelroSharingSupported = nativeCanUseSharedRelro();
                if (!sRelroSharingSupported)
                    Log.w(TAG, "This system cannot safely share RELRO sections");
                else {
                    if (DEBUG) Log.i(TAG, "This system supports safe shared RELRO sections");
                }

                if (sMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_INIT) {
                    sMemoryDeviceConfig = SysUtils.isLowEndDevice() ?
                            MEMORY_DEVICE_CONFIG_LOW : MEMORY_DEVICE_CONFIG_NORMAL;
                }

                switch (BROWSER_SHARED_RELRO_CONFIG) {
                    case BROWSER_SHARED_RELRO_CONFIG_NEVER:
                        sBrowserUsesSharedRelro = false;
                        break;
                    case BROWSER_SHARED_RELRO_CONFIG_LOW_RAM_ONLY:
                        sBrowserUsesSharedRelro =
                                (sMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW);
                        if (sBrowserUsesSharedRelro)
                            Log.w(TAG, "Low-memory device: shared RELROs used in all processes");
                        break;
                    case BROWSER_SHARED_RELRO_CONFIG_ALWAYS:
                        Log.w(TAG, "Beware: shared RELROs used in all processes!");
                        sBrowserUsesSharedRelro = true;
                        break;
                    default:
                        assert false : "Unreached";
                        break;
                }
            } else {
                if (DEBUG) Log.i(TAG, "Linker disabled");
            }

            if (!sRelroSharingSupported) {
                // Sanity.
                sBrowserUsesSharedRelro = false;
                sWaitForSharedRelros = false;
            }

            sInitialized = true;
        }
    }

    /**
     * A public interface used to run runtime linker tests after loading
     * libraries. Should only be used to implement the linker unit tests,
     * which is controlled by the value of NativeLibraries.ENABLE_LINKER_TESTS
     * configured at build time.
     */
    public interface TestRunner {
        /**
         * Run runtime checks and return true if they all pass.
         * @param memoryDeviceConfig The current memory device configuration.
         * @param inBrowserProcess true iff this is the browser process.
         */
        public boolean runChecks(int memoryDeviceConfig, boolean inBrowserProcess);
    }

    // The name of a class that implements TestRunner.
    static String sTestRunnerClassName = null;

    /**
     * Set the TestRunner by its class name. It will be instantiated at
     * runtime after all libraries are loaded.
     * @param testRunnerClassName null or a String for the class name of the
     * TestRunner to use.
     */
    public static void setTestRunnerClassName(String testRunnerClassName) {
        if (DEBUG) Log.i(TAG, "setTestRunnerByClassName(" + testRunnerClassName + ") called");

        if (!NativeLibraries.ENABLE_LINKER_TESTS) {
            // Ignore this in production code to prevent malvolent runtime injection.
            return;
        }

        synchronized (Linker.class) {
            assert sTestRunnerClassName == null;
            sTestRunnerClassName = testRunnerClassName;
        }
    }

    /**
     * Call this to retrieve the name of the current TestRunner class name
     * if any. This can be useful to pass it from the browser process to
     * child ones.
     * @return null or a String holding the name of the class implementing
     * the TestRunner set by calling setTestRunnerClassName() previously.
     */
    public static String getTestRunnerClassName() {
        synchronized (Linker.class) {
            return sTestRunnerClassName;
        }
    }

    /**
     * Call this method before any other Linker method to force a specific
     * memory device configuration. Should only be used for testing.
     * @param config either MEMORY_DEVICE_CONFIG_LOW or MEMORY_DEVICE_CONFIG_NORMAL.
     */
    public static void setMemoryDeviceConfig(int memoryDeviceConfig) {
        if (DEBUG) Log.i(TAG, "setMemoryDeviceConfig(" + memoryDeviceConfig + ") called");
        // Sanity check. This method should only be called during tests.
        assert NativeLibraries.ENABLE_LINKER_TESTS;
        synchronized (Linker.class) {
            assert sMemoryDeviceConfig == MEMORY_DEVICE_CONFIG_INIT;
            assert memoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW ||
                   memoryDeviceConfig == MEMORY_DEVICE_CONFIG_NORMAL;
            if (DEBUG) {
                if (memoryDeviceConfig == MEMORY_DEVICE_CONFIG_LOW)
                    Log.i(TAG, "Simulating a low-memory device");
                else
                    Log.i(TAG, "Simulating a regular-memory device");
            }
            sMemoryDeviceConfig = memoryDeviceConfig;
        }
    }

    /**
     * Call this method to determine if this content-based project must
     * use this linker. If not, System.loadLibrary() should be used to load
     * libraries instead.
     */
    public static boolean isUsed() {
        // Only GYP targets that are APKs and have the 'use_content_linker' variable
        // defined as 1 will use this linker. For all others (the default), the
        // auto-generated NativeLibraries.USE_LINKER variable will be false.
        if (!NativeLibraries.USE_LINKER)
            return false;

        synchronized (Linker.class) {
            ensureInitializedLocked();
            // At the moment, there is also no point in using this linker if the
            // system does not support RELRO sharing safely.
            return sRelroSharingSupported;
        }
    }

    /**
     * Call this method just before loading any native shared libraries in this process.
     */
    public static void prepareLibraryLoad() {
        if (DEBUG) Log.i(TAG, "prepareLibraryLoad() called");
        synchronized (Linker.class) {
            sPrepareLibraryLoadCalled = true;

            if (sInBrowserProcess) {
                // Force generation of random base load address, as well
                // as creation of shared RELRO sections in this process.
                setupBaseLoadAddressLocked();
            }
        }
    }

    /**
     * Call this method just after loading all native shared libraries in this process.
     * Note that when in a service process, this will block until the RELRO bundle is
     * received, i.e. when another thread calls useSharedRelros().
     */
     public static void finishLibraryLoad() {
        if (DEBUG) Log.i(TAG, "finishLibraryLoad() called");
        synchronized (Linker.class) {
            if (DEBUG) Log.i(TAG, String.format(
                    "sInBrowserProcess=%s sBrowserUsesSharedRelro=%s sWaitForSharedRelros=%s",
                    sInBrowserProcess ? "true" : "false",
                    sBrowserUsesSharedRelro ? "true" : "false",
                    sWaitForSharedRelros ? "true" : "false"));

            if (sLoadedLibraries == null) {
                if (DEBUG) Log.i(TAG, "No libraries loaded");
            } else {
                if (sInBrowserProcess) {
                    // Create new Bundle containing RELRO section information
                    // for all loaded libraries. Make it available to getSharedRelros().
                    sSharedRelros = createBundleFromLibInfoMap(sLoadedLibraries);
                    if (DEBUG) {
                        Log.i(TAG, "Shared RELRO created");
                        dumpBundle(sSharedRelros);
                    }

                    if (sBrowserUsesSharedRelro) {
                        useSharedRelrosLocked(sSharedRelros);
                    }
                }

                if (sWaitForSharedRelros) {
                    assert !sInBrowserProcess;

                    // Wait until the shared relro bundle is received from useSharedRelros().
                    while (sSharedRelros == null) {
                        try {
                            Linker.class.wait();
                        } catch (InterruptedException ie) {
                        }
                    }
                    useSharedRelrosLocked(sSharedRelros);
                    // Clear the Bundle to ensure its file descriptor references can't be reused.
                    sSharedRelros.clear();
                    sSharedRelros = null;
                }
            }

            if (NativeLibraries.ENABLE_LINKER_TESTS && sTestRunnerClassName != null) {
                // The TestRunner implementation must be instantiated _after_
                // all libraries are loaded to ensure that its native methods
                // are properly registered.
                if (DEBUG) Log.i(TAG, "Instantiating " + sTestRunnerClassName);
                TestRunner testRunner = null;
                try {
                    testRunner = (TestRunner)
                            Class.forName(sTestRunnerClassName).newInstance();
                } catch (Exception e) {
                    Log.e(TAG, "Could not extract test runner class name", e);
                    testRunner = null;
                }
                if (testRunner != null) {
                    if (!testRunner.runChecks(sMemoryDeviceConfig, sInBrowserProcess)) {
                        Log.wtf(TAG, "Linker runtime tests failed in this process!!");
                        assert false;
                    } else {
                        Log.i(TAG, "All linker tests passed!");
                    }
                }
            }
        }
        if (DEBUG) Log.i(TAG, "finishLibraryLoad() exiting");
     }

    /**
     * Call this to send a Bundle containing the shared RELRO sections to be
     * used in this process. If initServiceProcess() was previously called,
     * finishLibraryLoad() will not exit until this method is called in another
     * thread with a non-null value.
     * @param bundle The Bundle instance containing a map of shared RELRO sections
     * to use in this process.
     */
    public static void useSharedRelros(Bundle bundle) {
        // Ensure the bundle uses the application's class loader, not the framework
        // one which doesn't know anything about LibInfo.
        if (bundle != null)
            bundle.setClassLoader(LibInfo.class.getClassLoader());

        if (DEBUG) Log.i(TAG, "useSharedRelros() called with " + bundle);

        synchronized (Linker.class) {
            // Note that in certain cases, this can be called before
            // initServiceProcess() in service processes.
            sSharedRelros = bundle;
            // Tell any listener blocked in finishLibraryLoad() about it.
            Linker.class.notifyAll();
        }
    }

    /**
     * Call this to retrieve the shared RELRO sections created in this process,
     * after loading all libraries.
     * @return a new Bundle instance, or null if RELRO sharing is disabled on
     * this system, or if initServiceProcess() was called previously.
     */
    public static Bundle getSharedRelros() {
        if (DEBUG) Log.i(TAG, "getSharedRelros() called");
        synchronized (Linker.class) {
            if (!sInBrowserProcess) {
                if (DEBUG) Log.i(TAG, "... returning null Bundle");
                return null;
            }

            // Return the Bundle created in finishLibraryLoad().
            if (DEBUG) Log.i(TAG, "... returning " + sSharedRelros);
            return sSharedRelros;
        }
    }


    /**
     * Call this method before loading any libraries to indicate that this
     * process shall neither create or reuse shared RELRO sections.
     */
    public static void disableSharedRelros() {
        if (DEBUG) Log.i(TAG, "disableSharedRelros() called");
        synchronized (Linker.class) {
            sInBrowserProcess = false;
            sWaitForSharedRelros = false;
            sBrowserUsesSharedRelro = false;
        }
    }

    /**
     * Call this method before loading any libraries to indicate that this
     * process is ready to reuse shared RELRO sections from another one.
     * Typically used when starting service processes.
     * @param baseLoadAddress the base library load address to use.
     */
    public static void initServiceProcess(long baseLoadAddress) {
        if (DEBUG) Log.i(TAG, String.format("initServiceProcess(0x%x) called", baseLoadAddress));
        synchronized (Linker.class) {
            ensureInitializedLocked();
            sInBrowserProcess = false;
            sBrowserUsesSharedRelro = false;
            if (sRelroSharingSupported) {
                sWaitForSharedRelros = true;
                sBaseLoadAddress = baseLoadAddress;
                sCurrentLoadAddress = baseLoadAddress;
            }
        }
    }

    /**
     * Retrieve the base load address of all shared RELRO sections.
     * This also enforces the creation of shared RELRO sections in
     * prepareLibraryLoad(), which can later be retrieved with getSharedRelros().
     * @return a common, random base load address, or 0 if RELRO sharing is
     * disabled.
     */
    public static long getBaseLoadAddress() {
        synchronized (Linker.class) {
            ensureInitializedLocked();
            if (!sInBrowserProcess) {
                Log.w(TAG, "Shared RELRO sections are disabled in this process!");
                return 0;
            }

            setupBaseLoadAddressLocked();
            if (DEBUG) Log.i(TAG, String.format("getBaseLoadAddress() returns 0x%x",
                                                sBaseLoadAddress));
            return sBaseLoadAddress;
        }
    }

    // Used internally to lazily setup the common random base load address.
    private static void setupBaseLoadAddressLocked() {
        assert Thread.holdsLock(Linker.class);
        if (sBaseLoadAddress == 0) {
            long address = computeRandomBaseLoadAddress();
            sBaseLoadAddress = address;
            sCurrentLoadAddress = address;
            if (address == 0) {
              // If the computed address is 0, there are issues with the
              // entropy source, so disable RELRO shared / fixed load addresses.
              Log.w(TAG, "Disabling shared RELROs due to bad entropy sources");
              sBrowserUsesSharedRelro = false;
              sWaitForSharedRelros = false;
            }
        }
    }


    /**
     * Compute a random base load address where to place loaded libraries.
     * @return new base load address, or 0 if the system does not support
     * RELRO sharing.
     */
    private static long computeRandomBaseLoadAddress() {
        // The kernel ASLR feature will place randomized mappings starting
        // from this address. Never try to load anything above this
        // explicitly to avoid random conflicts.
        final long baseAddressLimit = 0x40000000;

        // Start loading libraries from this base address.
        final long baseAddress = 0x20000000;

        // Maximum randomized base address value. Used to ensure a margin
        // of 192 MB below baseAddressLimit.
        final long baseAddressMax = baseAddressLimit - 192 * 1024 * 1024;

        // The maximum limit of the desired random offset.
        final long pageSize = nativeGetPageSize();
        final int offsetLimit = (int)((baseAddressMax - baseAddress) / pageSize);

        // Get the greatest power of 2 that is smaller or equal to offsetLimit.
        int numBits = 30;
        for (; numBits > 1; numBits--) {
            if ((1 << numBits) <= offsetLimit)
                break;
        }

        if (DEBUG) {
            final int maxValue = (1 << numBits) - 1;
            Log.i(TAG, String.format("offsetLimit=%d numBits=%d maxValue=%d (0x%x)",
                offsetLimit, numBits, maxValue, maxValue));
        }

        // Find a random offset between 0 and (2^numBits - 1), included.
        int offset = getRandomBits(numBits);
        long address = 0;
        if (offset >= 0)
            address = baseAddress + offset * pageSize;

        if (DEBUG) {
            Log.i(TAG,
                  String.format("Linker.computeRandomBaseLoadAddress() return 0x%x",
                                address));
        }
        return address;
    }

    /**
     * Return a cryptographically-strong random number of numBits bits.
     * @param numBits The number of bits in the result. Must be in 1..31 range.
     * @return A random integer between 0 and (2^numBits - 1), inclusive, or -1
     * in case of error (e.g. if /dev/urandom can't be opened or read).
     */
    private static int getRandomBits(int numBits) {
        // Sanity check.
        assert numBits > 0;
        assert numBits < 32;

        FileInputStream input;
        try {
            // A naive implementation would read a 32-bit integer then use modulo, but
            // this introduces a slight bias. Instead, read 32-bit integers from the
            // entropy source until the value is positive but smaller than maxLimit.
            input = new FileInputStream(new File("/dev/urandom"));
        } catch (Exception e) {
            Log.e(TAG, "Could not open /dev/urandom", e);
            return -1;
        }

        int result = 0;
        try {
            for (int n = 0; n < 4; n++)
                result = (result << 8) | (input.read() & 255);
        } catch (Exception e) {
            Log.e(TAG, "Could not read /dev/urandom", e);
            return -1;
        } finally {
            try {
                input.close();
            } catch (Exception e) {
                // Can't really do anything here.
            }
        }
        result &= (1 << numBits) - 1;

        if (DEBUG) {
            Log.i(TAG, String.format(
                    "getRandomBits(%d) returned %d", numBits, result));
        }

        return result;
    }

    // Used for debugging only.
    private static void dumpBundle(Bundle bundle) {
        if (DEBUG) Log.i(TAG, "Bundle has " + bundle.size() + " items: " + bundle);
    }

    /**
     * Use the shared RELRO section from a Bundle received form another process.
     * Call this after calling setBaseLoadAddress() then loading all libraries
     * with loadLibrary().
     * @param a Bundle instance generated with createSharedRelroBundle() in
     * another process.
     */
    private static void useSharedRelrosLocked(Bundle bundle) {
        assert Thread.holdsLock(Linker.class);

        if (DEBUG) Log.i(TAG, "Linker.useSharedRelrosLocked() called");

        if (bundle == null) {
            if (DEBUG) Log.i(TAG, "null bundle!");
            return;
        }

        if (!sRelroSharingSupported) {
            if (DEBUG) Log.i(TAG, "System does not support RELRO sharing");
            return;
        }

        if (sLoadedLibraries == null) {
            if (DEBUG) Log.i(TAG, "No libraries loaded!");
            return;
        }

        if (DEBUG) dumpBundle(bundle);
        HashMap<String, LibInfo> relroMap = createLibInfoMapFromBundle(bundle);

        // Apply the RELRO section to all libraries that were already loaded.
        for (Map.Entry<String, LibInfo> entry : relroMap.entrySet()) {
            String libName = entry.getKey();
            LibInfo libInfo = entry.getValue();
            if (!nativeUseSharedRelro(libName, libInfo)) {
                Log.w(TAG, "Could not use shared RELRO section for " + libName);
            } else {
                if (DEBUG) Log.i(TAG, "Using shared RELRO section for " + libName);
            }
        }

        // In service processes, close all file descriptors from the map now.
        if (!sInBrowserProcess)
            closeLibInfoMap(relroMap);

        if (DEBUG) Log.i(TAG, "Linker.useSharedRelrosLocked() exiting");
    }

    /**
     * Returns whether the linker was unable to load one library at a given fixed address.
     *
     * @return true if at least one library was not loaded at the expected fixed address.
     */
    public static boolean loadAtFixedAddressFailed() {
        return sLoadAtFixedAddressFailed;
    }

    /**
     * Load a native shared library with the Chromium linker.
     * If neither initSharedRelro() or readFromBundle() were called
     * previously, this uses the standard linker (i.e. System.loadLibrary()).
     *
     * @param library The library's base name.
     * @throws UnsatisfiedLinkError if the library does not exist.
     */
    public static void loadLibrary(String library) {
        if (DEBUG) Log.i(TAG, "loadLibrary: " + library);

        // Don't self-load the linker. This is because the build system is
        // not clever enough to understand that all the libraries packaged
        // in the final .apk don't need to be explicitly loaded.
        // Also deal with the component build that adds a .cr suffix to the name.
        if (library.equals(TAG) || library.equals(TAG + ".cr")) {
            if (DEBUG) Log.i(TAG, "ignoring self-linker load");
            return;
        }

        synchronized (Linker.class) {
            ensureInitializedLocked();

            // Security: Ensure prepareLibraryLoad() was called before.
            // In theory, this can be done lazily here, but it's more consistent
            // to use a pair of functions (i.e. prepareLibraryLoad() + finishLibraryLoad())
            // that wrap all calls to loadLibrary() in the library loader.
            assert sPrepareLibraryLoadCalled;

            String libName = System.mapLibraryName(library);

            if (sLoadedLibraries == null)
              sLoadedLibraries = new HashMap<String, LibInfo>();

            if (sLoadedLibraries.containsKey(libName)) {
                if (DEBUG) Log.i(TAG, "Not loading " + libName + " twice");
                return;
            }

            LibInfo libInfo = new LibInfo();
            LibInfo relroLibInfo = null;
            long loadAddress = 0;
            if ((sInBrowserProcess && sBrowserUsesSharedRelro) || sWaitForSharedRelros) {
                // Load the library at a fixed address.
                loadAddress = sCurrentLoadAddress;
            }

            if (!nativeLoadLibrary(libName, loadAddress, libInfo)) {
                String errorMessage = "Unable to load library: " + libName;
                Log.e(TAG, errorMessage);
                throw new UnsatisfiedLinkError(errorMessage);
            }
            // Keep track whether the library has been loaded at the expected load address.
            if (loadAddress != 0 && loadAddress != libInfo.mLoadAddress)
                sLoadAtFixedAddressFailed = true;

            // Print the load address to the logcat when testing the linker. The format
            // of the string is expected by the Python test_runner script as one of:
            //    BROWSER_LIBRARY_ADDRESS: <library-name> <address>
            //    RENDERER_LIBRARY_ADDRESS: <library-name> <address>
            // Where <library-name> is the library name, and <address> is the hexadecimal load
            // address.
            if (NativeLibraries.ENABLE_LINKER_TESTS) {
                Log.i(TAG, String.format(
                        "%s_LIBRARY_ADDRESS: %s %x",
                        sInBrowserProcess ? "BROWSER" : "RENDERER",
                        libName,
                        libInfo.mLoadAddress));
            }

            if (sInBrowserProcess) {
                // Create a new shared RELRO section at the 'current' fixed load address.
                if (!nativeCreateSharedRelro(libName, sCurrentLoadAddress, libInfo)) {
                  Log.w(TAG,
                      String.format(
                          "Could not create shared RELRO for %s at %x",
                          libName, sCurrentLoadAddress));
                } else {
                    if (DEBUG) Log.i(TAG,
                        String.format(
                            "Created shared RELRO for %s at %x: %s",
                            libName,
                            sCurrentLoadAddress,
                            libInfo.toString()));
                }
            }

            if (sCurrentLoadAddress != 0) {
                // Compute the next current load address. If sBaseLoadAddress
                // is not 0, this is an explicit library load address. Otherwise,
                // this is an explicit load address for relocated RELRO sections
                // only.
                sCurrentLoadAddress = libInfo.mLoadAddress + libInfo.mLoadSize;
            }

            sLoadedLibraries.put(libName, libInfo);
            if (DEBUG) Log.i(TAG, "Library details " + libInfo.toString());
        }
    }

    /**
     * Native method used to load a library.
     * @param library Platform specific library name (e.g. libfoo.so)
     * @param loadAddress Explicit load address, or 0 for randomized one.
     * @param relro_info If not null, the mLoadAddress and mLoadSize fields
     * of this LibInfo instance will set on success.
     * @return true for success, false otherwise.
     */
    private static native boolean nativeLoadLibrary(String library,
                                                    long  loadAddress,
                                                    LibInfo libInfo);

    /**
     * Native method used to create a shared RELRO section.
     * If the library was already loaded at the same address using
     * nativeLoadLibrary(), this creates the RELRO for it. Otherwise,
     * this loads a new temporary library at the specified address,
     * creates and extracts the RELRO section from it, then unloads it.
     * @param library Library name.
     * @param loadAddress load address, which can be different from the one
     * used to load the library in the current process!
     * @param LibInfo libInfo instance. On success, the mRelroStart, mRelroSize
     * and mRelroFd will be set.
     * @return true on success, false otherwise.
     */
    private static native boolean nativeCreateSharedRelro(String library,
                                                          long loadAddress,
                                                          LibInfo libInfo);

    /**
     * Native method used to use a shared RELRO section.
     * @param library Library name.
     * @param libInfo A LibInfo instance containing valid RELRO information
     * @return true on success.
     */
    private static native boolean nativeUseSharedRelro(String library,
                                                       LibInfo libInfo);

    /**
     * Checks that the system supports shared RELROs. Old Android kernels
     * have a bug in the way they check Ashmem region protection flags, which
     * makes using shared RELROs unsafe. This method performs a simple runtime
     * check for this misfeature, even though nativeEnableSharedRelro() will
     * always fail if this returns false.
     */
    private static native boolean nativeCanUseSharedRelro();

    // Returns the native page size in bytes.
    private static native long nativeGetPageSize();

    /**
     * Record information for a given library.
     * IMPORTANT: Native code knows about this class's fields, so
     * don't change them without modifying the corresponding C++ sources.
     * Also, the LibInfo instance owns the ashmem file descriptor.
     */
    public static class LibInfo implements Parcelable {

        public LibInfo() {
            mLoadAddress = 0;
            mLoadSize = 0;
            mRelroStart = 0;
            mRelroSize = 0;
            mRelroFd = -1;
        }

        public void close() {
            if (mRelroFd >= 0) {
              try {
                  ParcelFileDescriptor.adoptFd(mRelroFd).close();
              } catch (java.io.IOException e) {
              }
              mRelroFd = -1;
            }
        }

        // from Parcelable
        public LibInfo(Parcel in) {
            mLoadAddress = in.readLong();
            mLoadSize = in.readLong();
            mRelroStart = in.readLong();
            mRelroSize = in.readLong();
            ParcelFileDescriptor fd = in.readFileDescriptor();
            mRelroFd = fd.detachFd();
        }

        // from Parcelable
        @Override
        public void writeToParcel(Parcel out, int flags) {
            if (mRelroFd >= 0) {
                out.writeLong(mLoadAddress);
                out.writeLong(mLoadSize);
                out.writeLong(mRelroStart);
                out.writeLong(mRelroSize);
                try {
                    ParcelFileDescriptor fd = ParcelFileDescriptor.fromFd(mRelroFd);
                    fd.writeToParcel(out, 0);
                    fd.close();
                } catch (java.io.IOException e) {
                    Log.e(TAG, "Cant' write LibInfo file descriptor to parcel", e);
                }
            }
        }

        // from Parcelable
        @Override
        public int describeContents() {
            return Parcelable.CONTENTS_FILE_DESCRIPTOR;
        }

        // from Parcelable
        public static final Parcelable.Creator<LibInfo> CREATOR
                = new Parcelable.Creator<LibInfo>() {
            public LibInfo createFromParcel(Parcel in) {
                return new LibInfo(in);
            }

            public LibInfo[] newArray(int size) {
                return new LibInfo[size];
            }
        };

        public String toString() {
            return String.format("[load=0x%x-0x%x relro=0x%x-0x%x fd=%d]",
                                 mLoadAddress,
                                 mLoadAddress + mLoadSize,
                                 mRelroStart,
                                 mRelroStart + mRelroSize,
                                 mRelroFd);
        }

        // IMPORTANT: Don't change these fields without modifying the
        // native code that accesses them directly!
        public long mLoadAddress; // page-aligned library load address.
        public long mLoadSize;    // page-aligned library load size.
        public long mRelroStart;  // page-aligned address in memory, or 0 if none.
        public long mRelroSize;   // page-aligned size in memory, or 0.
        public int  mRelroFd;     // ashmem file descriptor, or -1
    }

    // Create a Bundle from a map of LibInfo objects.
    private static Bundle createBundleFromLibInfoMap(HashMap<String, LibInfo> map) {
        Bundle bundle = new Bundle(map.size());
        for (Map.Entry<String, LibInfo> entry : map.entrySet())
            bundle.putParcelable(entry.getKey(), entry.getValue());

        return bundle;
    }

    // Create a new LibInfo map from a Bundle.
    private static HashMap<String, LibInfo> createLibInfoMapFromBundle(Bundle bundle) {
        HashMap<String, LibInfo> map = new HashMap<String, LibInfo>();
        for (String library : bundle.keySet()) {
          LibInfo libInfo = bundle.getParcelable(library);
          map.put(library, libInfo);
        }
        return map;
    }

    // Call the close() method on all values of a LibInfo map.
    private static void closeLibInfoMap(HashMap<String, LibInfo> map) {
        for (Map.Entry<String, LibInfo> entry : map.entrySet())
            entry.getValue().close();
    }

    // The map of libraries that are currently loaded in this process.
    private static HashMap<String, LibInfo> sLoadedLibraries = null;

    // Used to pass the shared RELRO Bundle through Binder.
    public static final String EXTRA_LINKER_SHARED_RELROS =
        "org.chromium.content.common.linker.shared_relros";
}
