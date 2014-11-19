// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/browsing_data_remover.h"

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/guid.h"
#include "base/message_loop/message_loop.h"
#include "base/platform_file.h"
#include "base/prefs/testing_pref_service.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/chrome_notification_types.h"
#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/mock_user_manager.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#endif
#include "chrome/browser/extensions/mock_extension_special_storage_policy.h"
#include "chrome/browser/history/history_service.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#if defined(OS_CHROMEOS)
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_dbus_thread_manager.h"
#include "chromeos/dbus/mock_cryptohome_client.h"
#endif
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "content/public/browser/dom_storage_context.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "net/cookies/cookie_monster.h"
#include "net/ssl/server_bound_cert_service.h"
#include "net/ssl/server_bound_cert_store.h"
#include "net/ssl/ssl_client_cert_type.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::StoragePartition;
using testing::_;
using testing::Invoke;
using testing::WithArgs;

namespace {

const char kTestOrigin1[] = "http://host1:1/";
const char kTestOrigin2[] = "http://host2:1/";
const char kTestOrigin3[] = "http://host3:1/";
const char kTestOriginExt[] = "chrome-extension://abcdefghijklmnopqrstuvwxyz/";
const char kTestOriginDevTools[] = "chrome-devtools://abcdefghijklmnopqrstuvw/";

// For Autofill.
const char kChromeOrigin[] = "Chrome settings";
const char kWebOrigin[] = "https://www.example.com/";

const GURL kOrigin1(kTestOrigin1);
const GURL kOrigin2(kTestOrigin2);
const GURL kOrigin3(kTestOrigin3);
const GURL kOriginExt(kTestOriginExt);
const GURL kOriginDevTools(kTestOriginDevTools);

const base::FilePath::CharType kDomStorageOrigin1[] =
    FILE_PATH_LITERAL("http_host1_1.localstorage");

const base::FilePath::CharType kDomStorageOrigin2[] =
    FILE_PATH_LITERAL("http_host2_1.localstorage");

const base::FilePath::CharType kDomStorageOrigin3[] =
    FILE_PATH_LITERAL("http_host3_1.localstorage");

const base::FilePath::CharType kDomStorageExt[] = FILE_PATH_LITERAL(
    "chrome-extension_abcdefghijklmnopqrstuvwxyz_0.localstorage");

class AwaitCompletionHelper : public BrowsingDataRemover::Observer {
 public:
  AwaitCompletionHelper() : start_(false), already_quit_(false) {}
  virtual ~AwaitCompletionHelper() {}

  void BlockUntilNotified() {
    if (!already_quit_) {
      DCHECK(!start_);
      start_ = true;
      base::MessageLoop::current()->Run();
    } else {
      DCHECK(!start_);
      already_quit_ = false;
    }
  }

  void Notify() {
    if (start_) {
      DCHECK(!already_quit_);
      base::MessageLoop::current()->Quit();
      start_ = false;
    } else {
      DCHECK(!already_quit_);
      already_quit_ = true;
    }
  }

 protected:
  // BrowsingDataRemover::Observer implementation.
  virtual void OnBrowsingDataRemoverDone() OVERRIDE {
    Notify();
  }

 private:
  // Helps prevent from running message_loop, if the callback invoked
  // immediately.
  bool start_;
  bool already_quit_;

  DISALLOW_COPY_AND_ASSIGN(AwaitCompletionHelper);
};

#if defined(OS_CHROMEOS)
void FakeDBusCall(const chromeos::BoolDBusMethodCallback& callback) {
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(callback, chromeos::DBUS_METHOD_CALL_SUCCESS, true));
}
#endif

struct StoragePartitionRemovalData {
  uint32 remove_mask;
  uint32 quota_storage_remove_mask;
  GURL remove_origin;
  base::Time remove_begin;
  base::Time remove_end;
  StoragePartition::OriginMatcherFunction origin_matcher;

  StoragePartitionRemovalData() : remove_mask(0),
                                  quota_storage_remove_mask(0) {}
};

class TestStoragePartition : public StoragePartition {
 public:
  TestStoragePartition() {}
  virtual ~TestStoragePartition() {}

  // content::StoragePartition implementation.
  virtual base::FilePath GetPath() OVERRIDE { return base::FilePath(); }
  virtual net::URLRequestContextGetter* GetURLRequestContext() OVERRIDE {
    return NULL;
  }
  virtual net::URLRequestContextGetter* GetMediaURLRequestContext() OVERRIDE {
    return NULL;
  }
  virtual quota::QuotaManager* GetQuotaManager() OVERRIDE {
    return NULL;
  }
  virtual appcache::AppCacheService* GetAppCacheService() OVERRIDE {
    return NULL;
  }
  virtual fileapi::FileSystemContext* GetFileSystemContext() OVERRIDE {
    return NULL;
  }
  virtual webkit_database::DatabaseTracker* GetDatabaseTracker() OVERRIDE {
    return NULL;
  }
  virtual content::DOMStorageContext* GetDOMStorageContext() OVERRIDE {
    return NULL;
  }
  virtual content::IndexedDBContext* GetIndexedDBContext() OVERRIDE {
    return NULL;
  }

  virtual void ClearDataForOrigin(
      uint32 remove_mask,
      uint32 quota_storage_remove_mask,
      const GURL& storage_origin,
      net::URLRequestContextGetter* rq_context) OVERRIDE {}

