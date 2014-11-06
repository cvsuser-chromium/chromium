// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/test_switches.h"

// Time (in milliseconds) that the tests should wait before timing out.
// TODO(phajdan.jr): Clean up the switch names.
const char switches::kTestLargeTimeout[] = "test-large-timeout";

// Maximum number of tests to run in a single batch.
const char switches::kTestLauncherBatchLimit[] = "test-launcher-batch-limit";

// Behaves in a more friendly way to local debugging, e.g. serial execution,
// no redirection of stdio, etc.
const char switches::kTestLauncherDeveloperMode[] =
    "test-launcher-developer-mode";

// Number of parallel test launcher jobs.
const char switches::kTestLauncherJobs[] = "test-launcher-jobs";

// Path to test results file in our custom test launcher format.
const char switches::kTestLauncherOutput[] = "test-launcher-output";

// Path to test results file with all the info from the test launcher.
const char switches::kTestLauncherSummaryOutput[] =
    "test-launcher-summary-output";

// Flag controlling when test stdio is displayed as part of the launcher's
// standard output.
const char switches::kTestLauncherPrintTestStdio[] =
    "test-launcher-print-test-stdio";

// Index of the test shard to run, starting from 0 (first shard) to total shards
// minus one (last shard).
const char switches::kTestLauncherShardIndex[] =
    "test-launcher-shard-index";

// Total number of shards. Must be the same for all shards.
const char switches::kTestLauncherTotalShards[] =
    "test-launcher-total-shards";

// Time (in milliseconds) that the tests should wait before timing out.
const char switches::kTestLauncherTimeout[] = "test-launcher-timeout";
// TODO(phajdan.jr): Clean up the switch names.
const char switches::kTestTinyTimeout[] = "test-tiny-timeout";
const char switches::kUiTestActionTimeout[] = "ui-test-action-timeout";
const char switches::kUiTestActionMaxTimeout[] = "ui-test-action-max-timeout";
