// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/app_list_item_list.h"

#include "base/memory/scoped_ptr.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/app_list/app_list_folder_item.h"
#include "ui/app_list/app_list_item_list_observer.h"
#include "ui/app_list/app_list_item_model.h"

namespace app_list {

namespace {

class TestObserver : public AppListItemListObserver {
 public:
  TestObserver()
      : items_added_(0),
        items_removed_(0) {
  }

  virtual ~TestObserver() {
  }

  // AppListItemListObserver overriden:
  virtual void OnListItemAdded(size_t index, AppListItemModel* item) OVERRIDE {
    ++items_added_;
  }

  virtual void OnListItemRemoved(size_t index,
                                 AppListItemModel* item) OVERRIDE {
    ++items_removed_;
  }

  size_t items_added() const { return items_added_; }
  size_t items_removed() const { return items_removed_; }

 private:
  size_t items_added_;
  size_t items_removed_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

std::string GetItemName(int id) {
  return base::StringPrintf("Item %d", id);
}

}  // namespace

class AppListItemListTest : public testing::Test {
 public:
  AppListItemListTest() {}
  virtual ~AppListItemListTest() {}

  // testing::Test overrides:
  virtual void SetUp() OVERRIDE {
    item_list_.AddObserver(&observer_);
  }

  virtual void TearDown() OVERRIDE {
    item_list_.RemoveObserver(&observer_);
  }

  AppListItemModel* CreateItem(const std::string& title,
                               const std::string& full_name) {
    AppListItemModel* item = new AppListItemModel(title);
    size_t nitems = item_list_.item_count();
    syncer::StringOrdinal position;
    if (nitems == 0)
      position = syncer::StringOrdinal::CreateInitialOrdinal();
    else
      position = item_list_.item_at(nitems - 1)->position().CreateAfter();
    item->set_position(position);
    item->SetTitleAndFullName(title, full_name);
    return item;
  }

  AppListItemModel* CreateAndAddItem(const std::string& title,
                                     const std::string& full_name) {
    AppListItemModel* item = CreateItem(title, full_name);
    item_list_.AddItem(item);
    return item;
  }

  void VerifyItemListOridinals() {
    for (size_t i = 1; i < item_list_.item_count(); ++i) {
      EXPECT_TRUE(item_list_.item_at(i - 1)->position().LessThan(
          item_list_.item_at(i)->position()));
    }
  }

