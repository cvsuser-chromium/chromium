// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/activity_log/activity_log.h"

#include <set>
#include <vector>

#include "base/command_line.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/extensions/activity_log/activity_action_constants.h"
#include "chrome/browser/extensions/activity_log/counting_policy.h"
#include "chrome/browser/extensions/activity_log/fullstream_ui_policy.h"
#include "chrome/browser/extensions/api/activity_log_private/activity_log_private_api.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/extensions/extension_system_factory.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "chrome/browser/prefs/pref_service_syncable.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/pref_names.h"
#include "components/browser_context_keyed_service/browser_context_dependency_manager.h"
#include "content/public/browser/web_contents.h"
#include "third_party/re2/re2/re2.h"
#include "url/gurl.h"

#if !defined(OS_ANDROID)
#include "chrome/browser/extensions/activity_log/uma_policy.h"
#endif

namespace constants = activity_log_constants;

namespace {

using extensions::Action;
using constants::kArgUrlPlaceholder;

// If DOM API methods start with this string, we flag them as being of type
// DomActionType::XHR.
const char kDomXhrPrefix[] = "XMLHttpRequest.";

// Specifies a possible action to take to get an extracted URL in the ApiInfo
// structure below.
enum Transformation {
  NONE,
  DICT_LOOKUP,
  LOOKUP_TAB_ID,
};

// Information about specific Chrome and DOM APIs, such as which contain
// arguments that should be extracted into the arg_url field of an Action.
struct ApiInfo {
  // The lookup key consists of the action_type and api_name in the Action
  // object.
  Action::ActionType action_type;
  const char* api_name;

  // If non-negative, an index into args might contain a URL to be extracted
  // into arg_url.
  int arg_url_index;