  virtual void ClearData(uint32 remove_mask,
                         uint32 quota_storage_remove_mask,
                         const GURL* storage_origin,
                         const OriginMatcherFunction& origin_matcher,
                         const base::Time begin,
                         const base::Time end,
                         const base::Closure& callback) OVERRIDE {
    // Store stuff to verify parameters' correctness later.
    storage_partition_removal_data_.remove_mask = remove_mask;
    storage_partition_removal_data_.quota_storage_remove_mask =
        quota_storage_remove_mask;
    storage_partition_removal_data_.remove_origin =
        storage_origin ? *storage_origin : GURL();
    storage_partition_removal_data_.remove_begin = begin;
    storage_partition_removal_data_.remove_end = end;
    storage_partition_removal_data_.origin_matcher = origin_matcher;

    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&TestStoragePartition::AsyncRunCallback,
                   base::Unretained(this), callback));
  }

  StoragePartitionRemovalData GetStoragePartitionRemovalData() {
    return storage_partition_removal_data_;
  }
 private:
  void AsyncRunCallback(const base::Closure& callback) {
    callback.Run();
  }

  StoragePartitionRemovalData storage_partition_removal_data_;

  DISALLOW_COPY_AND_ASSIGN(TestStoragePartition);
};

}  // namespace

// Testers -------------------------------------------------------------------

class RemoveCookieTester {
 public:
  RemoveCookieTester() : get_cookie_success_(false), monster_(NULL) {
  }

  // Returns true, if the given cookie exists in the cookie store.
  bool ContainsCookie() {
    get_cookie_success_ = false;
    monster_->GetCookiesWithOptionsAsync(
        kOrigin1, net::CookieOptions(),
        base::Bind(&RemoveCookieTester::GetCookieCallback,
                   base::Unretained(this)));
    await_completion_.BlockUntilNotified();
    return get_cookie_success_;
  }

  void AddCookie() {
    monster_->SetCookieWithOptionsAsync(
        kOrigin1, "A=1", net::CookieOptions(),
        base::Bind(&RemoveCookieTester::SetCookieCallback,
                   base::Unretained(this)));
    await_completion_.BlockUntilNotified();
  }

 protected:
  void SetMonster(net::CookieStore* monster) {
    monster_ = monster;
  }

 private:
  void GetCookieCallback(const std::string& cookies) {
    if (cookies == "A=1") {
      get_cookie_success_ = true;
    } else {
      EXPECT_EQ("", cookies);
      get_cookie_success_ = false;
    }
    await_completion_.Notify();
  }

  void SetCookieCallback(bool result) {
    ASSERT_TRUE(result);
    await_completion_.Notify();
  }

  bool get_cookie_success_;
  AwaitCompletionHelper await_completion_;
  net::CookieStore* monster_;

  DISALLOW_COPY_AND_ASSIGN(RemoveCookieTester);
};

#if defined(FULL_SAFE_BROWSING) || defined(MOBILE_SAFE_BROWSING)
class RemoveSafeBrowsingCookieTester : public RemoveCookieTester {
 public:
  RemoveSafeBrowsingCookieTester()
      : browser_process_(TestingBrowserProcess::GetGlobal()) {
    scoped_refptr<SafeBrowsingService> sb_service =
        SafeBrowsingService::CreateSafeBrowsingService();
    browser_process_->SetSafeBrowsingService(sb_service.get());
    sb_service->Initialize();
    base::MessageLoop::current()->RunUntilIdle();

    // Create a cookiemonster that does not have persistant storage, and replace
    // the SafeBrowsingService created one with it.
    net::CookieStore* monster = new net::CookieMonster(NULL, NULL);
    sb_service->url_request_context()->GetURLRequestContext()->
        set_cookie_store(monster);
    SetMonster(monster);
  }

  virtual ~RemoveSafeBrowsingCookieTester() {
    browser_process_->safe_browsing_service()->ShutDown();
    base::MessageLoop::current()->RunUntilIdle();
    browser_process_->SetSafeBrowsingService(NULL);
  }

 private:
  TestingBrowserProcess* browser_process_;

  DISALLOW_COPY_AND_ASSIGN(RemoveSafeBrowsingCookieTester);
};
#endif

class RemoveServerBoundCertTester : public net::SSLConfigService::Observer {
 public:
  explicit RemoveServerBoundCertTester(TestingProfile* profile)
      : ssl_config_changed_count_(0) {
    server_bound_cert_service_ = profile->GetRequestContext()->
        GetURLRequestContext()->server_bound_cert_service();
    ssl_config_service_ = profile->GetSSLConfigService();
    ssl_config_service_->AddObserver(this);
  }

  virtual ~RemoveServerBoundCertTester() {
    ssl_config_service_->RemoveObserver(this);
  }

  int ServerBoundCertCount() {
    return server_bound_cert_service_->cert_count();
  }

  // Add a server bound cert for |server| with specific creation and expiry
  // times.  The cert and key data will be filled with dummy values.
  void AddServerBoundCertWithTimes(const std::string& server_identifier,
                                   base::Time creation_time,
                                   base::Time expiration_time) {
    GetCertStore()->SetServerBoundCert(server_identifier,
                                       creation_time,
                                       expiration_time,
                                       "a",
                                       "b");
  }

