// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/chrome_download_manager_delegate.h"

#include <string>

#include "base/basictypes.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/prefs/pref_member.h"
#include "base/prefs/pref_service.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/download_completion_blocker.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/download/download_file_picker.h"
#include "chrome/browser/download/download_history.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_path_reservation_tracker.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_service.h"
#include "chrome/browser/download/download_service_factory.h"
#include "chrome/browser/download/download_stats.h"
#include "chrome/browser/download/download_target_determiner.h"
#include "chrome/browser/download/save_package_file_picker.h"
#include "chrome/browser/extensions/api/downloads/downloads_api.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "components/user_prefs/pref_registry_syncable.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/page_navigator.h"
#include "extensions/common/constants.h"
#include "net/base/mime_util.h"
#include "net/base/net_util.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/drive/download_handler.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#endif

using content::BrowserThread;
using content::DownloadItem;
using content::DownloadManager;
using safe_browsing::DownloadProtectionService;

namespace {

#if defined(FULL_SAFE_BROWSING)

// String pointer used for identifying safebrowing data associated with
// a download item.
const char kSafeBrowsingUserDataKey[] = "Safe Browsing ID";

// The state of a safebrowsing check.
class SafeBrowsingState : public DownloadCompletionBlocker {
 public:
  SafeBrowsingState()
    : verdict_(DownloadProtectionService::SAFE) {
  }

  virtual ~SafeBrowsingState();

  // The verdict that we got from calling CheckClientDownload. Only valid to
  // call if |is_complete()|.
  DownloadProtectionService::DownloadCheckResult verdict() const {
    return verdict_;
  }

  void SetVerdict(DownloadProtectionService::DownloadCheckResult result) {
    verdict_ = result;
    CompleteDownload();
  }

 private:
  DownloadProtectionService::DownloadCheckResult verdict_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingState);
};

SafeBrowsingState::~SafeBrowsingState() {}

#endif  // FULL_SAFE_BROWSING

// Used with GetPlatformDownloadPath() to indicate which platform path to
// return.
enum PlatformDownloadPathType {
  // Return the platform specific target path.
  PLATFORM_TARGET_PATH,

  // Return the platform specific current path. If the download is in-progress
  // and the download location is a local filesystem path, then
  // GetPlatformDownloadPath will return the path to the intermediate file.
  PLATFORM_CURRENT_PATH
};

// Returns a path in the form that that is expected by platform_util::OpenItem /
// platform_util::ShowItemInFolder / DownloadTargetDeterminer.
//
// DownloadItems corresponding to Drive downloads use a temporary file as the
// target path. The paths returned by DownloadItem::GetFullPath() /
// GetTargetFilePath() refer to this temporary file. This function looks up the
// corresponding path in Drive for these downloads.
//
// How the platform path is determined is based on PlatformDownloadPathType.
base::FilePath GetPlatformDownloadPath(Profile* profile,
                                       const DownloadItem* download,
                                       PlatformDownloadPathType path_type) {
#if defined(OS_CHROMEOS)
  // Drive downloads always return the target path for all types.
  drive::DownloadHandler* drive_download_handler =
      drive::DownloadHandler::GetForProfile(profile);
  if (drive_download_handler &&
      drive_download_handler->IsDriveDownload(download))
    return drive_download_handler->GetTargetPath(download);
#endif

  if (path_type == PLATFORM_TARGET_PATH)
    return download->GetTargetFilePath();
  return download->GetFullPath();
}

#if defined(FULL_SAFE_BROWSING)
// Callback invoked by DownloadProtectionService::CheckClientDownload.
// |is_content_check_supported| is true if the SB service supports scanning the
// download for malicious content.
// |callback| is invoked with a danger type determined as follows:
//
// Danger type is (in order of preference):
//   * DANGEROUS_URL, if the URL is a known malware site.
//   * MAYBE_DANGEROUS_CONTENT, if the content will be scanned for
//         malware. I.e. |is_content_check_supported| is true.
//   * NOT_DANGEROUS.
void CheckDownloadUrlDone(
    const DownloadTargetDeterminerDelegate::CheckDownloadUrlCallback& callback,
    bool is_content_check_supported,
    DownloadProtectionService::DownloadCheckResult result) {
  content::DownloadDangerType danger_type;
  if (result == DownloadProtectionService::SAFE) {
    // If this type of files is handled by the enhanced SafeBrowsing download
    // protection, mark it as potentially dangerous content until we are done
    // with scanning it.
    if (is_content_check_supported)
      danger_type = content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT;
    else
      danger_type = content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS;
  } else {
    // If the URL is malicious, we'll use that as the danger type. The results
    // of the content check, if one is performed, will be ignored.
    danger_type = content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL;
  }
  callback.Run(danger_type);
}

