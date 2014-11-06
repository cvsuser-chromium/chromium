// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_system.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/prefs/testing_pref_service.h"
#include "base/run_loop.h"
#include "chrome/browser/chromeos/drive/change_list_loader.h"
#include "chrome/browser/chromeos/drive/change_list_processor.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/file_system_observer.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/sync_client.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/drive/fake_drive_service.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/test_util.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace {

// Counts the number of invocation, and if it increased up to |expected_counter|
// quits the current message loop by calling |quit|.
void AsyncInitializationCallback(
    int* counter, int expected_counter, const base::Closure& quit,
    FileError error, scoped_ptr<ResourceEntry> entry) {
  if (error != FILE_ERROR_OK || !entry) {
    // If we hit an error case, quit the message loop immediately.
    // Then the expectation in the test case can find it because the actual
    // value of |counter| is different from the expected one.
    quit.Run();
    return;
  }

  (*counter)++;
  if (*counter >= expected_counter)
    quit.Run();
}

// This class is used to record directory changes and examine them later.
class MockDirectoryChangeObserver : public FileSystemObserver {
 public:
  MockDirectoryChangeObserver() {}
  virtual ~MockDirectoryChangeObserver() {}

  // FileSystemObserver overrides.
  virtual void OnDirectoryChanged(
      const base::FilePath& directory_path) OVERRIDE {
    changed_directories_.push_back(directory_path);
  }

  const std::vector<base::FilePath>& changed_directories() const {
    return changed_directories_;
  }

 private:
  std::vector<base::FilePath> changed_directories_;
  DISALLOW_COPY_AND_ASSIGN(MockDirectoryChangeObserver);
};

}  // namespace

class FileSystemTest : public testing::Test {
 protected:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    pref_service_.reset(new TestingPrefServiceSimple);
    test_util::RegisterDrivePrefs(pref_service_->registry());

    fake_drive_service_.reset(new FakeDriveService);
    fake_drive_service_->LoadResourceListForWapi(
        "gdata/root_feed.json");
    fake_drive_service_->LoadAccountMetadataForWapi(
        "gdata/account_metadata.json");

    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    scheduler_.reset(new JobScheduler(pref_service_.get(),
                                      fake_drive_service_.get(),
                                      base::MessageLoopProxy::current().get()));

    mock_directory_observer_.reset(new MockDirectoryChangeObserver);

