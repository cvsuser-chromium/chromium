// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Browser test for basic Chrome OS file manager functionality:
//  - The file list is updated when a file is added externally to the Downloads
//    folder.
//  - Selecting a file and copy-pasting it with the keyboard copies the file.
//  - Selecting a file and pressing delete deletes it.

#include <deque>
#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/json/json_value_converter.h"
#include "base/json/json_writer.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/drive/drive_integration_service.h"
#include "chrome/browser/chromeos/drive/file_system_interface.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/chromeos/file_manager/drive_test_util.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "chrome/browser/extensions/api/test/test_api.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_test_message_listener.h"
#include "chrome/browser/google_apis/gdata_wapi_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "webkit/browser/fileapi/external_mount_points.h"

namespace file_manager {
namespace {

enum EntryType {
  FILE,
  DIRECTORY,
};

enum TargetVolume {
  LOCAL_VOLUME,
  DRIVE_VOLUME,
};

enum SharedOption {
  NONE,
  SHARED,
};

enum GuestMode {
  NOT_IN_GUEST_MODE,
  IN_GUEST_MODE,
};

// This global operator is used from Google Test to format error messages.
std::ostream& operator<<(std::ostream& os, const GuestMode& guest_mode) {
  return os << (guest_mode == IN_GUEST_MODE ?
                "IN_GUEST_MODE" : "NOT_IN_GUEST_MODE");
}

// Maps the given string to EntryType. Returns true on success.
bool MapStringToEntryType(const base::StringPiece& value, EntryType* output) {
  if (value == "file")
    *output = FILE;
  else if (value == "directory")
    *output = DIRECTORY;
  else
    return false;
  return true;
}

// Maps the given string to SharedOption. Returns true on success.
bool MapStringToSharedOption(const base::StringPiece& value,
                             SharedOption* output) {
  if (value == "shared")
    *output = SHARED;
  else if (value == "none")
    *output = NONE;
  else
    return false;
  return true;
}

// Maps the given string to TargetVolume. Returns true on success.
bool MapStringToTargetVolume(const base::StringPiece& value,
                             TargetVolume* output) {
  if (value == "drive")
    *output = DRIVE_VOLUME;
  else if (value == "local")
    *output = LOCAL_VOLUME;
  else
    return false;
  return true;
}

// Maps the given string to base::Time. Returns true on success.
bool MapStringToTime(const base::StringPiece& value, base::Time* time) {
  return base::Time::FromString(value.as_string().c_str(), time);
}

// Test data of file or directory.
struct TestEntryInfo {
  TestEntryInfo() : type(FILE), shared_option(NONE) {}

  TestEntryInfo(EntryType type,
                const std::string& source_file_name,
                const std::string& target_path,
                const std::string& mime_type,
                SharedOption shared_option,
                const base::Time& last_modified_time) :
      type(type),
      source_file_name(source_file_name),
      target_path(target_path),
      mime_type(mime_type),
      shared_option(shared_option),
      last_modified_time(last_modified_time) {
  }

  EntryType type;
  std::string source_file_name;  // Source file name to be used as a prototype.
  std::string target_path;  // Target file or directory path.
  std::string mime_type;
  SharedOption shared_option;
  base::Time last_modified_time;

  // Registers the member information to the given converter.
  static void RegisterJSONConverter(
      base::JSONValueConverter<TestEntryInfo>* converter);
};

// static
void TestEntryInfo::RegisterJSONConverter(
    base::JSONValueConverter<TestEntryInfo>* converter) {
  converter->RegisterCustomField("type",
                                 &TestEntryInfo::type,
                                 &MapStringToEntryType);
  converter->RegisterStringField("sourceFileName",
                                 &TestEntryInfo::source_file_name);
  converter->RegisterStringField("targetPath", &TestEntryInfo::target_path);
  converter->RegisterStringField("mimeType", &TestEntryInfo::mime_type);
  converter->RegisterCustomField("sharedOption",
                                 &TestEntryInfo::shared_option,
                                 &MapStringToSharedOption);
  converter->RegisterCustomField("lastModifiedTime",
                                 &TestEntryInfo::last_modified_time,
                                 &MapStringToTime);
}

// Message from JavaScript to add entries.
struct AddEntriesMessage {
  // Target volume to be added the |entries|.
  TargetVolume volume;

