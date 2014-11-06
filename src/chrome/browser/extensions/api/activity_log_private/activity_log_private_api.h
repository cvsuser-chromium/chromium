// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This extension API provides access to the Activity Log, which is a
// monitoring framework for extension behavior. Only specific Google-produced
// extensions should have access to it.

#ifndef CHROME_BROWSER_EXTENSIONS_API_ACTIVITY_LOG_PRIVATE_ACTIVITY_LOG_PRIVATE_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_ACTIVITY_LOG_PRIVATE_ACTIVITY_LOG_PRIVATE_API_H_

#include "base/synchronization/lock.h"
#include "chrome/browser/extensions/activity_log/activity_actions.h"
#include "chrome/browser/extensions/activity_log/activity_log.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/extensions/event_router.h"

namespace extensions {

class ActivityLog;

// The ID of the trusted/whitelisted ActivityLog extension.
extern const char kActivityLogExtensionId[];
extern const char kActivityLogTestExtensionId[];
extern const char kActivityLogObsoleteExtensionId[];

// Handles interactions between the Activity Log API and implementation.
class ActivityLogAPI : public ProfileKeyedAPI,
                       public extensions::ActivityLog::Observer,
                       public EventRouter::Observer {
 public:
  explicit ActivityLogAPI(Profile* profile);
  virtual ~ActivityLogAPI();

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<ActivityLogAPI>* GetFactoryInstance();

  virtual void Shutdown() OVERRIDE;

  // Lookup whether the extension ID is whitelisted.
  static bool IsExtensionWhitelisted(const std::string& extension_id);

 private:
  friend class ProfileKeyedAPIFactory<ActivityLogAPI>;
  static const char* service_name() { return "ActivityLogPrivateAPI"; }

  // ActivityLog::Observer
  // We pass this along to activityLogPrivate.onExtensionActivity.
  virtual void OnExtensionActivity(scoped_refptr<Action> activity) OVERRIDE;

  // EventRouter::Observer
  // We only keep track of OnExtensionActivity if we have any listeners.
  virtual void OnListenerAdded(const EventListenerInfo& details) OVERRIDE;
  virtual void OnListenerRemoved(const EventListenerInfo& details) OVERRIDE;

  Profile* profile_;
  ActivityLog* activity_log_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(ActivityLogAPI);
};

template<>
void ProfileKeyedAPIFactory<ActivityLogAPI>::DeclareFactoryDependencies();

// The implementation of activityLogPrivate.getExtensionActivities
class ActivityLogPrivateGetExtensionActivitiesFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("activityLogPrivate.getExtensionActivities",
                             ACTIVITYLOGPRIVATE_GETEXTENSIONACTIVITIES)

 protected:
  virtual ~ActivityLogPrivateGetExtensionActivitiesFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;

 private:
  void OnLookupCompleted(
      scoped_ptr<std::vector<scoped_refptr<Action> > > activities);
};

// The implementation of activityLogPrivate.deleteDatabase
class ActivityLogPrivateDeleteDatabaseFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("activityLogPrivate.deleteDatabase",
                             ACTIVITYLOGPRIVATE_DELETEDATABASE)

 protected:
  virtual ~ActivityLogPrivateDeleteDatabaseFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

// The implementation of activityLogPrivate.deleteUrls
class ActivityLogPrivateDeleteUrlsFunction
    : public ChromeAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("activityLogPrivate.deleteUrls",
                             ACTIVITYLOGPRIVATE_DELETEURLS)

 protected:
  virtual ~ActivityLogPrivateDeleteUrlsFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_ACTIVITY_LOG_PRIVATE_ACTIVITY_LOG_PRIVATE_API_H_