  // A transformation to apply to the data found at index arg_url_index in the
  // argument list.
  //
  // If NONE, the data is expected to be a string which is treated as a URL.
  //
  // If LOOKUP_TAB_ID, the data is either an integer which is treated as a tab
  // ID and translated (in the context of a provided Profile), or a list of tab
  // IDs which are translated.
  //
  // If DICT_LOOKUP, the data is expected to be a dictionary, and
  // arg_url_dict_path is a path (list of keys delimited by ".") where a URL
  // string is to be found.
  Transformation arg_url_transform;
  const char* arg_url_dict_path;
};

static const ApiInfo kApiInfoTable[] = {
  // Tabs APIs that require tab ID translation
  {Action::ACTION_API_CALL, "tabs.connect", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.detectLanguage", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.duplicate", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.executeScript", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.get", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.insertCSS", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.move", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.reload", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.remove", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.sendMessage", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_CALL, "tabs.update", 0, LOOKUP_TAB_ID, NULL},

  {Action::ACTION_API_EVENT, "tabs.onUpdated", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_EVENT, "tabs.onMoved", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_EVENT, "tabs.onDetached", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_EVENT, "tabs.onAttached", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_EVENT, "tabs.onRemoved", 0, LOOKUP_TAB_ID, NULL},
  {Action::ACTION_API_EVENT, "tabs.onReplaced", 0, LOOKUP_TAB_ID, NULL},

  // Other APIs that accept URLs as strings
  {Action::ACTION_API_CALL, "bookmarks.create", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "bookmarks.update", 1, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "cookies.get", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "cookies.getAll", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "cookies.remove", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "cookies.set", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "downloads.download", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "history.addUrl", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "history.deleteUrl", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "history.getVisits", 0, DICT_LOOKUP, "url"},
  {Action::ACTION_API_CALL, "webstore.install", 0, NONE, NULL},
  {Action::ACTION_API_CALL, "windows.create", 0, DICT_LOOKUP, "url"},

  {Action::ACTION_DOM_ACCESS, "Document.location", 0, NONE, NULL},
  {Action::ACTION_DOM_ACCESS, "Location.assign", 0, NONE, NULL},
  {Action::ACTION_DOM_ACCESS, "Location.replace", 0, NONE, NULL},
  {Action::ACTION_DOM_ACCESS, "Window.location", 0, NONE, NULL},
  {Action::ACTION_DOM_ACCESS, "XMLHttpRequest.open", 1, NONE, NULL},
};

// A singleton class which provides lookups into the kApiInfoTable data
// structure.  It inserts all data into a map on first lookup.
class ApiInfoDatabase {
 public:
  static ApiInfoDatabase* GetInstance() {
    return Singleton<ApiInfoDatabase>::get();
  }

  // Retrieves an ApiInfo record for the given Action type.  Returns either a
  // pointer to the record, or NULL if no such record was found.
  const ApiInfo* Lookup(Action::ActionType action_type,
                        const std::string& api_name) const {
    std::map<std::string, const ApiInfo*>::const_iterator i =
        api_database_.find(api_name);
    if (i == api_database_.end())
      return NULL;
    if (i->second->action_type != action_type)
      return NULL;
    return i->second;
  }

 private:
  ApiInfoDatabase() {
    for (size_t i = 0; i < arraysize(kApiInfoTable); i++) {
      const ApiInfo* info = &kApiInfoTable[i];
      api_database_[info->api_name] = info;
    }
  }
  virtual ~ApiInfoDatabase() {}

  // The map is keyed by API name only, since API names aren't be repeated
  // across multiple action types in kApiInfoTable.  However, the action type
  // should still be checked before returning a positive match.
  std::map<std::string, const ApiInfo*> api_database_;

  friend struct DefaultSingletonTraits<ApiInfoDatabase>;
  DISALLOW_COPY_AND_ASSIGN(ApiInfoDatabase);
};

// Gets the URL for a given tab ID.  Helper method for ExtractUrls.  Returns
// true if able to perform the lookup.  The URL is stored to *url, and
// *is_incognito is set to indicate whether the URL is for an incognito tab.
bool GetUrlForTabId(int tab_id,
                    Profile* profile,
                    GURL* url,
                    bool* is_incognito) {
  content::WebContents* contents = NULL;
  Browser* browser = NULL;
  bool found = ExtensionTabUtil::GetTabById(tab_id,
                                            profile,
                                            true,  // search incognito tabs too
                                            &browser,
                                            NULL,
                                            &contents,
                                            NULL);
  if (found) {
    *url = contents->GetURL();
    *is_incognito = browser->profile()->IsOffTheRecord();
    return true;
  } else {
    return false;
  }
}

// Resolves an argument URL relative to a base page URL.  If the page URL is
// not valid, then only absolute argument URLs are supported.
bool ResolveUrl(const GURL& base, const std::string& arg, GURL* arg_out) {
  if (base.is_valid())
    *arg_out = base.Resolve(arg);
  else
    *arg_out = GURL(arg);

  return arg_out->is_valid();
}

// Performs processing of the Action object to extract URLs from the argument
// list and translate tab IDs to URLs, according to the API call metadata in
// kApiInfoTable.  Mutates the Action object in place.  There is a small chance
// that the tab id->URL translation could be wrong, if the tab has already been
// navigated by the time of invocation.
//
// Any extracted URL is stored into the arg_url field of the action, and the
// URL in the argument list is replaced with the marker value "<arg_url>".  For
// APIs that take a list of tab IDs, extracts the first valid URL into arg_url
// and overwrites the other tab IDs in the argument list with the translated
// URL.
void ExtractUrls(scoped_refptr<Action> action, Profile* profile) {
  const ApiInfo* api_info = ApiInfoDatabase::GetInstance()->Lookup(
      action->action_type(), action->api_name());
  if (api_info == NULL)
    return;

  int url_index = api_info->arg_url_index;

  if (!action->args() || url_index < 0 ||
      static_cast<size_t>(url_index) >= action->args()->GetSize())
    return;

  // Do not overwrite an existing arg_url value in the Action, so that callers
  // have the option of doing custom arg_url extraction.
  if (action->arg_url().is_valid())
    return;

  GURL arg_url;
  bool arg_incognito = action->page_incognito();

  switch (api_info->arg_url_transform) {
    case NONE: {
      // No translation needed; just extract the URL directly from a raw string
      // or from a dictionary.  Succeeds if we can find a string in the
      // argument list and that the string resolves to a valid URL.
      std::string url_string;
      if (action->args()->GetString(url_index, &url_string) &&
          ResolveUrl(action->page_url(), url_string, &arg_url)) {
        action->mutable_args()->Set(url_index,
                                    new StringValue(kArgUrlPlaceholder));
      }
      break;
    }

    case DICT_LOOKUP: {
      CHECK(api_info->arg_url_dict_path);
      // Look up the URL from a dictionary at the specified location.  Succeeds
      // if we can find a dictionary in the argument list, the dictionary
      // contains the specified key, and the corresponding value resolves to a
      // valid URL.
      DictionaryValue* dict = NULL;
      std::string url_string;
      if (action->mutable_args()->GetDictionary(url_index, &dict) &&
          dict->GetString(api_info->arg_url_dict_path, &url_string) &&
          ResolveUrl(action->page_url(), url_string, &arg_url)) {
        dict->SetString(api_info->arg_url_dict_path, kArgUrlPlaceholder);
      }
      break;
    }

    case LOOKUP_TAB_ID: {
      // Translation of tab IDs to URLs has been requested.  There are two
      // cases to consider: either a single integer or a list of integers (when
      // multiple tabs are manipulated).
      int tab_id;
      base::ListValue* tab_list = NULL;
      if (action->args()->GetInteger(url_index, &tab_id)) {
        // Single tab ID to translate.
        GetUrlForTabId(tab_id, profile, &arg_url, &arg_incognito);
        if (arg_url.is_valid()) {
          action->mutable_args()->Set(url_index,
                                      new StringValue(kArgUrlPlaceholder));
        }
      } else if (action->mutable_args()->GetList(url_index, &tab_list)) {
        // A list of possible IDs to translate.  Work through in reverse order
        // so the last one translated is left in arg_url.
        int extracted_index = -1;  // Which list item is copied to arg_url?
        for (int i = tab_list->GetSize() - 1; i >= 0; --i) {
          if (tab_list->GetInteger(i, &tab_id) &&
              GetUrlForTabId(tab_id, profile, &arg_url, &arg_incognito)) {
            if (!arg_incognito)
              tab_list->Set(i, new base::StringValue(arg_url.spec()));
            extracted_index = i;
          }
        }
        if (extracted_index >= 0)
          tab_list->Set(extracted_index, new StringValue(kArgUrlPlaceholder));
      }
      break;
    }

    default:
      NOTREACHED();
  }

  if (arg_url.is_valid()) {
    action->set_arg_incognito(arg_incognito);
    action->set_arg_url(arg_url);
  }
}

}  // namespace

namespace extensions {

// ActivityLogFactory

ActivityLogFactory* ActivityLogFactory::GetInstance() {
  return Singleton<ActivityLogFactory>::get();
}

BrowserContextKeyedService* ActivityLogFactory::BuildServiceInstanceFor(
    content::BrowserContext* profile) const {
  return new ActivityLog(static_cast<Profile*>(profile));
}

content::BrowserContext* ActivityLogFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}

ActivityLogFactory::ActivityLogFactory()
    : BrowserContextKeyedServiceFactory(
        "ActivityLog",
        BrowserContextDependencyManager::GetInstance()) {
  DependsOn(ExtensionSystemFactory::GetInstance());
  DependsOn(InstallTrackerFactory::GetInstance());
}

// static
ActivityLog* ActivityLog::GetInstance(Profile* profile) {
  return ActivityLogFactory::GetForProfile(profile);
}

ActivityLogFactory::~ActivityLogFactory() {
}

// ActivityLog

// SET THINGS UP. --------------------------------------------------------------

// Use GetInstance instead of directly creating an ActivityLog.
ActivityLog::ActivityLog(Profile* profile)
    : database_policy_(NULL),
      database_policy_type_(ActivityLogPolicy::POLICY_INVALID),
      uma_policy_(NULL),
      profile_(profile),
      db_enabled_(false),
      testing_mode_(false),
      has_threads_(true),
      tracker_(NULL),
      watchdog_app_active_(false) {
  // This controls whether logging statements are printed & which policy is set.
  testing_mode_ = CommandLine::ForCurrentProcess()->HasSwitch(
    switches::kEnableExtensionActivityLogTesting);

  // Check if the watchdog extension is previously installed and active.
  watchdog_app_active_ =
    profile_->GetPrefs()->GetBoolean(prefs::kWatchdogExtensionActive);

  observers_ = new ObserverListThreadSafe<Observer>;

  // Check that the right threads exist for logging to the database.
  // If not, we shouldn't try to do things that require them.
  if (!BrowserThread::IsMessageLoopValid(BrowserThread::DB) ||
      !BrowserThread::IsMessageLoopValid(BrowserThread::FILE) ||
      !BrowserThread::IsMessageLoopValid(BrowserThread::IO)) {
    has_threads_ = false;
  }

  db_enabled_ = has_threads_
      && (CommandLine::ForCurrentProcess()->
          HasSwitch(switches::kEnableExtensionActivityLogging)
      || watchdog_app_active_);

  ExtensionSystem::Get(profile_)->ready().Post(
      FROM_HERE,
      base::Bind(&ActivityLog::InitInstallTracker, base::Unretained(this)));

// None of this should run on Android since the AL is behind ENABLE_EXTENSION
// checks. However, UmaPolicy can't even compile on Android because it uses
// BrowserList and related classes that aren't compiled for Android.
#if !defined(OS_ANDROID)
  if (!profile->IsOffTheRecord())
    uma_policy_ = new UmaPolicy(profile_);
#endif

  ChooseDatabasePolicy();
}

void ActivityLog::SetDatabasePolicy(
    ActivityLogPolicy::PolicyType policy_type) {
  if (database_policy_type_ == policy_type)
    return;
  if (!IsDatabaseEnabled() && !IsWatchdogAppActive())
    return;

  // Deleting the old policy takes place asynchronously, on the database
  // thread.  Initializing a new policy below similarly happens
  // asynchronously.  Since the two operations are both queued for the
  // database, the queue ordering should ensure that the deletion completes
  // before database initialization occurs.
  //
  // However, changing policies at runtime is still not recommended, and
  // likely only should be done for unit tests.
  if (database_policy_)
    database_policy_->Close();

  switch (policy_type) {
    case ActivityLogPolicy::POLICY_FULLSTREAM:
      database_policy_ = new FullStreamUIPolicy(profile_);
      break;
    case ActivityLogPolicy::POLICY_COUNTS:
      database_policy_ = new CountingPolicy(profile_);
      break;
    default:
      NOTREACHED();
  }
  database_policy_->Init();
  database_policy_type_ = policy_type;
}

// SHUT DOWN. ------------------------------------------------------------------

void ActivityLog::Shutdown() {
  if (tracker_) tracker_->RemoveObserver(this);
}

ActivityLog::~ActivityLog() {
  if (uma_policy_)
    uma_policy_->Close();
  if (database_policy_)
    database_policy_->Close();
}

// MAINTAIN STATUS. ------------------------------------------------------------

void ActivityLog::InitInstallTracker() {
  tracker_ = InstallTrackerFactory::GetForProfile(profile_);
  tracker_->AddObserver(this);
}

void ActivityLog::ChooseDatabasePolicy() {
  if (!(IsDatabaseEnabled() || IsWatchdogAppActive()))
    return;
  if (testing_mode_)
    SetDatabasePolicy(ActivityLogPolicy::POLICY_FULLSTREAM);
  else
    SetDatabasePolicy(ActivityLogPolicy::POLICY_COUNTS);
}

bool ActivityLog::IsDatabaseEnabled() {
  // Make sure we are not enabled when there are no threads.
  DCHECK(has_threads_ || !db_enabled_);
  return db_enabled_;
}

bool ActivityLog::IsWatchdogAppActive() {
  return watchdog_app_active_;
}

void ActivityLog::SetWatchdogAppActive(bool active) {
  watchdog_app_active_ = active;
}

void ActivityLog::OnExtensionLoaded(const Extension* extension) {
  if (extension->id() != kActivityLogExtensionId) return;
  if (has_threads_)
    db_enabled_ = true;
  if (!watchdog_app_active_) {
    watchdog_app_active_ = true;
    profile_->GetPrefs()->SetBoolean(prefs::kWatchdogExtensionActive, true);
  }
  ChooseDatabasePolicy();
}

void ActivityLog::OnExtensionUnloaded(const Extension* extension) {
  if (extension->id() != kActivityLogExtensionId) return;
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableExtensionActivityLogging)) {
    db_enabled_ = false;
  }
  if (watchdog_app_active_) {
    watchdog_app_active_ = false;
    profile_->GetPrefs()->SetBoolean(prefs::kWatchdogExtensionActive,
                                     false);
  }
}