    SetUpResourceMetadataAndFileSystem();
  }

  void SetUpResourceMetadataAndFileSystem() {
    const base::FilePath metadata_dir = temp_dir_.path().AppendASCII("meta");
    ASSERT_TRUE(file_util::CreateDirectory(metadata_dir));
    metadata_storage_.reset(new internal::ResourceMetadataStorage(
        metadata_dir, base::MessageLoopProxy::current().get()));
    ASSERT_TRUE(metadata_storage_->Initialize());

    const base::FilePath cache_dir = temp_dir_.path().AppendASCII("files");
    ASSERT_TRUE(file_util::CreateDirectory(cache_dir));
    cache_.reset(new internal::FileCache(
        metadata_storage_.get(),
        cache_dir,
        base::MessageLoopProxy::current().get(),
        fake_free_disk_space_getter_.get()));
    ASSERT_TRUE(cache_->Initialize());

    resource_metadata_.reset(new internal::ResourceMetadata(
        metadata_storage_.get(), base::MessageLoopProxy::current()));
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata_->Initialize());

    const base::FilePath temp_file_dir = temp_dir_.path().AppendASCII("tmp");
    ASSERT_TRUE(file_util::CreateDirectory(temp_file_dir));
    file_system_.reset(new FileSystem(
        pref_service_.get(),
        cache_.get(),
        fake_drive_service_.get(),
        scheduler_.get(),
        resource_metadata_.get(),
        base::MessageLoopProxy::current().get(),
        temp_file_dir));
    file_system_->AddObserver(mock_directory_observer_.get());

    // Disable delaying so that the sync starts immediately.
    file_system_->sync_client_for_testing()->set_delay_for_testing(
        base::TimeDelta::FromSeconds(0));
  }

  // Loads the full resource list via FakeDriveService.
  bool LoadFullResourceList() {
    FileError error = FILE_ERROR_FAILED;
    file_system_->change_list_loader_for_testing()->LoadIfNeeded(
        internal::DirectoryFetchInfo(),
        google_apis::test_util::CreateCopyResultCallback(&error));
    test_util::RunBlockingPoolTask();
    return error == FILE_ERROR_OK;
  }

  // Gets resource entry by path synchronously.
  scoped_ptr<ResourceEntry> GetResourceEntrySync(
      const base::FilePath& file_path) {
    FileError error = FILE_ERROR_FAILED;
    scoped_ptr<ResourceEntry> entry;
    file_system_->GetResourceEntry(
        file_path,
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    test_util::RunBlockingPoolTask();

    return entry.Pass();
  }

  // Gets directory info by path synchronously.
  scoped_ptr<ResourceEntryVector> ReadDirectorySync(
      const base::FilePath& file_path) {
    FileError error = FILE_ERROR_FAILED;
    scoped_ptr<ResourceEntryVector> entries;
    file_system_->ReadDirectory(
        file_path,
        google_apis::test_util::CreateCopyResultCallback(&error, &entries));
    test_util::RunBlockingPoolTask();

    return entries.Pass();
  }

  // Returns true if an entry exists at |file_path|.
  bool EntryExists(const base::FilePath& file_path) {
    return GetResourceEntrySync(file_path);
  }

  // Flag for specifying the timestamp of the test filesystem cache.
  enum SetUpTestFileSystemParam {
    USE_OLD_TIMESTAMP,
    USE_SERVER_TIMESTAMP,
  };

  // Sets up a filesystem with directories: drive/root, drive/root/Dir1,
  // drive/root/Dir1/SubDir2 and files drive/root/File1, drive/root/Dir1/File2,
  // drive/root/Dir1/SubDir2/File3. If |use_up_to_date_timestamp| is true, sets
  // the changestamp to 654321, equal to that of "account_metadata.json" test
  // data, indicating the cache is holding the latest file system info.
  void SetUpTestFileSystem(SetUpTestFileSystemParam param) {
    // Destroy the existing resource metadata to close DB.
    resource_metadata_.reset();

    const base::FilePath metadata_dir = temp_dir_.path().AppendASCII("meta");
    ASSERT_TRUE(file_util::CreateDirectory(metadata_dir));
    scoped_ptr<internal::ResourceMetadataStorage,
               test_util::DestroyHelperForTests> metadata_storage(
        new internal::ResourceMetadataStorage(
            metadata_dir, base::MessageLoopProxy::current().get()));

    scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
        resource_metadata(new internal::ResourceMetadata(
            metadata_storage_.get(), base::MessageLoopProxy::current()));

    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->Initialize());

    const int64 changestamp = param == USE_SERVER_TIMESTAMP ? 654321 : 1;
    ASSERT_EQ(FILE_ERROR_OK,
              resource_metadata->SetLargestChangestamp(changestamp));

    // drive/root
    const std::string root_resource_id =
        fake_drive_service_->GetRootResourceId();
    std::string local_id;
    ASSERT_EQ(FILE_ERROR_OK,
              resource_metadata->AddEntry(util::CreateMyDriveRootEntry(
                  root_resource_id), &local_id));
    const std::string root_local_id = local_id;

    // drive/root/File1
    ResourceEntry file1;
    file1.set_title("File1");
    file1.set_resource_id("resource_id:File1");
    file1.set_parent_local_id(root_local_id);
    file1.mutable_file_specific_info()->set_md5("md5");
    file1.mutable_file_info()->set_is_directory(false);
    file1.mutable_file_info()->set_size(1048576);
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->AddEntry(file1, &local_id));

    // drive/root/Dir1
    ResourceEntry dir1;
    dir1.set_title("Dir1");
    dir1.set_resource_id("resource_id:Dir1");
    dir1.set_parent_local_id(root_local_id);
    dir1.mutable_file_info()->set_is_directory(true);
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->AddEntry(dir1, &local_id));
    const std::string dir1_local_id = local_id;

    // drive/root/Dir1/File2
    ResourceEntry file2;
    file2.set_title("File2");
    file2.set_resource_id("resource_id:File2");
    file2.set_parent_local_id(dir1_local_id);
    file2.mutable_file_specific_info()->set_md5("md5");
    file2.mutable_file_info()->set_is_directory(false);
    file2.mutable_file_info()->set_size(555);
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->AddEntry(file2, &local_id));

    // drive/root/Dir1/SubDir2
    ResourceEntry dir2;
    dir2.set_title("SubDir2");
    dir2.set_resource_id("resource_id:SubDir2");
    dir2.set_parent_local_id(dir1_local_id);
    dir2.mutable_file_info()->set_is_directory(true);
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->AddEntry(dir2, &local_id));
    const std::string dir2_local_id = local_id;

    // drive/root/Dir1/SubDir2/File3
    ResourceEntry file3;
    file3.set_title("File3");
    file3.set_resource_id("resource_id:File3");
    file3.set_parent_local_id(dir2_local_id);
    file3.mutable_file_specific_info()->set_md5("md5");
    file3.mutable_file_info()->set_is_directory(false);
    file3.mutable_file_info()->set_size(12345);
    ASSERT_EQ(FILE_ERROR_OK, resource_metadata->AddEntry(file3, &local_id));

    // Recreate resource metadata.
    SetUpResourceMetadataAndFileSystem();
  }

  content::TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_dir_;
  // We don't use TestingProfile::GetPrefs() in favor of having less
  // dependencies to Profile in general.
  scoped_ptr<TestingPrefServiceSimple> pref_service_;

  scoped_ptr<FakeDriveService> fake_drive_service_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<MockDirectoryChangeObserver> mock_directory_observer_;

  scoped_ptr<internal::ResourceMetadataStorage,
             test_util::DestroyHelperForTests> metadata_storage_;
  scoped_ptr<internal::FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
      resource_metadata_;
  scoped_ptr<FileSystem> file_system_;
};

