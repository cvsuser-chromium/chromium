// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_APP_LIST_LINUX_APP_LIST_SERVICE_LINUX_H_
#define CHROME_BROWSER_UI_VIEWS_APP_LIST_LINUX_APP_LIST_SERVICE_LINUX_H_

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/app_list/app_list_service_impl.h"

template <typename T> struct DefaultSingletonTraits;

class AppListShower;

// AppListServiceLinux manages global resources needed for the app list to
// operate, and controls when the app list is opened and closed.
class AppListServiceLinux : public AppListServiceImpl {
 public:
  virtual ~AppListServiceLinux();

  static AppListServiceLinux* GetInstance();
  void set_can_close(bool can_close);
  void OnAppListClosing();

  // AppListService overrides:
  virtual void Init(Profile* initial_profile) OVERRIDE;
  virtual void CreateForProfile(Profile* requested_profile) OVERRIDE;
  virtual void ShowForProfile(Profile* requested_profile) OVERRIDE;
  virtual void DismissAppList() OVERRIDE;
  virtual bool IsAppListVisible() const OVERRIDE;
  virtual gfx::NativeWindow GetAppListWindow() OVERRIDE;
  virtual Profile* GetCurrentAppListProfile() OVERRIDE;
  virtual AppListControllerDelegate* CreateControllerDelegate() OVERRIDE;

  // AppListServiceImpl overrides:
  virtual void CreateShortcut() OVERRIDE;

 private:
  friend struct DefaultSingletonTraits<AppListServiceLinux>;

  AppListServiceLinux();

  // Responsible for putting views on the screen.
  scoped_ptr<AppListShower> shower_;

  DISALLOW_COPY_AND_ASSIGN(AppListServiceLinux);
};

#endif  // CHROME_BROWSER_UI_VIEWS_APP_LIST_LINUX_APP_LIST_SERVICE_LINUX_H_