void ActivityLog::OnExtensionUninstalled(const Extension* extension) {
  if (!database_policy_)
    return;
  // If the extension has been uninstalled but not disabled, we delete the
  // database.
  if (extension->id() == kActivityLogExtensionId) {
    if (!CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableExtensionActivityLogging)) {
      DeleteDatabase();
    }
  } else {
    database_policy_->RemoveExtensionData(extension->id());
  }
}

void ActivityLog::AddObserver(ActivityLog::Observer* observer) {
  observers_->AddObserver(observer);
}

void ActivityLog::RemoveObserver(ActivityLog::Observer* observer) {
  observers_->RemoveObserver(observer);
}

// static
void ActivityLog::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kWatchdogExtensionActive,
      false,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

// LOG ACTIONS. ----------------------------------------------------------------

void ActivityLog::LogAction(scoped_refptr<Action> action) {
  if (ActivityLogAPI::IsExtensionWhitelisted(action->extension_id()))
    return;

  // Perform some preprocessing of the Action data: convert tab IDs to URLs and
  // mask out incognito URLs if appropriate.
  ExtractUrls(action, profile_);

  // Mark DOM XHR requests as such, for easier processing later.
  if (action->action_type() == Action::ACTION_DOM_ACCESS &&
      StartsWithASCII(action->api_name(), kDomXhrPrefix, true) &&
      action->other()) {
    DictionaryValue* other = action->mutable_other();
    int dom_verb = -1;
    if (other->GetInteger(constants::kActionDomVerb, &dom_verb) &&
        dom_verb == DomActionType::METHOD) {
      other->SetInteger(constants::kActionDomVerb, DomActionType::XHR);
    }
  }

  if (uma_policy_)
    uma_policy_->ProcessAction(action);
  if (IsDatabaseEnabled() && database_policy_)
    database_policy_->ProcessAction(action);
  if (IsWatchdogAppActive())
    observers_->Notify(&Observer::OnExtensionActivity, action);
  if (testing_mode_)
    LOG(INFO) << action->PrintForDebug();
}