  // Add a server bound cert for |server|, with the current time as the
  // creation time.  The cert and key data will be filled with dummy values.
  void AddServerBoundCert(const std::string& server_identifier) {
    base::Time now = base::Time::Now();
    AddServerBoundCertWithTimes(server_identifier,
                                now,
                                now + base::TimeDelta::FromDays(1));
  }

  void GetCertList(net::ServerBoundCertStore::ServerBoundCertList* certs) {
    GetCertStore()->GetAllServerBoundCerts(
        base::Bind(&RemoveServerBoundCertTester::GetAllCertsCallback, certs));
  }

  net::ServerBoundCertStore* GetCertStore() {
    return server_bound_cert_service_->GetCertStore();
  }

  int ssl_config_changed_count() const {
    return ssl_config_changed_count_;
  }

  // net::SSLConfigService::Observer implementation:
  virtual void OnSSLConfigChanged() OVERRIDE {
    ssl_config_changed_count_++;
  }

 private:
  static void GetAllCertsCallback(
      net::ServerBoundCertStore::ServerBoundCertList* dest,
      const net::ServerBoundCertStore::ServerBoundCertList& result) {
    *dest = result;
  }

  net::ServerBoundCertService* server_bound_cert_service_;
  scoped_refptr<net::SSLConfigService> ssl_config_service_;
  int ssl_config_changed_count_;

  DISALLOW_COPY_AND_ASSIGN(RemoveServerBoundCertTester);
};

class RemoveHistoryTester {
 public:
  RemoveHistoryTester() : query_url_success_(false), history_service_(NULL) {}

  bool Init(TestingProfile* profile) WARN_UNUSED_RESULT {
    if (!profile->CreateHistoryService(true, false))
      return false;
    history_service_ = HistoryServiceFactory::GetForProfile(
        profile, Profile::EXPLICIT_ACCESS);
    return true;
  }

  // Returns true, if the given URL exists in the history service.
  bool HistoryContainsURL(const GURL& url) {
    history_service_->QueryURL(
        url,
        true,
        &consumer_,
        base::Bind(&RemoveHistoryTester::SaveResultAndQuit,
                   base::Unretained(this)));
    await_completion_.BlockUntilNotified();
    return query_url_success_;
  }

  void AddHistory(const GURL& url, base::Time time) {
    history_service_->AddPage(url, time, NULL, 0, GURL(),
        history::RedirectList(), content::PAGE_TRANSITION_LINK,
        history::SOURCE_BROWSED, false);
  }

 private:
  // Callback for HistoryService::QueryURL.
  void SaveResultAndQuit(HistoryService::Handle,
                         bool success,
                         const history::URLRow*,
                         history::VisitVector*) {
    query_url_success_ = success;
    await_completion_.Notify();
  }

  // For History requests.
  CancelableRequestConsumer consumer_;
  bool query_url_success_;

  // TestingProfile owns the history service; we shouldn't delete it.
  HistoryService* history_service_;

  AwaitCompletionHelper await_completion_;

  DISALLOW_COPY_AND_ASSIGN(RemoveHistoryTester);
};

class RemoveAutofillTester : public autofill::PersonalDataManagerObserver {
 public:
  explicit RemoveAutofillTester(TestingProfile* profile)
      : personal_data_manager_(
            autofill::PersonalDataManagerFactory::GetForProfile(profile)) {
        autofill::test::DisableSystemServices(profile);
    personal_data_manager_->AddObserver(this);
  }

  virtual ~RemoveAutofillTester() {
    personal_data_manager_->RemoveObserver(this);
  }

  // Returns true if there are autofill profiles.
  bool HasProfile() {
    return !personal_data_manager_->GetProfiles().empty() &&
           !personal_data_manager_->GetCreditCards().empty();
  }

  bool HasOrigin(const std::string& origin) {
    const std::vector<autofill::AutofillProfile*>& profiles =
        personal_data_manager_->GetProfiles();
    for (std::vector<autofill::AutofillProfile*>::const_iterator it =
             profiles.begin();
         it != profiles.end(); ++it) {
      if ((*it)->origin() == origin)
        return true;
    }

    const std::vector<autofill::CreditCard*>& credit_cards =
        personal_data_manager_->GetCreditCards();
    for (std::vector<autofill::CreditCard*>::const_iterator it =
             credit_cards.begin();
         it != credit_cards.end(); ++it) {
      if ((*it)->origin() == origin)
        return true;
    }

    return false;
  }