#endif  // FULL_SAFE_BROWSING

// Called on the blocking pool to determine the MIME type for |path|.
void GetMimeTypeAndReplyOnUIThread(
    const base::FilePath& path,
    const base::Callback<void(const std::string&)>& callback) {
  std::string mime_type;
  net::GetMimeTypeFromFile(path, &mime_type);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE, base::Bind(callback, mime_type));
}

bool IsOpenInBrowserPreferreredForFile(const base::FilePath& path) {
  // On Android, always prefer opening with an external app.
#if !defined(OS_ANDROID) && defined(ENABLE_PLUGINS)
  // TODO(asanka): Consider other file types and MIME types.
  if (path.MatchesExtension(FILE_PATH_LITERAL(".pdf")))
    return true;
#endif
  return false;
}

}  // namespace

ChromeDownloadManagerDelegate::ChromeDownloadManagerDelegate(Profile* profile)
    : profile_(profile),
      next_download_id_(content::DownloadItem::kInvalidId),
      download_prefs_(new DownloadPrefs(profile)) {
}

ChromeDownloadManagerDelegate::~ChromeDownloadManagerDelegate() {
}

void ChromeDownloadManagerDelegate::SetDownloadManager(DownloadManager* dm) {
  download_manager_ = dm;
}

void ChromeDownloadManagerDelegate::Shutdown() {
  download_prefs_.reset();
}

void ChromeDownloadManagerDelegate::SetNextId(uint32 next_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!profile_->IsOffTheRecord());
  DCHECK_NE(content::DownloadItem::kInvalidId, next_id);
  next_download_id_ = next_id;

  IdCallbackVector callbacks;
  id_callbacks_.swap(callbacks);
  for (IdCallbackVector::const_iterator it = callbacks.begin();
       it != callbacks.end(); ++it) {
    ReturnNextId(*it);
  }
}

void ChromeDownloadManagerDelegate::GetNextId(
    const content::DownloadIdCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (profile_->IsOffTheRecord()) {
    content::BrowserContext::GetDownloadManager(
        profile_->GetOriginalProfile())->GetDelegate()->GetNextId(callback);
    return;
  }
  if (next_download_id_ == content::DownloadItem::kInvalidId) {
    id_callbacks_.push_back(callback);
    return;
  }
  ReturnNextId(callback);
}

void ChromeDownloadManagerDelegate::ReturnNextId(
    const content::DownloadIdCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!profile_->IsOffTheRecord());
  DCHECK_NE(content::DownloadItem::kInvalidId, next_download_id_);
  callback.Run(next_download_id_++);
}

bool ChromeDownloadManagerDelegate::DetermineDownloadTarget(
    DownloadItem* download,
    const content::DownloadTargetCallback& callback) {
  DownloadTargetDeterminer::CompletionCallback target_determined_callback =
      base::Bind(&ChromeDownloadManagerDelegate::OnDownloadTargetDetermined,
                 this,
                 download->GetId(),
                 callback);
  DownloadTargetDeterminer::Start(
      download,
      GetPlatformDownloadPath(profile_, download, PLATFORM_TARGET_PATH),
      download_prefs_.get(),
      this,
      target_determined_callback);
  return true;
}

bool ChromeDownloadManagerDelegate::ShouldOpenFileBasedOnExtension(
    const base::FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (path.Extension().empty())
    return false;
  // TODO(asanka): This determination is done based on |path|, while
  // ShouldOpenDownload() detects extension downloads based on the
  // characteristics of the download. Reconcile this. http://crbug.com/167702
  if (path.MatchesExtension(extensions::kExtensionFileExtension))
    return false;
  return download_prefs_->IsAutoOpenEnabledBasedOnExtension(path);
}

// static
void ChromeDownloadManagerDelegate::DisableSafeBrowsing(DownloadItem* item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(FULL_SAFE_BROWSING)
  SafeBrowsingState* state = static_cast<SafeBrowsingState*>(
      item->GetUserData(&kSafeBrowsingUserDataKey));
  if (!state) {
    state = new SafeBrowsingState();
    item->SetUserData(&kSafeBrowsingUserDataKey, state);
  }
  state->SetVerdict(DownloadProtectionService::SAFE);
#endif
}