TEST_F(FileSystemTest, DuplicatedAsyncInitialization) {
  base::RunLoop loop;

  int counter = 0;
  const GetResourceEntryCallback& callback = base::Bind(
      &AsyncInitializationCallback, &counter, 2, loop.QuitClosure());

  file_system_->GetResourceEntry(
      base::FilePath(FILE_PATH_LITERAL("drive/root")), callback);
  file_system_->GetResourceEntry(
      base::FilePath(FILE_PATH_LITERAL("drive/root")), callback);
  loop.Run();  // Wait to get our result
  EXPECT_EQ(2, counter);

  // Although GetResourceEntry() was called twice, the resource list
  // should only be loaded once. In the past, there was a bug that caused
  // it to be loaded twice.
  EXPECT_EQ(1, fake_drive_service_->resource_list_load_count());
  // See the comment in GetMyDriveRoot test case why this is 2.
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());

  // "Fast fetch" will fire an OnirectoryChanged event.
  ASSERT_EQ(1u, mock_directory_observer_->changed_directories().size());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("drive")),
            mock_directory_observer_->changed_directories()[0]);
}

TEST_F(FileSystemTest, GetGrandRootEntry) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(util::kDriveGrandRootLocalId, entry->resource_id());

  // Getting the grand root entry should not cause the resource load to happen.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetOtherDirEntry) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/other"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(util::kDriveOtherDirLocalId, entry->resource_id());

  // Getting the "other" directory entry should not cause the resource load to
  // happen.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());
}

TEST_F(FileSystemTest, GetMyDriveRoot) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ(fake_drive_service_->GetRootResourceId(), entry->resource_id());

  // Absence of "drive/root" in the local metadata triggers the "fast fetch"
  // of "drive" directory. Fetch of "drive" grand root directory has a special
  // implementation. Instead of normal GetResourceListInDirectory(), it is
  // emulated by calling GetAboutResource() so that the resource_id of
  // "drive/root" is listed.
  // Together with the normal GetAboutResource() call to retrieve the largest
  // changestamp, the method is called twice.
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());

  // After "fast fetch" is done, full resource list is fetched.
  EXPECT_EQ(1, fake_drive_service_->resource_list_load_count());

  // "Fast fetch" will fire an OnirectoryChanged event.
  ASSERT_EQ(1u, mock_directory_observer_->changed_directories().size());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("drive")),
            mock_directory_observer_->changed_directories()[0]);
}

TEST_F(FileSystemTest, GetExistingFile) {
  // Simulate the situation that full feed fetching takes very long time,
  // to test the recursive "fast fetch" feature is properly working.
  fake_drive_service_->set_never_return_all_resource_list(true);

  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/Directory 1/SubDirectory File 1.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:subdirectory_file_1_id", entry->resource_id());

  // One server changestamp check (about_resource), three directory load for
  // "drive", "drive/root", and "drive/root/Directory 1", and one background
  // full resource list loading. Note that the directory load for "drive" is
  // special and resorts to about_resource.
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(2, fake_drive_service_->directory_load_count());
  EXPECT_EQ(1, fake_drive_service_->blocked_resource_list_load_count());
}