  // Add two profiles and two credit cards to the database.  In each pair, one
  // entry has a web origin and the other has a Chrome origin.
  void AddProfilesAndCards() {
    std::vector<autofill::AutofillProfile> profiles;
    autofill::AutofillProfile profile;
    profile.set_guid(base::GenerateGUID());
    profile.set_origin(kWebOrigin);
    profile.SetRawInfo(autofill::NAME_FIRST, ASCIIToUTF16("Bob"));
    profile.SetRawInfo(autofill::NAME_LAST, ASCIIToUTF16("Smith"));
    profile.SetRawInfo(autofill::ADDRESS_HOME_ZIP, ASCIIToUTF16("94043"));
    profile.SetRawInfo(autofill::EMAIL_ADDRESS,
                       ASCIIToUTF16("sue@example.com"));
    profile.SetRawInfo(autofill::COMPANY_NAME, ASCIIToUTF16("Company X"));
    profiles.push_back(profile);

    profile.set_guid(base::GenerateGUID());
    profile.set_origin(kChromeOrigin);
    profiles.push_back(profile);

    personal_data_manager_->SetProfiles(&profiles);
    base::MessageLoop::current()->Run();

    std::vector<autofill::CreditCard> cards;
    autofill::CreditCard card;
    card.set_guid(base::GenerateGUID());
    card.set_origin(kWebOrigin);
    card.SetRawInfo(autofill::CREDIT_CARD_NUMBER,
                    ASCIIToUTF16("1234-5678-9012-3456"));
    cards.push_back(card);

    card.set_guid(base::GenerateGUID());
    card.set_origin(kChromeOrigin);
    cards.push_back(card);

    personal_data_manager_->SetCreditCards(&cards);
    base::MessageLoop::current()->Run();
  }

 private:
  virtual void OnPersonalDataChanged() OVERRIDE {
    base::MessageLoop::current()->Quit();
  }

  autofill::PersonalDataManager* personal_data_manager_;
  DISALLOW_COPY_AND_ASSIGN(RemoveAutofillTester);
};

class RemoveLocalStorageTester {
 public:
  explicit RemoveLocalStorageTester(TestingProfile* profile)
      : profile_(profile), dom_storage_context_(NULL) {
    dom_storage_context_ =
        content::BrowserContext::GetDefaultStoragePartition(profile)->
            GetDOMStorageContext();
  }

  // Returns true, if the given origin URL exists.
  bool DOMStorageExistsForOrigin(const GURL& origin) {
    GetLocalStorageUsage();
    await_completion_.BlockUntilNotified();
    for (size_t i = 0; i < infos_.size(); ++i) {
      if (origin == infos_[i].origin)
        return true;
    }
    return false;
  }

  void AddDOMStorageTestData() {
    // Note: This test depends on details of how the dom_storage library
    // stores data in the host file system.
    base::FilePath storage_path =
        profile_->GetPath().AppendASCII("Local Storage");
    file_util::CreateDirectory(storage_path);

    // Write some files.
    file_util::WriteFile(storage_path.Append(kDomStorageOrigin1), NULL, 0);
    file_util::WriteFile(storage_path.Append(kDomStorageOrigin2), NULL, 0);
    file_util::WriteFile(storage_path.Append(kDomStorageOrigin3), NULL, 0);
    file_util::WriteFile(storage_path.Append(kDomStorageExt), NULL, 0);

    // Tweak their dates.
    file_util::SetLastModifiedTime(storage_path.Append(kDomStorageOrigin1),
        base::Time::Now());
    file_util::SetLastModifiedTime(storage_path.Append(kDomStorageOrigin2),
        base::Time::Now() - base::TimeDelta::FromDays(1));
    file_util::SetLastModifiedTime(storage_path.Append(kDomStorageOrigin3),
        base::Time::Now() - base::TimeDelta::FromDays(60));
    file_util::SetLastModifiedTime(storage_path.Append(kDomStorageExt),
        base::Time::Now());
  }

 private:
  void GetLocalStorageUsage() {
    dom_storage_context_->GetLocalStorageUsage(
        base::Bind(&RemoveLocalStorageTester::OnGotLocalStorageUsage,
                   base::Unretained(this)));
  }
  void OnGotLocalStorageUsage(
      const std::vector<content::LocalStorageUsageInfo>& infos) {
    infos_ = infos;
    await_completion_.Notify();
  }

  // We don't own these pointers.
  TestingProfile* profile_;
  content::DOMStorageContext* dom_storage_context_;

  std::vector<content::LocalStorageUsageInfo> infos_;

  AwaitCompletionHelper await_completion_;

  DISALLOW_COPY_AND_ASSIGN(RemoveLocalStorageTester);
};

// Test Class ----------------------------------------------------------------