bool ChromeDownloadManagerDelegate::IsDownloadReadyForCompletion(
    DownloadItem* item,
    const base::Closure& internal_complete_callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(FULL_SAFE_BROWSING)
  SafeBrowsingState* state = static_cast<SafeBrowsingState*>(
      item->GetUserData(&kSafeBrowsingUserDataKey));
  if (!state) {
    // Begin the safe browsing download protection check.
    DownloadProtectionService* service = GetDownloadProtectionService();
    if (service) {
      VLOG(2) << __FUNCTION__ << "() Start SB download check for download = "
              << item->DebugString(false);
      state = new SafeBrowsingState();
      state->set_callback(internal_complete_callback);
      item->SetUserData(&kSafeBrowsingUserDataKey, state);
      service->CheckClientDownload(
          item,
          base::Bind(
              &ChromeDownloadManagerDelegate::CheckClientDownloadDone,
              this,
              item->GetId()));
      return false;
    }
  } else if (!state->is_complete()) {
    // Don't complete the download until we have an answer.
    state->set_callback(internal_complete_callback);
    return false;
  }
#endif
  return true;
}

void ChromeDownloadManagerDelegate::ShouldCompleteDownloadInternal(
    uint32 download_id,
    const base::Closure& user_complete_callback) {
  DownloadItem* item = download_manager_->GetDownload(download_id);
  if (!item)
    return;
  if (ShouldCompleteDownload(item, user_complete_callback))
    user_complete_callback.Run();
}

bool ChromeDownloadManagerDelegate::ShouldCompleteDownload(
    DownloadItem* item,
    const base::Closure& user_complete_callback) {
  return IsDownloadReadyForCompletion(item, base::Bind(
      &ChromeDownloadManagerDelegate::ShouldCompleteDownloadInternal,
      this, item->GetId(), user_complete_callback));
}

bool ChromeDownloadManagerDelegate::ShouldOpenDownload(
    DownloadItem* item, const content::DownloadOpenDelayedCallback& callback) {
  if (download_crx_util::IsExtensionDownload(*item)) {
    scoped_refptr<extensions::CrxInstaller> crx_installer =
        download_crx_util::OpenChromeExtension(profile_, *item);

    // CRX_INSTALLER_DONE will fire when the install completes.  At that
    // time, Observe() will call the passed callback.
    registrar_.Add(
        this,
        chrome::NOTIFICATION_CRX_INSTALLER_DONE,
        content::Source<extensions::CrxInstaller>(crx_installer.get()));

    crx_installers_[crx_installer.get()] = callback;
    // The status text and percent complete indicator will change now
    // that we are installing a CRX.  Update observers so that they pick
    // up the change.
    item->UpdateObservers();
    return false;
  }

  return true;
}

bool ChromeDownloadManagerDelegate::GenerateFileHash() {
#if defined(FULL_SAFE_BROWSING)
  return profile_->GetPrefs()->GetBoolean(prefs::kSafeBrowsingEnabled) &&
      g_browser_process->safe_browsing_service()->DownloadBinHashNeeded();
#else
  return false;
#endif
}

void ChromeDownloadManagerDelegate::GetSaveDir(
    content::BrowserContext* browser_context,
    base::FilePath* website_save_dir,
    base::FilePath* download_save_dir,
    bool* skip_dir_check) {
  *website_save_dir = download_prefs_->SaveFilePath();
  DCHECK(!website_save_dir->empty());
  *download_save_dir = download_prefs_->DownloadPath();
  *skip_dir_check = false;
#if defined(OS_CHROMEOS)
  *skip_dir_check = drive::util::IsUnderDriveMountPoint(*website_save_dir);
#endif
}

void ChromeDownloadManagerDelegate::ChooseSavePath(
    content::WebContents* web_contents,
    const base::FilePath& suggested_path,
    const base::FilePath::StringType& default_extension,
    bool can_save_as_complete,
    const content::SavePackagePathPickedCallback& callback) {
  // Deletes itself.
  new SavePackageFilePicker(
      web_contents,
      suggested_path,
      default_extension,
      can_save_as_complete,
      download_prefs_.get(),
      callback);
}

void ChromeDownloadManagerDelegate::OpenDownloadUsingPlatformHandler(
    DownloadItem* download) {
  base::FilePath platform_path(
      GetPlatformDownloadPath(profile_, download, PLATFORM_TARGET_PATH));
  DCHECK(!platform_path.empty());
  platform_util::OpenItem(platform_path);
}