  // Entries to be added.
  ScopedVector<TestEntryInfo> entries;

  // Registers the member information to the given converter.
  static void RegisterJSONConverter(
      base::JSONValueConverter<AddEntriesMessage>* converter);
};


// static
void AddEntriesMessage::RegisterJSONConverter(
    base::JSONValueConverter<AddEntriesMessage>* converter) {
  converter->RegisterCustomField("volume",
                                 &AddEntriesMessage::volume,
                                 &MapStringToTargetVolume);
  converter->RegisterRepeatedMessage<TestEntryInfo>(
      "entries",
      &AddEntriesMessage::entries);
}

// The local volume class for test.
// This class provides the operations for a test volume that simulates local
// drive.
class LocalTestVolume {
 public:
  // Adds this volume to the file system as a local volume. Returns true on
  // success.
  bool Mount(Profile* profile) {
    const std::string kDownloads = "Downloads";

    if (local_path_.empty()) {
      if (!tmp_dir_.CreateUniqueTempDir())
        return false;
      local_path_ = tmp_dir_.path().Append(kDownloads);
    }
    fileapi::ExternalMountPoints* const mount_points =
        content::BrowserContext::GetMountPoints(profile);
    mount_points->RevokeFileSystem(kDownloads);

    return mount_points->RegisterFileSystem(
        kDownloads, fileapi::kFileSystemTypeNativeLocal, local_path_) &&
        file_util::CreateDirectory(local_path_);
  }

  void CreateEntry(const TestEntryInfo& entry) {
    const base::FilePath target_path =
        local_path_.AppendASCII(entry.target_path);

    entries_.insert(std::make_pair(target_path, entry));
    switch (entry.type) {
      case FILE: {
        const base::FilePath source_path =
            google_apis::test_util::GetTestFilePath("chromeos/file_manager").
            AppendASCII(entry.source_file_name);
        ASSERT_TRUE(base::CopyFile(source_path, target_path))
            << "Copy from " << source_path.value()
            << " to " << target_path.value() << " failed.";
        break;
      }
      case DIRECTORY:
        ASSERT_TRUE(file_util::CreateDirectory(target_path)) <<
            "Failed to create a directory: " << target_path.value();
        break;
    }
    ASSERT_TRUE(UpdateModifiedTime(entry));
  }

 private:
  // Updates ModifiedTime of the entry and its parents by referring
  // TestEntryInfo. Returns true on success.
  bool UpdateModifiedTime(const TestEntryInfo& entry) {
    const base::FilePath path = local_path_.AppendASCII(entry.target_path);
    if (!file_util::SetLastModifiedTime(path, entry.last_modified_time))
      return false;

    // Update the modified time of parent directories because it may be also
    // affected by the update of child items.
    if (path.DirName() != local_path_) {
      const std::map<base::FilePath, const TestEntryInfo>::iterator it =
          entries_.find(path.DirName());
      if (it == entries_.end())
        return false;
      return UpdateModifiedTime(it->second);
    }
    return true;
  }

  base::FilePath local_path_;
  base::ScopedTempDir tmp_dir_;
  std::map<base::FilePath, const TestEntryInfo> entries_;
};

// The drive volume class for test.
// This class provides the operations for a test volume that simulates Google
// drive.
class DriveTestVolume {
 public:
  DriveTestVolume() : fake_drive_service_(NULL),
                      integration_service_(NULL) {
  }