class BrowsingDataRemoverTest : public testing::Test,
                                public content::NotificationObserver {
 public:
  BrowsingDataRemoverTest()
      : profile_(new TestingProfile()) {
    registrar_.Add(this, chrome::NOTIFICATION_BROWSING_DATA_REMOVED,
                   content::Source<Profile>(profile_.get()));
  }

  virtual ~BrowsingDataRemoverTest() {
  }

  virtual void TearDown() {
    // TestingProfile contains a DOMStorageContext.  BrowserContext's destructor
    // posts a message to the WEBKIT thread to delete some of its member
    // variables. We need to ensure that the profile is destroyed, and that
    // the message loop is cleared out, before destroying the threads and loop.
    // Otherwise we leak memory.
    profile_.reset();
    base::MessageLoop::current()->RunUntilIdle();
  }

  void BlockUntilBrowsingDataRemoved(BrowsingDataRemover::TimePeriod period,
                                     int remove_mask,
                                     bool include_protected_origins) {
    BrowsingDataRemover* remover = BrowsingDataRemover::CreateForPeriod(
        profile_.get(), period);

    TestStoragePartition storage_partition;
    remover->OverrideStoragePartitionForTesting(&storage_partition);

    AwaitCompletionHelper await_completion;
    remover->AddObserver(&await_completion);

    called_with_details_.reset(new BrowsingDataRemover::NotificationDetails());

    // BrowsingDataRemover deletes itself when it completes.
    int origin_set_mask = BrowsingDataHelper::UNPROTECTED_WEB;
    if (include_protected_origins)
      origin_set_mask |= BrowsingDataHelper::PROTECTED_WEB;
    remover->Remove(remove_mask, origin_set_mask);
    await_completion.BlockUntilNotified();

    // Save so we can verify later.
    storage_partition_removal_data_ =
        storage_partition.GetStoragePartitionRemovalData();
  }

  void BlockUntilOriginDataRemoved(BrowsingDataRemover::TimePeriod period,
                                   int remove_mask,
                                   const GURL& remove_origin) {
    BrowsingDataRemover* remover = BrowsingDataRemover::CreateForPeriod(
        profile_.get(), period);
    TestStoragePartition storage_partition;
    remover->OverrideStoragePartitionForTesting(&storage_partition);

    AwaitCompletionHelper await_completion;
    remover->AddObserver(&await_completion);

    called_with_details_.reset(new BrowsingDataRemover::NotificationDetails());

    // BrowsingDataRemover deletes itself when it completes.
    remover->RemoveImpl(remove_mask, remove_origin,
        BrowsingDataHelper::UNPROTECTED_WEB);
    await_completion.BlockUntilNotified();

    // Save so we can verify later.
    storage_partition_removal_data_ =
        storage_partition.GetStoragePartitionRemovalData();
  }

  TestingProfile* GetProfile() {
    return profile_.get();
  }

  base::Time GetBeginTime() {
    return called_with_details_->removal_begin;
  }

  int GetRemovalMask() {
    return called_with_details_->removal_mask;
  }

  int GetOriginSetMask() {
    return called_with_details_->origin_set_mask;
  }

  StoragePartitionRemovalData GetStoragePartitionRemovalData() {
    return storage_partition_removal_data_;
  }

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    DCHECK_EQ(type, chrome::NOTIFICATION_BROWSING_DATA_REMOVED);

    // We're not taking ownership of the details object, but storing a copy of
    // it locally.
    called_with_details_.reset(new BrowsingDataRemover::NotificationDetails(
        *content::Details<BrowsingDataRemover::NotificationDetails>(
            details).ptr()));

    registrar_.RemoveAll();
  }

 private:
  scoped_ptr<BrowsingDataRemover::NotificationDetails> called_with_details_;
  content::NotificationRegistrar registrar_;

  content::TestBrowserThreadBundle thread_bundle_;
  scoped_ptr<TestingProfile> profile_;

  StoragePartitionRemovalData storage_partition_removal_data_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataRemoverTest);
};

// Tests ---------------------------------------------------------------------

TEST_F(BrowsingDataRemoverTest, RemoveCookieForever) {
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_COOKIES,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_COOKIES, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify that storage partition was instructed to remove the cookies.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_COOKIES));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());
}

TEST_F(BrowsingDataRemoverTest, RemoveCookieLastHour) {
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
                                BrowsingDataRemover::REMOVE_COOKIES,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_COOKIES, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify that storage partition was instructed to remove the cookies.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_COOKIES));
  // Removing with time period other than EVERYTHING should not clear
  // persistent storage data.
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL &
                ~StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());
}

#if defined(FULL_SAFE_BROWSING) || defined(MOBILE_SAFE_BROWSING)
TEST_F(BrowsingDataRemoverTest, RemoveSafeBrowsingCookieForever) {
  RemoveSafeBrowsingCookieTester tester;

  tester.AddCookie();
  ASSERT_TRUE(tester.ContainsCookie());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_COOKIES, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_COOKIES, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_FALSE(tester.ContainsCookie());
}

TEST_F(BrowsingDataRemoverTest, RemoveSafeBrowsingCookieLastHour) {
  RemoveSafeBrowsingCookieTester tester;

  tester.AddCookie();
  ASSERT_TRUE(tester.ContainsCookie());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_COOKIES, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_COOKIES, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  // Removing with time period other than EVERYTHING should not clear safe
  // browsing cookies.
  EXPECT_TRUE(tester.ContainsCookie());
}
#endif

TEST_F(BrowsingDataRemoverTest, RemoveServerBoundCertForever) {
  RemoveServerBoundCertTester tester(GetProfile());

  tester.AddServerBoundCert(kTestOrigin1);
  EXPECT_EQ(0, tester.ssl_config_changed_count());
  EXPECT_EQ(1, tester.ServerBoundCertCount());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_SERVER_BOUND_CERTS, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_SERVER_BOUND_CERTS, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_EQ(1, tester.ssl_config_changed_count());
  EXPECT_EQ(0, tester.ServerBoundCertCount());
}