void ChromeDownloadManagerDelegate::OpenDownload(DownloadItem* download) {
  DCHECK_EQ(DownloadItem::COMPLETE, download->GetState());
  DCHECK(!download->GetTargetFilePath().empty());
  if (!download->CanOpenDownload())
    return;

  if (!DownloadItemModel(download).ShouldPreferOpeningInBrowser()) {
    RecordDownloadOpenMethod(DOWNLOAD_OPEN_METHOD_DEFAULT_PLATFORM);
    OpenDownloadUsingPlatformHandler(download);
    return;
  }

#if !defined(OS_ANDROID)
  content::WebContents* web_contents = download->GetWebContents();
  Browser* browser =
      web_contents ? chrome::FindBrowserWithWebContents(web_contents) : NULL;
  scoped_ptr<chrome::ScopedTabbedBrowserDisplayer> browser_displayer;
  if (!browser ||
      !browser->CanSupportWindowFeature(Browser::FEATURE_TABSTRIP)) {
    browser_displayer.reset(new chrome::ScopedTabbedBrowserDisplayer(
        profile_, chrome::GetActiveDesktop()));
    browser = browser_displayer->browser();
  }
  content::OpenURLParams params(
      net::FilePathToFileURL(download->GetTargetFilePath()),
      content::Referrer(),
      NEW_FOREGROUND_TAB,
      content::PAGE_TRANSITION_LINK,
      false);
  browser->OpenURL(params);
  RecordDownloadOpenMethod(DOWNLOAD_OPEN_METHOD_DEFAULT_BROWSER);
#else
  // ShouldPreferOpeningInBrowser() should never be true on Android.
  NOTREACHED();
#endif
}

void ChromeDownloadManagerDelegate::ShowDownloadInShell(
    DownloadItem* download) {
  if (!download->CanShowInFolder())
    return;
  base::FilePath platform_path(
      GetPlatformDownloadPath(profile_, download, PLATFORM_CURRENT_PATH));
  DCHECK(!platform_path.empty());
  platform_util::ShowItemInFolder(platform_path);
}

void ChromeDownloadManagerDelegate::CheckForFileExistence(
    DownloadItem* download,
    const content::CheckForFileExistenceCallback& callback) {
#if defined(OS_CHROMEOS)
  drive::DownloadHandler* drive_download_handler =
      drive::DownloadHandler::GetForProfile(profile_);
  if (drive_download_handler &&
      drive_download_handler->IsDriveDownload(download)) {
    drive_download_handler->CheckForFileExistence(download, callback);
    return;
  }
#endif
  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&base::PathExists, download->GetTargetFilePath()),
      callback);
}

std::string
ChromeDownloadManagerDelegate::ApplicationClientIdForFileScanning() const {
  return std::string(chrome::kApplicationClientIDStringForAVScanning);
}

DownloadProtectionService*
    ChromeDownloadManagerDelegate::GetDownloadProtectionService() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(FULL_SAFE_BROWSING)
  SafeBrowsingService* sb_service = g_browser_process->safe_browsing_service();
  if (sb_service && sb_service->download_protection_service() &&
      profile_->GetPrefs()->GetBoolean(prefs::kSafeBrowsingEnabled)) {
    return sb_service->download_protection_service();
  }
#endif
  return NULL;
}

void ChromeDownloadManagerDelegate::NotifyExtensions(
    DownloadItem* download,
    const base::FilePath& virtual_path,
    const NotifyExtensionsCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if !defined(OS_ANDROID)
  ExtensionDownloadsEventRouter* router =
      DownloadServiceFactory::GetForBrowserContext(profile_)->
      GetExtensionEventRouter();
  if (router) {
    base::Closure original_path_callback =
        base::Bind(callback, base::FilePath(),
                   DownloadPathReservationTracker::UNIQUIFY);
    router->OnDeterminingFilename(download, virtual_path.BaseName(),
                                  original_path_callback,
                                  callback);
    return;
  }
#endif
  callback.Run(base::FilePath(), DownloadPathReservationTracker::UNIQUIFY);
}

void ChromeDownloadManagerDelegate::ReserveVirtualPath(
    content::DownloadItem* download,
    const base::FilePath& virtual_path,
    bool create_directory,
    DownloadPathReservationTracker::FilenameConflictAction conflict_action,
    const DownloadTargetDeterminerDelegate::ReservedPathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!virtual_path.empty());
#if defined(OS_CHROMEOS)
  // TODO(asanka): Handle path reservations for virtual paths as well.
  //               http://crbug.com/151618
  if (drive::util::IsUnderDriveMountPoint(virtual_path)) {
    callback.Run(virtual_path, true);
    return;
  }
#endif
  DownloadPathReservationTracker::GetReservedPath(
      download,
      virtual_path,
      download_prefs_->DownloadPath(),
      create_directory,
      conflict_action,
      callback);
}

void ChromeDownloadManagerDelegate::PromptUserForDownloadPath(
    DownloadItem* download,
    const base::FilePath& suggested_path,
    const DownloadTargetDeterminerDelegate::FileSelectedCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadFilePicker::ShowFilePicker(download, suggested_path, callback);
}