  // Sends request to add this volume to the file system as Google drive.
  // This method must be calld at SetUp method of FileManagerBrowserTestBase.
  // Returns true on success.
  bool SetUp() {
    if (!test_cache_root_.CreateUniqueTempDir())
      return false;
    drive::DriveIntegrationServiceFactory::SetFactoryForTest(
        base::Bind(&DriveTestVolume::CreateDriveIntegrationService,
                   base::Unretained(this)));
    return true;
  }

  void CreateEntry(const TestEntryInfo& entry) {
    const base::FilePath path =
        base::FilePath::FromUTF8Unsafe(entry.target_path);
    const std::string target_name = path.BaseName().AsUTF8Unsafe();

    // Obtain the parent entry.
    drive::FileError error = drive::FILE_ERROR_OK;
    scoped_ptr<drive::ResourceEntry> parent_entry(new drive::ResourceEntry);
    integration_service_->file_system()->GetResourceEntry(
        drive::util::GetDriveMyDriveRootPath().Append(path).DirName(),
        google_apis::test_util::CreateCopyResultCallback(
            &error, &parent_entry));
    drive::test_util::RunBlockingPoolTask();
    ASSERT_EQ(drive::FILE_ERROR_OK, error);
    ASSERT_TRUE(parent_entry);

    switch (entry.type) {
      case FILE:
        CreateFile(entry.source_file_name,
                   parent_entry->resource_id(),
                   target_name,
                   entry.mime_type,
                   entry.shared_option == SHARED,
                   entry.last_modified_time);
        break;
      case DIRECTORY:
        CreateDirectory(parent_entry->resource_id(),
                        target_name,
                        entry.last_modified_time);
        break;
    }
  }

  // Creates an empty directory with the given |name| and |modification_time|.
  void CreateDirectory(const std::string& parent_id,
                       const std::string& target_name,
                       const base::Time& modification_time) {
    google_apis::GDataErrorCode error = google_apis::GDATA_OTHER_ERROR;
    scoped_ptr<google_apis::ResourceEntry> resource_entry;
    fake_drive_service_->AddNewDirectory(
        parent_id,
        target_name,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_CREATED, error);
    ASSERT_TRUE(resource_entry);

    fake_drive_service_->SetLastModifiedTime(
        resource_entry->resource_id(),
        modification_time,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_TRUE(error == google_apis::HTTP_SUCCESS);
    ASSERT_TRUE(resource_entry);
    CheckForUpdates();
  }

  // Creates a test file with the given spec.
  // Serves |test_file_name| file. Pass an empty string for an empty file.
  void CreateFile(const std::string& source_file_name,
                  const std::string& parent_id,
                  const std::string& target_name,
                  const std::string& mime_type,
                  bool shared_with_me,
                  const base::Time& modification_time) {
    google_apis::GDataErrorCode error = google_apis::GDATA_OTHER_ERROR;

    std::string content_data;
    if (!source_file_name.empty()) {
      base::FilePath source_file_path =
          google_apis::test_util::GetTestFilePath("chromeos/file_manager").
              AppendASCII(source_file_name);
      ASSERT_TRUE(base::ReadFileToString(source_file_path, &content_data));
    }

    scoped_ptr<google_apis::ResourceEntry> resource_entry;
    fake_drive_service_->AddNewFile(
        mime_type,
        content_data,
        parent_id,
        target_name,
        shared_with_me,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_CREATED, error);
    ASSERT_TRUE(resource_entry);

    fake_drive_service_->SetLastModifiedTime(
        resource_entry->resource_id(),
        modification_time,
        google_apis::test_util::CreateCopyResultCallback(&error,
                                                         &resource_entry));
    base::MessageLoop::current()->RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_SUCCESS, error);
    ASSERT_TRUE(resource_entry);

    CheckForUpdates();
  }

  // Notifies FileSystem that the contents in FakeDriveService are
  // changed, hence the new contents should be fetched.
  void CheckForUpdates() {
    if (integration_service_ && integration_service_->file_system()) {
      integration_service_->file_system()->CheckForUpdates();
    }
  }