TEST_F(BrowsingDataRemoverTest, RemoveServerBoundCertLastHour) {
  RemoveServerBoundCertTester tester(GetProfile());

  base::Time now = base::Time::Now();
  tester.AddServerBoundCert(kTestOrigin1);
  tester.AddServerBoundCertWithTimes(kTestOrigin2,
                                     now - base::TimeDelta::FromHours(2),
                                     now);
  EXPECT_EQ(0, tester.ssl_config_changed_count());
  EXPECT_EQ(2, tester.ServerBoundCertCount());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_SERVER_BOUND_CERTS, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_SERVER_BOUND_CERTS, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_EQ(1, tester.ssl_config_changed_count());
  ASSERT_EQ(1, tester.ServerBoundCertCount());
  net::ServerBoundCertStore::ServerBoundCertList certs;
  tester.GetCertList(&certs);
  ASSERT_EQ(1U, certs.size());
  EXPECT_EQ(kTestOrigin2, certs.front().server_identifier());
}

TEST_F(BrowsingDataRemoverTest, RemoveUnprotectedLocalStorageForever) {
  // Protect kOrigin1.
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  mock_policy->AddProtected(kOrigin1.GetOrigin());
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_LOCAL_STORAGE,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_LOCAL_STORAGE, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify that storage partition was instructed to remove the data correctly.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());

  // Check origin matcher.
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOriginExt, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveProtectedLocalStorageForever) {
  // Protect kOrigin1.
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  mock_policy->AddProtected(kOrigin1.GetOrigin());
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_LOCAL_STORAGE,
                                true);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_LOCAL_STORAGE, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB |
            BrowsingDataHelper::PROTECTED_WEB, GetOriginSetMask());

  // Verify that storage partition was instructed to remove the data correctly.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());

  // Check origin matcher all http origin will match since we specified
  // both protected and unprotected.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOriginExt, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveLocalStorageForLastWeek) {
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_WEEK,
                                BrowsingDataRemover::REMOVE_LOCAL_STORAGE,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_LOCAL_STORAGE, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify that storage partition was instructed to remove the data correctly.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE));
  // Persistent storage won't be deleted.
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL &
                ~StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());

  // Check origin matcher.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOriginExt, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveHistoryForever) {
  RemoveHistoryTester tester;
  ASSERT_TRUE(tester.Init(GetProfile()));

  tester.AddHistory(kOrigin1, base::Time::Now());
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin1));

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_HISTORY, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_FALSE(tester.HistoryContainsURL(kOrigin1));
}

TEST_F(BrowsingDataRemoverTest, RemoveHistoryForLastHour) {
  RemoveHistoryTester tester;
  ASSERT_TRUE(tester.Init(GetProfile()));

  base::Time two_hours_ago = base::Time::Now() - base::TimeDelta::FromHours(2);

  tester.AddHistory(kOrigin1, base::Time::Now());
  tester.AddHistory(kOrigin2, two_hours_ago);
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin1));
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin2));

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_HISTORY, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_FALSE(tester.HistoryContainsURL(kOrigin1));
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin2));
}

// This should crash (DCHECK) in Debug, but death tests don't work properly
// here.
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
TEST_F(BrowsingDataRemoverTest, RemoveHistoryProhibited) {
  RemoveHistoryTester tester;
  ASSERT_TRUE(tester.Init(GetProfile()));
  PrefService* prefs = GetProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kAllowDeletingBrowserHistory, false);

  base::Time two_hours_ago = base::Time::Now() - base::TimeDelta::FromHours(2);

  tester.AddHistory(kOrigin1, base::Time::Now());
  tester.AddHistory(kOrigin2, two_hours_ago);
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin1));
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin2));

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_HISTORY, false);
  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Nothing should have been deleted.
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin1));
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin2));
}
#endif

TEST_F(BrowsingDataRemoverTest, RemoveMultipleTypes) {
  // Add some history.
  RemoveHistoryTester history_tester;
  ASSERT_TRUE(history_tester.Init(GetProfile()));
  history_tester.AddHistory(kOrigin1, base::Time::Now());
  ASSERT_TRUE(history_tester.HistoryContainsURL(kOrigin1));

  int removal_mask = BrowsingDataRemover::REMOVE_HISTORY |
                     BrowsingDataRemover::REMOVE_COOKIES;

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
      removal_mask, false);

  EXPECT_EQ(removal_mask, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_FALSE(history_tester.HistoryContainsURL(kOrigin1));

  // The cookie would be deleted throught the StorageParition, check if the
  // partition was requested to remove cookie.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_COOKIES));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
}