TEST_F(FileSystemTest, GetExistingDocument) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/Document 1 excludeDir-test.gdoc"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("document:5_document_resource_id", entry->resource_id());
}

TEST_F(FileSystemTest, GetNonExistingFile) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/nonexisting.file"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  EXPECT_FALSE(entry);
}

TEST_F(FileSystemTest, GetExistingDirectory) {
  const base::FilePath kFilePath(FILE_PATH_LITERAL("drive/root/Directory 1"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  ASSERT_EQ("folder:1_folder_resource_id", entry->resource_id());

  // The changestamp should be propagated to the directory.
  EXPECT_EQ(fake_drive_service_->largest_changestamp(),
            entry->directory_specific_info().changestamp());
}

TEST_F(FileSystemTest, GetInSubSubdir) {
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/root/Directory 1/Sub Directory Folder/"
                        "Sub Sub Directory Folder"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  ASSERT_EQ("folder:sub_sub_directory_folder_id", entry->resource_id());
}

TEST_F(FileSystemTest, GetOrphanFile) {
  ASSERT_TRUE(LoadFullResourceList());

  // Entry without parents are placed under "drive/other".
  const base::FilePath kFilePath(
      FILE_PATH_LITERAL("drive/other/Orphan File 1.txt"));
  scoped_ptr<ResourceEntry> entry = GetResourceEntrySync(kFilePath);
  ASSERT_TRUE(entry);
  EXPECT_EQ("file:1_orphanfile_resource_id", entry->resource_id());
}

TEST_F(FileSystemTest, ReadDirectory_Root) {
  // ReadDirectory() should kick off the resource list loading.
  scoped_ptr<ResourceEntryVector> entries(
      ReadDirectorySync(base::FilePath::FromUTF8Unsafe("drive")));
  // The root directory should be read correctly.
  ASSERT_TRUE(entries);
  ASSERT_EQ(2U, entries->size());

  // The found two directories should be /drive/root and /drive/other.
  bool found_other = false;
  bool found_my_drive = false;
  for (size_t i = 0; i < entries->size(); ++i) {
    const base::FilePath title =
        base::FilePath::FromUTF8Unsafe((*entries)[i].title());
    if (title == base::FilePath(util::kDriveOtherDirName)) {
      found_other = true;
    } else if (title == base::FilePath(util::kDriveMyDriveRootDirName)) {
      found_my_drive = true;
    }
  }

  EXPECT_TRUE(found_other);
  EXPECT_TRUE(found_my_drive);

  ASSERT_EQ(1u, mock_directory_observer_->changed_directories().size());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("drive")),
            mock_directory_observer_->changed_directories()[0]);
}

TEST_F(FileSystemTest, ReadDirectory_NonRootDirectory) {
  // ReadDirectory() should kick off the resource list loading.
  scoped_ptr<ResourceEntryVector> entries(
      ReadDirectorySync(
          base::FilePath::FromUTF8Unsafe("drive/root/Directory 1")));
  // The non root directory should also be read correctly.
  // There was a bug (crbug.com/181487), which broke this behavior.
  // Make sure this is fixed.
  ASSERT_TRUE(entries);
  EXPECT_EQ(3U, entries->size());
}