  // Sets the url base for the test server to be used to generate share urls
  // on the files and directories.
  void ConfigureShareUrlBase(const GURL& share_url_base) {
    fake_drive_service_->set_share_url_base(share_url_base);
  }

  drive::DriveIntegrationService* CreateDriveIntegrationService(
      Profile* profile) {
    fake_drive_service_ = new drive::FakeDriveService;
    fake_drive_service_->LoadResourceListForWapi(
        "gdata/empty_feed.json");
    fake_drive_service_->LoadAccountMetadataForWapi(
        "gdata/account_metadata.json");
    fake_drive_service_->LoadAppListForDriveApi("drive/applist.json");
    integration_service_ = new drive::DriveIntegrationService(
        profile, NULL, fake_drive_service_, test_cache_root_.path(), NULL);
    return integration_service_;
  }

 private:
  base::ScopedTempDir test_cache_root_;
  drive::FakeDriveService* fake_drive_service_;
  drive::DriveIntegrationService* integration_service_;
};

// Listener to obtain the test relative messages synchronously.
class FileManagerTestListener : public content::NotificationObserver {
 public:
  struct Message {
    int type;
    std::string message;
    extensions::TestSendMessageFunction* function;
  };

  FileManagerTestListener() {
    registrar_.Add(this,
                   chrome::NOTIFICATION_EXTENSION_TEST_PASSED,
                   content::NotificationService::AllSources());
    registrar_.Add(this,
                   chrome::NOTIFICATION_EXTENSION_TEST_FAILED,
                   content::NotificationService::AllSources());
    registrar_.Add(this,
                   chrome::NOTIFICATION_EXTENSION_TEST_MESSAGE,
                   content::NotificationService::AllSources());
  }

  Message GetNextMessage() {
    if (messages_.empty())
      content::RunMessageLoop();
    const Message entry = messages_.front();
    messages_.pop_front();
    return entry;
  }

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE {
    Message entry;
    entry.type = type;
    entry.message = type != chrome::NOTIFICATION_EXTENSION_TEST_PASSED ?
        *content::Details<std::string>(details).ptr() :
        std::string();
    entry.function = type == chrome::NOTIFICATION_EXTENSION_TEST_MESSAGE ?
        content::Source<extensions::TestSendMessageFunction>(source).ptr() :
        NULL;
    messages_.push_back(entry);
    base::MessageLoopForUI::current()->Quit();
  }