void ActivityLog::OnScriptsExecuted(
    const content::WebContents* web_contents,
    const ExecutingScriptsMap& extension_ids,
    int32 on_page_id,
    const GURL& on_url) {
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  const ExtensionService* extension_service =
      ExtensionSystem::Get(profile)->extension_service();
  const ExtensionSet* extensions = extension_service->extensions();
  const prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForProfile(
          Profile::FromBrowserContext(web_contents->GetBrowserContext()));

  for (ExecutingScriptsMap::const_iterator it = extension_ids.begin();
       it != extension_ids.end(); ++it) {
    const Extension* extension = extensions->GetByID(it->first);
    if (!extension || ActivityLogAPI::IsExtensionWhitelisted(extension->id()))
      continue;

    // If OnScriptsExecuted is fired because of tabs.executeScript, the list
    // of content scripts will be empty.  We don't want to log it because
    // the call to tabs.executeScript will have already been logged anyway.
    if (!it->second.empty()) {
      scoped_refptr<Action> action;
      action = new Action(extension->id(),
                          base::Time::Now(),
                          Action::ACTION_CONTENT_SCRIPT,
                          "");  // no API call here
      action->set_page_url(on_url);
      action->set_page_title(base::UTF16ToUTF8(web_contents->GetTitle()));
      action->set_page_incognito(
          web_contents->GetBrowserContext()->IsOffTheRecord());
      if (prerender_manager &&
          prerender_manager->IsWebContentsPrerendering(web_contents, NULL))
        action->mutable_other()->SetBoolean(constants::kActionPrerender, true);
      for (std::set<std::string>::const_iterator it2 = it->second.begin();
           it2 != it->second.end();
           ++it2) {
        action->mutable_args()->AppendString(*it2);
      }
      LogAction(action);
    }
  }
}