 protected:
  AppListItemList item_list_;
  TestObserver observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AppListItemListTest);
};

TEST_F(AppListItemListTest, FindItemIndex) {
  AppListItemModel* item_0 = CreateAndAddItem(GetItemName(0), GetItemName(0));
  AppListItemModel* item_1 = CreateAndAddItem(GetItemName(1), GetItemName(1));
  AppListItemModel* item_2 = CreateAndAddItem(GetItemName(2), GetItemName(2));
  EXPECT_EQ(observer_.items_added(), 3u);
  EXPECT_EQ(item_list_.item_count(), 3u);
  EXPECT_EQ(item_0, item_list_.item_at(0));
  EXPECT_EQ(item_1, item_list_.item_at(1));
  EXPECT_EQ(item_2, item_list_.item_at(2));
  VerifyItemListOridinals();

  size_t index;
  EXPECT_TRUE(item_list_.FindItemIndex(item_0->id(), &index));
  EXPECT_EQ(index, 0u);
  EXPECT_TRUE(item_list_.FindItemIndex(item_1->id(), &index));
  EXPECT_EQ(index, 1u);
  EXPECT_TRUE(item_list_.FindItemIndex(item_2->id(), &index));
  EXPECT_EQ(index, 2u);

  scoped_ptr<AppListItemModel> item_3(
      CreateItem(GetItemName(3), GetItemName(3)));
  EXPECT_FALSE(item_list_.FindItemIndex(item_3->id(), &index));
}

TEST_F(AppListItemListTest, RemoveItemAt) {
  AppListItemModel* item_0 = CreateAndAddItem(GetItemName(0), GetItemName(0));
  AppListItemModel* item_1 = CreateAndAddItem(GetItemName(1), GetItemName(1));
  AppListItemModel* item_2 = CreateAndAddItem(GetItemName(2), GetItemName(2));
  EXPECT_EQ(item_list_.item_count(), 3u);
  EXPECT_EQ(observer_.items_added(), 3u);
  size_t index;
  EXPECT_TRUE(item_list_.FindItemIndex(item_1->id(), &index));
  EXPECT_EQ(index, 1u);
  VerifyItemListOridinals();

  scoped_ptr<AppListItemModel> item_removed = item_list_.RemoveItemAt(1);
  EXPECT_EQ(item_removed, item_1);
  EXPECT_FALSE(item_list_.FindItem(item_1->id()));
  EXPECT_EQ(item_list_.item_count(), 2u);
  EXPECT_EQ(observer_.items_removed(), 1u);
  EXPECT_EQ(item_list_.item_at(0), item_0);
  EXPECT_EQ(item_list_.item_at(1), item_2);
  VerifyItemListOridinals();
}

TEST_F(AppListItemListTest, RemoveItem) {
  AppListItemModel* item_0 = CreateAndAddItem(GetItemName(0), GetItemName(0));
  AppListItemModel* item_1 = CreateAndAddItem(GetItemName(1), GetItemName(1));
  AppListItemModel* item_2 = CreateAndAddItem(GetItemName(2), GetItemName(2));
  EXPECT_EQ(item_list_.item_count(), 3u);
  EXPECT_EQ(observer_.items_added(), 3u);
  EXPECT_EQ(item_0, item_list_.item_at(0));
  EXPECT_EQ(item_1, item_list_.item_at(1));
  EXPECT_EQ(item_2, item_list_.item_at(2));
  VerifyItemListOridinals();

  size_t index;
  EXPECT_TRUE(item_list_.FindItemIndex(item_1->id(), &index));
  EXPECT_EQ(index, 1u);

  scoped_ptr<AppListItemModel> item_removed =
      item_list_.RemoveItem(item_1->id());
  EXPECT_EQ(item_removed, item_1);
  EXPECT_FALSE(item_list_.FindItem(item_1->id()));
  EXPECT_EQ(item_list_.item_count(), 2u);
  EXPECT_EQ(observer_.items_removed(), 1u);
  VerifyItemListOridinals();

  scoped_ptr<AppListItemModel> not_found_item = item_list_.RemoveItem("Bogus");
  EXPECT_FALSE(not_found_item.get());
}

TEST_F(AppListItemListTest, InsertItemAt) {
  AppListItemModel* item_0 = CreateAndAddItem(GetItemName(0), GetItemName(0));
  AppListItemModel* item_1 = CreateAndAddItem(GetItemName(1), GetItemName(1));
  EXPECT_EQ(item_list_.item_count(), 2u);
  EXPECT_EQ(observer_.items_added(), 2u);
  EXPECT_EQ(item_list_.item_at(0), item_0);
  EXPECT_EQ(item_list_.item_at(1), item_1);
  VerifyItemListOridinals();

  // Insert an item at the beginning of the item_list_.
  AppListItemModel* item_2 = CreateItem(GetItemName(2), GetItemName(2));
  item_list_.InsertItemAt(item_2, 0);
  EXPECT_EQ(item_list_.item_count(), 3u);
  EXPECT_EQ(observer_.items_added(), 3u);
  EXPECT_EQ(item_list_.item_at(0), item_2);
  EXPECT_EQ(item_list_.item_at(1), item_0);
  EXPECT_EQ(item_list_.item_at(2), item_1);
  VerifyItemListOridinals();

  // Insert an item at the end of the item_list_.
  AppListItemModel* item_3 = CreateItem(GetItemName(3), GetItemName(3));
  item_list_.InsertItemAt(item_3, item_list_.item_count());
  EXPECT_EQ(item_list_.item_count(), 4u);
  EXPECT_EQ(observer_.items_added(), 4u);
  EXPECT_EQ(item_list_.item_at(0), item_2);
  EXPECT_EQ(item_list_.item_at(1), item_0);
  EXPECT_EQ(item_list_.item_at(2), item_1);
  EXPECT_EQ(item_list_.item_at(3), item_3);
  VerifyItemListOridinals();

  // Insert an item at the 2nd item of the item_list_.
  AppListItemModel* item_4 = CreateItem(GetItemName(4), GetItemName(4));
  item_list_.InsertItemAt(item_4, 1);
  EXPECT_EQ(item_list_.item_count(), 5u);
  EXPECT_EQ(observer_.items_added(), 5u);
  EXPECT_EQ(item_list_.item_at(0), item_2);
  EXPECT_EQ(item_list_.item_at(1), item_4);
  EXPECT_EQ(item_list_.item_at(2), item_0);
  EXPECT_EQ(item_list_.item_at(3), item_1);
  EXPECT_EQ(item_list_.item_at(4), item_3);
  VerifyItemListOridinals();
}

TEST_F(AppListItemListTest, InsertItemAtEmptyList) {
  AppListItemModel* item_0 = CreateItem(GetItemName(0), GetItemName(0));
  EXPECT_EQ(item_list_.item_count(), 0u);
  item_list_.InsertItemAt(item_0, 0);
  EXPECT_EQ(item_list_.item_count(), 1u);
  EXPECT_EQ(observer_.items_added(), 1u);
  EXPECT_EQ(item_list_.item_at(0), item_0);
  VerifyItemListOridinals();
}

}  // namespace app_list