 private:
  std::deque<Message> messages_;
  content::NotificationRegistrar registrar_;
};

// Parameter of FileManagerBrowserTest.
// The second value is the case name of JavaScript.
typedef std::tr1::tuple<GuestMode, const char*> TestParameter;

// The base test class.
class FileManagerBrowserTest :
      public ExtensionApiTest,
      public ::testing::WithParamInterface<TestParameter> {
 protected:
  FileManagerBrowserTest() :
      local_volume_(new LocalTestVolume),
      drive_volume_(std::tr1::get<0>(GetParam()) != IN_GUEST_MODE ?
                    new DriveTestVolume() : NULL) {}

  virtual void SetUp() OVERRIDE {
    // TODO(danakj): The GPU Video Decoder needs real GL bindings.
    // crbug.com/269087
    UseRealGLBindings();

    ExtensionApiTest::SetUp();
  }

  virtual void SetUpInProcessBrowserTestFixture() OVERRIDE;

  virtual void SetUpOnMainThread() OVERRIDE;

  // Adds an incognito and guest-mode flags for tests in the guest mode.
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE;

  // Loads our testing extension and sends it a string identifying the current
  // test.
  void StartTest();

  const scoped_ptr<LocalTestVolume> local_volume_;
  const scoped_ptr<DriveTestVolume> drive_volume_;
};

void FileManagerBrowserTest::SetUpInProcessBrowserTestFixture() {
  ExtensionApiTest::SetUpInProcessBrowserTestFixture();
  extensions::ComponentLoader::EnableBackgroundExtensionsForTesting();
  if (drive_volume_)
    ASSERT_TRUE(drive_volume_->SetUp());
}

void FileManagerBrowserTest::SetUpOnMainThread() {
  ExtensionApiTest::SetUpOnMainThread();
  ASSERT_TRUE(local_volume_->Mount(browser()->profile()));

  if (drive_volume_) {
    // Install the web server to serve the mocked share dialog.
    ASSERT_TRUE(embedded_test_server()->InitializeAndWaitUntilReady());
    const GURL share_url_base(embedded_test_server()->GetURL(
        "/chromeos/file_manager/share_dialog_mock/index.html"));
    drive_volume_->ConfigureShareUrlBase(share_url_base);
    test_util::WaitUntilDriveMountPointIsAdded(browser()->profile());
  }
}

void FileManagerBrowserTest::SetUpCommandLine(CommandLine* command_line) {
  if (std::tr1::get<0>(GetParam()) == IN_GUEST_MODE) {
    command_line->AppendSwitch(chromeos::switches::kGuestSession);
    command_line->AppendSwitchNative(chromeos::switches::kLoginUser, "");
    command_line->AppendSwitch(switches::kIncognito);
  }
  // TODO(yoshiki): Remove the flag when the feature is launched.
  if (std::tr1::get<1>(GetParam()) == std::string("suggestAppDialog")) {
    command_line->AppendSwitch(
        chromeos::switches::kFileManagerEnableWebstoreIntegration);
  }
  ExtensionApiTest::SetUpCommandLine(command_line);
}

IN_PROC_BROWSER_TEST_P(FileManagerBrowserTest, Test) {
  // Launch the extension.
  base::FilePath path = test_data_dir_.AppendASCII("file_manager_browsertest");
  const extensions::Extension* extension = LoadExtensionAsComponent(path);
  ASSERT_TRUE(extension);

  // Handle the messages from JavaScript.
  // The while loop is break when the test is passed or failed.
  FileManagerTestListener listener;
  base::JSONValueConverter<AddEntriesMessage> add_entries_message_converter;
  while (true) {
    FileManagerTestListener::Message entry = listener.GetNextMessage();
    if (entry.type == chrome::NOTIFICATION_EXTENSION_TEST_PASSED) {
      // Test succeed.
      break;
    } else if (entry.type == chrome::NOTIFICATION_EXTENSION_TEST_FAILED) {
      // Test failed.
      ADD_FAILURE() << entry.message;
      break;
    }

    // Parse the message value as JSON.
    const scoped_ptr<const base::Value> value(
        base::JSONReader::Read(entry.message));

    // If the message is not the expected format, just ignore it.
    const base::DictionaryValue* message_dictionary = NULL;
    std::string name;
    if (!value || !value->GetAsDictionary(&message_dictionary) ||
        !message_dictionary->GetString("name", &name))
      continue;

    if (name == "getTestName") {
      // Pass the test case name.
      entry.function->Reply(std::tr1::get<1>(GetParam()));
    } else if (name == "isInGuestMode") {
      // Obtain whether the test is in guest mode or not.
      entry.function->Reply(std::tr1::get<0>(GetParam()) ? "true" : "false");
    } else if (name == "getCwsWidgetContainerMockUrl") {
      // Obtain whether the test is in guest mode or not.
      const GURL url = embedded_test_server()->GetURL(
            "/chromeos/file_manager/cws_container_mock/index.html");
      std::string origin = url.GetOrigin().spec();

      // Removes trailing a slash.
      if (*origin.rbegin() == '/')
        origin.resize(origin.length() - 1);

      const scoped_ptr<base::DictionaryValue> res(new base::DictionaryValue());
      res->SetString("url", url.spec());
      res->SetString("origin", origin);
      std::string jsonString;
      base::JSONWriter::Write(res.get(), &jsonString);
      entry.function->Reply(jsonString);
    } else if (name == "addEntries") {
      // Add entries to the specified volume.
      AddEntriesMessage message;
      if (!add_entries_message_converter.Convert(*value.get(), &message)) {
        entry.function->Reply("onError");
        continue;
      }
      for (size_t i = 0; i < message.entries.size(); ++i) {
        switch (message.volume) {
          case LOCAL_VOLUME:
            local_volume_->CreateEntry(*message.entries[i]);
            break;
          case DRIVE_VOLUME:
            if (drive_volume_)
              drive_volume_->CreateEntry(*message.entries[i]);
            break;
          default:
            NOTREACHED();
            break;
        }
      }
      entry.function->Reply("onEntryAdded");
    }
  }
}

INSTANTIATE_TEST_CASE_P(
    FileDisplay,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "fileDisplayDownloads"),
                      TestParameter(IN_GUEST_MODE, "fileDisplayDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "fileDisplayDrive")));

INSTANTIATE_TEST_CASE_P(
    OpenSpecialTypes,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "videoOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "videoOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "videoOpenDrive"),
                      TestParameter(IN_GUEST_MODE, "audioOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "audioOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "audioOpenDrive"),
                      TestParameter(IN_GUEST_MODE, "galleryOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "galleryOpenDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "galleryOpenDrive")));

/* http://crbug.com/316918 Tests are flaky.
INSTANTIATE_TEST_CASE_P(
    KeyboardOperations,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "keyboardDeleteDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "keyboardDeleteDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardDeleteDrive"),
                      TestParameter(IN_GUEST_MODE, "keyboardCopyDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardCopyDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "keyboardCopyDrive")));
*/

INSTANTIATE_TEST_CASE_P(
    DriveSpecific,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "openSidebarRecent"),
                      TestParameter(NOT_IN_GUEST_MODE, "openSidebarOffline"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "openSidebarSharedWithMe"),
                      TestParameter(NOT_IN_GUEST_MODE, "autocomplete")));

