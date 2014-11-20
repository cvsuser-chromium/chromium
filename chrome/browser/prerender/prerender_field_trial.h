// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRERENDER_PRERENDER_FIELD_TRIAL_H_
#define CHROME_BROWSER_PRERENDER_PRERENDER_FIELD_TRIAL_H_

#include <string>

class CommandLine;
class Profile;

namespace prerender {

// Parse the --prerender= command line switch, which controls both prerendering
// and prefetching.  If the switch is unset, or is set to "auto", then the user
// is assigned to a field trial.
void ConfigurePrefetchAndPrerender(const CommandLine& command_line);

// Returns true if the user has opted in or has been opted in to the
// prerendering from Omnibox experiment.
bool IsOmniboxEnabled(Profile* profile);

// Returns true iff the Prerender Local Predictor is enabled.
bool IsLocalPredictorEnabled();

// Indicates whether to disable the local predictor due to unencrypted sync
// settings and configuration.
bool DisableLocalPredictorBasedOnSyncAndConfiguration(Profile* profile);

// Returns true iff the LoggedIn Predictor is enabled.
bool IsLoggedInPredictorEnabled();

// Returns true iff the side-effect free whitelist is enabled.
bool IsSideEffectFreeWhitelistEnabled();

// Returns true if the local predictor should actually launch prerenders.
bool IsLocalPredictorPrerenderLaunchEnabled();

// Returns true if the local predictor should prerender, but only as control
// group. If the local predictor never launches prerenders, then this setting
// is irrelevant.
bool IsLocalPredictorPrerenderAlwaysControlEnabled();

// Returns true if we should query the prerender service for the profile
// provided.
bool ShouldQueryPrerenderService(Profile* profile);

// Indicates whether we should query the prerender service for the current URL
// and candidate URLs, respectively.
bool ShouldQueryPrerenderServiceForCurrentURL();
bool ShouldQueryPrerenderServiceForCandidateURLs();

// Returns the URL prefix to be used for the prerender service. The only thing
// that will be appended is the urlencoded query json.
std::string GetPrerenderServiceURLPrefix();

// Returns the prerender service behavior ID that should be passed to the
// to the prerender service in requests.
int GetPrerenderServiceBehaviorID();

// Returns the fetch timeout to be used for the prerender service, in ms.
int GetPrerenderServiceFetchTimeoutMs();

// Returns the TTL to be used for the local predictor.
int GetLocalPredictorTTLSeconds();

// Returns the half-life time to use to decay local predictor prerender
// priorities.
int GetLocalPredictorPrerenderPriorityHalfLifeTimeSeconds();

// Returns the maximum number of concurrent prerenders the local predictor
// may maintain.
int GetLocalPredictorMaxConcurrentPrerenders();

// The following functions return whether certain LocalPredictor checks should
// be skipped, as indicated by the name.
bool SkipLocalPredictorFragment();
bool SkipLocalPredictorHTTPS();
bool SkipLocalPredictorWhitelist();
bool SkipLocalPredictorServiceWhitelist();
bool SkipLocalPredictorLoggedIn();
bool SkipLocalPredictorDefaultNoPrerender();
bool SkipLocalPredictorLocalCandidates();
bool SkipLocalPredictorServiceCandidates();

}  // namespace prerender

#endif  // CHROME_BROWSER_PRERENDER_PRERENDER_FIELD_TRIAL_H_