TEST_F(FileSystemTest, LoadFileSystemFromUpToDateCache) {
  ASSERT_NO_FATAL_FAILURE(SetUpTestFileSystem(USE_SERVER_TIMESTAMP));

  // Kicks loading of cached file system and query for server update.
  EXPECT_TRUE(ReadDirectorySync(util::GetDriveMyDriveRootPath()));

  // SetUpTestFileSystem and "account_metadata.json" have the same
  // changestamp (i.e. the local metadata is up-to-date), so no request for
  // new resource list (i.e., call to GetResourceList) should happen.
  EXPECT_EQ(1, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(0, fake_drive_service_->resource_list_load_count());

  // Since the file system has verified that it holds the latest snapshot,
  // it should change its state to "loaded", which admits periodic refresh.
  // To test it, call CheckForUpdates and verify it does try to check updates.
  file_system_->CheckForUpdates();
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(2, fake_drive_service_->about_resource_load_count());
}

TEST_F(FileSystemTest, LoadFileSystemFromCacheWhileOffline) {
  ASSERT_NO_FATAL_FAILURE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));

  // Make GetResourceList fail for simulating offline situation. This will
  // leave the file system "loaded from cache, but not synced with server"
  // state.
  fake_drive_service_->set_offline(true);

  // Load the root.
  EXPECT_TRUE(ReadDirectorySync(util::GetDriveGrandRootPath()));
  // Loading of about resource should not happen as it's offline.
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());

  // Load "My Drive".
  EXPECT_TRUE(ReadDirectorySync(util::GetDriveMyDriveRootPath()));
  EXPECT_EQ(0, fake_drive_service_->about_resource_load_count());

  // Tests that cached data can be loaded even if the server is not reachable.
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/File1"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/File2"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/SubDir2"))));
  EXPECT_TRUE(EntryExists(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/SubDir2/File3"))));

  // Since the file system has at least succeeded to load cached snapshot,
  // the file system should be able to start periodic refresh.
  // To test it, call CheckForUpdates and verify it does try to check
  // updates, which will cause directory changes.
  fake_drive_service_->set_offline(false);

  file_system_->CheckForUpdates();

  test_util::RunBlockingPoolTask();
  EXPECT_EQ(1, fake_drive_service_->about_resource_load_count());
  EXPECT_EQ(1, fake_drive_service_->change_list_load_count());

  ASSERT_LE(1u, mock_directory_observer_->changed_directories().size());
}

TEST_F(FileSystemTest, ReadDirectoryWhileRefreshing) {
  // Enter the "refreshing" state so the fast fetch will be performed.
  ASSERT_NO_FATAL_FAILURE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // The list of resources in "drive/root/Dir1" should be fetched.
  EXPECT_TRUE(ReadDirectorySync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1"))));
  EXPECT_EQ(1, fake_drive_service_->directory_load_count());

  ASSERT_LE(1u, mock_directory_observer_->changed_directories().size());
}

TEST_F(FileSystemTest, GetResourceEntryExistingWhileRefreshing) {
  // Enter the "refreshing" state.
  ASSERT_NO_FATAL_FAILURE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // If an entry is already found in local metadata, no directory fetch happens.
  EXPECT_TRUE(GetResourceEntrySync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/File2"))));
  EXPECT_EQ(0, fake_drive_service_->directory_load_count());
}

TEST_F(FileSystemTest, GetResourceEntryNonExistentWhileRefreshing) {
  // Enter the "refreshing" state so the fast fetch will be performed.
  ASSERT_NO_FATAL_FAILURE(SetUpTestFileSystem(USE_OLD_TIMESTAMP));
  file_system_->CheckForUpdates();

  // If an entry is not found, parent directory's resource list is fetched.
  EXPECT_FALSE(GetResourceEntrySync(base::FilePath(
      FILE_PATH_LITERAL("drive/root/Dir1/NonExistentFile"))));
  EXPECT_EQ(1, fake_drive_service_->directory_load_count());

  ASSERT_LE(1u, mock_directory_observer_->changed_directories().size());
}

TEST_F(FileSystemTest, CreateDirectoryByImplicitLoad) {
  // Intentionally *not* calling LoadFullResourceList(), for testing that
  // CreateDirectory ensures the resource list is loaded before it runs.

  base::FilePath existing_directory(
      FILE_PATH_LITERAL("drive/root/Directory 1"));
  FileError error = FILE_ERROR_FAILED;
  file_system_->CreateDirectory(
      existing_directory,
      true,  // is_exclusive
      false,  // is_recursive
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();

  // It should fail because is_exclusive is set to true.
  EXPECT_EQ(FILE_ERROR_EXISTS, error);
}

TEST_F(FileSystemTest, PinAndUnpin) {
  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_path(FILE_PATH_LITERAL("drive/root/File 1.txt"));

  // Get the file info.
  scoped_ptr<ResourceEntry> entry(GetResourceEntrySync(file_path));
  ASSERT_TRUE(entry);

  // Pin the file.
  FileError error = FILE_ERROR_FAILED;
  file_system_->Pin(file_path,
                    google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  FileCacheEntry cache_entry;
  EXPECT_TRUE(cache_->GetCacheEntry(entry->local_id(), &cache_entry));
  EXPECT_TRUE(cache_entry.is_pinned());
  EXPECT_TRUE(cache_entry.is_present());

  // Unpin the file.
  error = FILE_ERROR_FAILED;
  file_system_->Unpin(file_path,
                      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  EXPECT_TRUE(cache_->GetCacheEntry(entry->local_id(), &cache_entry));
  EXPECT_FALSE(cache_entry.is_pinned());

  // Pinned file gets synced and it results in entry state changes.
  ASSERT_EQ(1u, mock_directory_observer_->changed_directories().size());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("drive/root")),
            mock_directory_observer_->changed_directories()[0]);
}

TEST_F(FileSystemTest, PinAndUnpin_NotSynced) {
  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_path(FILE_PATH_LITERAL("drive/root/File 1.txt"));

  // Get the file info.
  scoped_ptr<ResourceEntry> entry(GetResourceEntrySync(file_path));
  ASSERT_TRUE(entry);

  // Unpin the file just after pinning. File fetch should be cancelled.
  FileError error_pin = FILE_ERROR_FAILED;
  file_system_->Pin(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error_pin));

  FileError error_unpin = FILE_ERROR_FAILED;
  file_system_->Unpin(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error_unpin));

  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error_pin);
  EXPECT_EQ(FILE_ERROR_OK, error_unpin);

  // No cache file available because the sync was cancelled by Unpin().
  FileCacheEntry cache_entry;
  EXPECT_FALSE(cache_->GetCacheEntry(entry->local_id(), &cache_entry));
}

TEST_F(FileSystemTest, GetAvailableSpace) {
  FileError error = FILE_ERROR_OK;
  int64 bytes_total;
  int64 bytes_used;
  file_system_->GetAvailableSpace(
      google_apis::test_util::CreateCopyResultCallback(
          &error, &bytes_total, &bytes_used));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(GG_LONGLONG(6789012345), bytes_used);
  EXPECT_EQ(GG_LONGLONG(9876543210), bytes_total);
}

TEST_F(FileSystemTest, MarkCacheFileAsMountedAndUnmounted) {
  ASSERT_TRUE(LoadFullResourceList());

  base::FilePath file_in_root(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  scoped_ptr<ResourceEntry> entry(GetResourceEntrySync(file_in_root));
  ASSERT_TRUE(entry);

  // Write to cache.
  ASSERT_EQ(FILE_ERROR_OK, cache_->Store(
      entry->local_id(),
      entry->file_specific_info().md5(),
      google_apis::test_util::GetTestFilePath("gdata/root_feed.json"),
      internal::FileCache::FILE_OPERATION_COPY));

  // Test for mounting.
  FileError error = FILE_ERROR_FAILED;
  base::FilePath file_path;
  file_system_->MarkCacheFileAsMounted(
      file_in_root,
      google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Cannot remove a cache entry while it's being mounted.
  EXPECT_EQ(FILE_ERROR_IN_USE, cache_->Remove(entry->local_id()));

  // Test for unmounting.
  error = FILE_ERROR_FAILED;
  file_system_->MarkCacheFileAsUnmounted(
      file_path,
      google_apis::test_util::CreateCopyResultCallback(&error));
  test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  // Now able to remove the cache entry.
  EXPECT_EQ(FILE_ERROR_OK, cache_->Remove(entry->local_id()));
}

TEST_F(FileSystemTest, GetShareUrl) {
  ASSERT_TRUE(LoadFullResourceList());

  const base::FilePath kFileInRoot(FILE_PATH_LITERAL("drive/root/File 1.txt"));
  const GURL kEmbedOrigin("chrome-extension://test-id");

  // Try to fetch the URL for the sharing dialog.
  FileError error = FILE_ERROR_FAILED;
  GURL share_url;
  file_system_->GetShareUrl(
      kFileInRoot,
      kEmbedOrigin,
      google_apis::test_util::CreateCopyResultCallback(&error, &share_url));
  test_util::RunBlockingPoolTask();

  // Verify the share url to the sharing dialog.
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_EQ(GURL("https://file_link_share/"), share_url);
}

TEST_F(FileSystemTest, GetShareUrlNotAvailable) {
  ASSERT_TRUE(LoadFullResourceList());

  const base::FilePath kFileInRoot(
      FILE_PATH_LITERAL("drive/root/Directory 1/SubDirectory File 1.txt"));
  const GURL kEmbedOrigin("chrome-extension://test-id");

  // Try to fetch the URL for the sharing dialog.
  FileError error = FILE_ERROR_FAILED;
  GURL share_url;

  file_system_->GetShareUrl(
      kFileInRoot,
      kEmbedOrigin,
      google_apis::test_util::CreateCopyResultCallback(&error, &share_url));
  test_util::RunBlockingPoolTask();

  // Verify the error and the share url, which should be empty.
  EXPECT_EQ(FILE_ERROR_FAILED, error);
  EXPECT_TRUE(share_url.is_empty());
}

}   // namespace drive