void ChromeDownloadManagerDelegate::DetermineLocalPath(
    DownloadItem* download,
    const base::FilePath& virtual_path,
    const DownloadTargetDeterminerDelegate::LocalPathCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
#if defined(OS_CHROMEOS)
  drive::DownloadHandler* drive_download_handler =
      drive::DownloadHandler::GetForProfile(profile_);
  if (drive_download_handler) {
    drive_download_handler->SubstituteDriveDownloadPath(
        virtual_path, download, callback);
    return;
  }
#endif
  callback.Run(virtual_path);
}

void ChromeDownloadManagerDelegate::CheckDownloadUrl(
    DownloadItem* download,
    const base::FilePath& suggested_path,
    const CheckDownloadUrlCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

#if defined(FULL_SAFE_BROWSING)
  safe_browsing::DownloadProtectionService* service =
      GetDownloadProtectionService();
  if (service) {
    bool is_content_check_supported =
        service->IsSupportedDownload(*download, suggested_path);
    VLOG(2) << __FUNCTION__ << "() Start SB URL check for download = "
            << download->DebugString(false);
    service->CheckDownloadUrl(*download,
                              base::Bind(&CheckDownloadUrlDone,
                                         callback,
                                         is_content_check_supported));
    return;
  }
#endif
  callback.Run(content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS);
}

void ChromeDownloadManagerDelegate::GetFileMimeType(
    const base::FilePath& path,
    const GetFileMimeTypeCallback& callback) {
  BrowserThread::PostBlockingPoolTask(
      FROM_HERE,
      base::Bind(&GetMimeTypeAndReplyOnUIThread, path, callback));
}

#if defined(FULL_SAFE_BROWSING)
void ChromeDownloadManagerDelegate::CheckClientDownloadDone(
    uint32 download_id,
    DownloadProtectionService::DownloadCheckResult result) {
  DownloadItem* item = download_manager_->GetDownload(download_id);
  if (!item || (item->GetState() != DownloadItem::IN_PROGRESS))
    return;

  VLOG(2) << __FUNCTION__ << "() download = " << item->DebugString(false)
          << " verdict = " << result;
  // We only mark the content as being dangerous if the download's safety state
  // has not been set to DANGEROUS yet.  We don't want to show two warnings.
  if (item->GetDangerType() == content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS ||
      item->GetDangerType() ==
      content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT) {
    content::DownloadDangerType danger_type =
        content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS;
    switch (result) {
      case DownloadProtectionService::SAFE:
        // Do nothing.
        break;
      case DownloadProtectionService::DANGEROUS:
        danger_type = content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT;
        break;
      case DownloadProtectionService::UNCOMMON:
        danger_type = content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT;
        break;
      case DownloadProtectionService::DANGEROUS_HOST:
        danger_type = content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST;
        break;
      case DownloadProtectionService::POTENTIALLY_UNWANTED:
        danger_type = content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED;
        break;
    }

    if (danger_type != content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS)
      item->OnContentCheckCompleted(danger_type);
  }

  SafeBrowsingState* state = static_cast<SafeBrowsingState*>(
      item->GetUserData(&kSafeBrowsingUserDataKey));
  state->SetVerdict(result);
}
#endif  // FULL_SAFE_BROWSING

// content::NotificationObserver implementation.
void ChromeDownloadManagerDelegate::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(type == chrome::NOTIFICATION_CRX_INSTALLER_DONE);

  registrar_.Remove(this,
                    chrome::NOTIFICATION_CRX_INSTALLER_DONE,
                    source);

  scoped_refptr<extensions::CrxInstaller> installer =
      content::Source<extensions::CrxInstaller>(source).ptr();
  content::DownloadOpenDelayedCallback callback =
      crx_installers_[installer.get()];
  crx_installers_.erase(installer.get());
  callback.Run(installer->did_handle_successfully());
}

void ChromeDownloadManagerDelegate::OnDownloadTargetDetermined(
    int32 download_id,
    const content::DownloadTargetCallback& callback,
    scoped_ptr<DownloadTargetInfo> target_info) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DownloadItem* item = download_manager_->GetDownload(download_id);
  if (!target_info->target_path.empty() && item &&
      IsOpenInBrowserPreferreredForFile(target_info->target_path) &&
      target_info->is_filetype_handled_securely)
    DownloadItemModel(item).SetShouldPreferOpeningInBrowser(true);
  callback.Run(target_info->target_path,
               target_info->target_disposition,
               target_info->danger_type,
               target_info->intermediate_path);
}