INSTANTIATE_TEST_CASE_P(
    Transfer,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromDriveToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromDownloadsToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromSharedToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromSharedToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromRecentToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromRecentToDrive"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromOfflineToDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE,
                                    "transferFromOfflineToDrive")));

INSTANTIATE_TEST_CASE_P(
     HideSearchBox,
     FileManagerBrowserTest,
     ::testing::Values(TestParameter(IN_GUEST_MODE, "hideSearchBox"),
                       TestParameter(NOT_IN_GUEST_MODE, "hideSearchBox")));

INSTANTIATE_TEST_CASE_P(
    RestorePrefs,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "restoreSortColumn"),
                      TestParameter(NOT_IN_GUEST_MODE, "restoreSortColumn"),
                      TestParameter(IN_GUEST_MODE, "restoreCurrentView"),
                      TestParameter(NOT_IN_GUEST_MODE, "restoreCurrentView")));

INSTANTIATE_TEST_CASE_P(
    ShareDialog,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "shareFile"),
                      TestParameter(NOT_IN_GUEST_MODE, "shareDirectory")));

INSTANTIATE_TEST_CASE_P(
    restoreGeometry,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "restoreGeometry"),
                      TestParameter(IN_GUEST_MODE, "restoreGeometry")));

INSTANTIATE_TEST_CASE_P(
    Traverse,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(IN_GUEST_MODE, "traverseDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "traverseDownloads"),
                      TestParameter(NOT_IN_GUEST_MODE, "traverseDrive")));

INSTANTIATE_TEST_CASE_P(
    SuggestAppDialog,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE, "suggestAppDialog")));

INSTANTIATE_TEST_CASE_P(
    NavigationList,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE,
                                    "traverseNavigationList")));

INSTANTIATE_TEST_CASE_P(
    TabIndex,
    FileManagerBrowserTest,
    ::testing::Values(TestParameter(NOT_IN_GUEST_MODE,
                                    "searchBoxFocus")));

}  // namespace
}  // namespace file_manager