// LOOKUP ACTIONS. -------------------------------------------------------------

void ActivityLog::GetFilteredActions(
    const std::string& extension_id,
    const Action::ActionType type,
    const std::string& api_name,
    const std::string& page_url,
    const std::string& arg_url,
    const int daysAgo,
    const base::Callback
        <void(scoped_ptr<std::vector<scoped_refptr<Action> > >)>& callback) {
  if (database_policy_) {
    database_policy_->ReadFilteredData(
        extension_id, type, api_name, page_url, arg_url, daysAgo, callback);
  }
}

// DELETE ACTIONS. -------------------------------------------------------------

void ActivityLog::RemoveURLs(const std::vector<GURL>& restrict_urls) {
  if (!database_policy_)
    return;
  database_policy_->RemoveURLs(restrict_urls);
}

void ActivityLog::RemoveURLs(const std::set<GURL>& restrict_urls) {
  if (!database_policy_)
    return;

  std::vector<GURL> urls;
  for (std::set<GURL>::const_iterator it = restrict_urls.begin();
       it != restrict_urls.end(); ++it) {
    urls.push_back(*it);
  }
  database_policy_->RemoveURLs(urls);
}

void ActivityLog::RemoveURL(const GURL& url) {
  if (url.is_empty())
    return;
  std::vector<GURL> urls;
  urls.push_back(url);
  RemoveURLs(urls);
}

void ActivityLog::DeleteDatabase() {
  if (!database_policy_)
    return;
  database_policy_->DeleteDatabase();
}

}  // namespace extensions