// This should crash (DCHECK) in Debug, but death tests don't work properly
// here.
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
TEST_F(BrowsingDataRemoverTest, RemoveMultipleTypesHistoryProhibited) {
  PrefService* prefs = GetProfile()->GetPrefs();
  prefs->SetBoolean(prefs::kAllowDeletingBrowserHistory, false);

  // Add some history.
  RemoveHistoryTester history_tester;
  ASSERT_TRUE(history_tester.Init(GetProfile()));
  history_tester.AddHistory(kOrigin1, base::Time::Now());
  ASSERT_TRUE(history_tester.HistoryContainsURL(kOrigin1));

  int removal_mask = BrowsingDataRemover::REMOVE_HISTORY |
                     BrowsingDataRemover::REMOVE_COOKIES;

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
                                removal_mask, false);
  EXPECT_EQ(removal_mask, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // 1/2. History should remain.
  EXPECT_TRUE(history_tester.HistoryContainsURL(kOrigin1));

  // 2/2. The cookie(s) would be deleted throught the StorageParition, check if
  // the partition was requested to remove cookie.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_COOKIES));
  // Persistent storage won't be deleted, since EVERYTHING was not specified.
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL &
                ~StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT));
}
#endif

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForeverBoth) {
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();
  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForeverOnlyTemporary) {
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check that all related origin data would be removed, that is, origin
  // matcher would match these origin.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForeverOnlyPersistent) {
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check that all related origin data would be removed, that is, origin
  // matcher would match these origin.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForeverNeither) {
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());


  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check that all related origin data would be removed, that is, origin
  // matcher would match these origin.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForeverSpecificOrigin) {
  // Remove Origin 1.
  BlockUntilOriginDataRemoved(BrowsingDataRemover::EVERYTHING,
                              BrowsingDataRemover::REMOVE_APPCACHE |
                              BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                              BrowsingDataRemover::REMOVE_INDEXEDDB |
                              BrowsingDataRemover::REMOVE_WEBSQL,
                              kOrigin1);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_INDEXEDDB |
      BrowsingDataRemover::REMOVE_WEBSQL, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_EQ(removal_data.remove_origin, kOrigin1);
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForLastHour) {
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_HOUR,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));

  // Persistent data would be left out since we are not removing from
  // beginning of time.
  uint32 expected_quota_mask =
      ~StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT;
  EXPECT_EQ(removal_data.quota_storage_remove_mask, expected_quota_mask);
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  // Check removal begin time.
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedDataForLastWeek) {
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::LAST_WEEK,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));

  // Persistent data would be left out since we are not removing from
  // beginning of time.
  uint32 expected_quota_mask =
      ~StoragePartition::QUOTA_MANAGED_STORAGE_MASK_PERSISTENT;
  EXPECT_EQ(removal_data.quota_storage_remove_mask, expected_quota_mask);
  EXPECT_TRUE(removal_data.remove_origin.is_empty());
  // Check removal begin time.
  EXPECT_EQ(removal_data.remove_begin, GetBeginTime());
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedUnprotectedOrigins) {
  // Protect kOrigin1.
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  mock_policy->AddProtected(kOrigin1.GetOrigin());
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_WEBSQL |
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_INDEXEDDB,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_WEBSQL |
      BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_INDEXEDDB, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check OriginMatcherFunction, |kOrigin1| would not match mask since it
  // is protected.
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedProtectedSpecificOrigin) {
  // Protect kOrigin1.
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  mock_policy->AddProtected(kOrigin1.GetOrigin());
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  // Try to remove kOrigin1. Expect failure.
  BlockUntilOriginDataRemoved(BrowsingDataRemover::EVERYTHING,
                              BrowsingDataRemover::REMOVE_APPCACHE |
                              BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                              BrowsingDataRemover::REMOVE_INDEXEDDB |
                              BrowsingDataRemover::REMOVE_WEBSQL,
                              kOrigin1);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_INDEXEDDB |
      BrowsingDataRemover::REMOVE_WEBSQL, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_EQ(removal_data.remove_origin, kOrigin1);

  // Check OriginMatcherFunction, |kOrigin1| would not match mask since it
  // is protected.
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedProtectedOrigins) {
  // Protect kOrigin1.
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  mock_policy->AddProtected(kOrigin1.GetOrigin());
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  // Try to remove kOrigin1. Expect success.
  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_INDEXEDDB |
                                BrowsingDataRemover::REMOVE_WEBSQL,
                                true);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_INDEXEDDB |
      BrowsingDataRemover::REMOVE_WEBSQL, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::PROTECTED_WEB |
      BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check OriginMatcherFunction, |kOrigin1| would match mask since we
  // would have 'protected' specified in origin_set_mask.
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin1, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin2, mock_policy));
  EXPECT_TRUE(removal_data.origin_matcher.Run(kOrigin3, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, RemoveQuotaManagedIgnoreExtensionsAndDevTools) {
  scoped_refptr<MockExtensionSpecialStoragePolicy> mock_policy =
      new MockExtensionSpecialStoragePolicy;
  GetProfile()->SetExtensionSpecialStoragePolicy(mock_policy.get());

  BlockUntilBrowsingDataRemoved(BrowsingDataRemover::EVERYTHING,
                                BrowsingDataRemover::REMOVE_APPCACHE |
                                BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
                                BrowsingDataRemover::REMOVE_INDEXEDDB |
                                BrowsingDataRemover::REMOVE_WEBSQL,
                                false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_APPCACHE |
      BrowsingDataRemover::REMOVE_FILE_SYSTEMS |
      BrowsingDataRemover::REMOVE_INDEXEDDB |
      BrowsingDataRemover::REMOVE_WEBSQL, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Verify storage partition related stuffs.
  StoragePartitionRemovalData removal_data = GetStoragePartitionRemovalData();

  EXPECT_EQ(removal_data.remove_mask,
            static_cast<uint32>(
                StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS |
                StoragePartition::REMOVE_DATA_MASK_WEBSQL |
                StoragePartition::REMOVE_DATA_MASK_APPCACHE |
                StoragePartition::REMOVE_DATA_MASK_INDEXEDDB));
  EXPECT_EQ(removal_data.quota_storage_remove_mask,
            static_cast<uint32>(
                StoragePartition::QUOTA_MANAGED_STORAGE_MASK_ALL));
  EXPECT_TRUE(removal_data.remove_origin.is_empty());

  // Check that extension and devtools data wouldn't be removed, that is,
  // origin matcher would not match these origin.
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOriginExt, mock_policy));
  EXPECT_FALSE(removal_data.origin_matcher.Run(kOriginDevTools, mock_policy));
}

TEST_F(BrowsingDataRemoverTest, OriginBasedHistoryRemoval) {
  RemoveHistoryTester tester;
  ASSERT_TRUE(tester.Init(GetProfile()));

  base::Time two_hours_ago = base::Time::Now() - base::TimeDelta::FromHours(2);

  tester.AddHistory(kOrigin1, base::Time::Now());
  tester.AddHistory(kOrigin2, two_hours_ago);
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin1));
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin2));

  BlockUntilOriginDataRemoved(BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_HISTORY, kOrigin2);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());

  // Nothing should have been deleted.
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin1));
  EXPECT_FALSE(tester.HistoryContainsURL(kOrigin2));
}

TEST_F(BrowsingDataRemoverTest, OriginAndTimeBasedHistoryRemoval) {
  RemoveHistoryTester tester;
  ASSERT_TRUE(tester.Init(GetProfile()));

  base::Time two_hours_ago = base::Time::Now() - base::TimeDelta::FromHours(2);

  tester.AddHistory(kOrigin1, base::Time::Now());
  tester.AddHistory(kOrigin2, two_hours_ago);
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin1));
  ASSERT_TRUE(tester.HistoryContainsURL(kOrigin2));

  BlockUntilOriginDataRemoved(BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_HISTORY, kOrigin2);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin1));
  EXPECT_TRUE(tester.HistoryContainsURL(kOrigin2));
}

// Verify that clearing autofill form data works.
TEST_F(BrowsingDataRemoverTest, AutofillRemovalLastHour) {
  GetProfile()->CreateWebDataService();
  RemoveAutofillTester tester(GetProfile());

  ASSERT_FALSE(tester.HasProfile());
  tester.AddProfilesAndCards();
  ASSERT_TRUE(tester.HasProfile());

  BlockUntilBrowsingDataRemoved(
      BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_FORM_DATA, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FORM_DATA, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  ASSERT_FALSE(tester.HasProfile());
}

TEST_F(BrowsingDataRemoverTest, AutofillRemovalEverything) {
  GetProfile()->CreateWebDataService();
  RemoveAutofillTester tester(GetProfile());

  ASSERT_FALSE(tester.HasProfile());
  tester.AddProfilesAndCards();
  ASSERT_TRUE(tester.HasProfile());

  BlockUntilBrowsingDataRemoved(
      BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_FORM_DATA, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_FORM_DATA, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  ASSERT_FALSE(tester.HasProfile());
}

// Verify that clearing autofill form data works.
TEST_F(BrowsingDataRemoverTest, AutofillOriginsRemovedWithHistory) {
  GetProfile()->CreateWebDataService();
  RemoveAutofillTester tester(GetProfile());

  tester.AddProfilesAndCards();
  EXPECT_FALSE(tester.HasOrigin(std::string()));
  EXPECT_TRUE(tester.HasOrigin(kWebOrigin));
  EXPECT_TRUE(tester.HasOrigin(kChromeOrigin));

  BlockUntilBrowsingDataRemoved(
      BrowsingDataRemover::LAST_HOUR,
      BrowsingDataRemover::REMOVE_HISTORY, false);

  EXPECT_EQ(BrowsingDataRemover::REMOVE_HISTORY, GetRemovalMask());
  EXPECT_EQ(BrowsingDataHelper::UNPROTECTED_WEB, GetOriginSetMask());
  EXPECT_TRUE(tester.HasOrigin(std::string()));
  EXPECT_FALSE(tester.HasOrigin(kWebOrigin));
  EXPECT_TRUE(tester.HasOrigin(kChromeOrigin));
}

#if defined(OS_CHROMEOS)
TEST_F(BrowsingDataRemoverTest, ContentProtectionPlatformKeysRemoval) {
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service;
  chromeos::ScopedTestCrosSettings test_cros_settings;
  chromeos::MockUserManager* mock_user_manager =
      new testing::NiceMock<chromeos::MockUserManager>();
  mock_user_manager->SetActiveUser("test@example.com");
  chromeos::ScopedUserManagerEnabler user_manager_enabler(mock_user_manager);

  chromeos::FakeDBusThreadManager* fake_dbus_manager =
      new chromeos::FakeDBusThreadManager;
  chromeos::MockCryptohomeClient* cryptohome_client =
      new chromeos::MockCryptohomeClient;
  fake_dbus_manager->SetCryptohomeClient(
      scoped_ptr<chromeos::CryptohomeClient>(cryptohome_client));
  chromeos::DBusThreadManager::InitializeForTesting(fake_dbus_manager);

  // Expect exactly one call.  No calls means no attempt to delete keys and more
  // than one call means a significant performance problem.
  EXPECT_CALL(*cryptohome_client, TpmAttestationDeleteKeys(_, _, _, _))
      .WillOnce(WithArgs<3>(Invoke(FakeDBusCall)));

  BlockUntilBrowsingDataRemoved(
      BrowsingDataRemover::EVERYTHING,
      BrowsingDataRemover::REMOVE_CONTENT_LICENSES, false);
}
#endif
