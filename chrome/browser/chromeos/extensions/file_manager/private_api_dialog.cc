// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/extensions/file_manager/private_api_dialog.h"

#include "chrome/browser/chromeos/extensions/file_manager/private_api_util.h"
#include "chrome/browser/ui/views/select_file_dialog_extension.h"
#include "chrome/common/extensions/api/file_browser_private.h"
#include "content/public/browser/browser_thread.h"
#include "ui/shell_dialogs/selected_file_info.h"

using content::BrowserThread;

namespace extensions {

namespace {

// Computes the routing ID for SelectFileDialogExtension from the |dispatcher|.
SelectFileDialogExtension::RoutingID GetFileDialogRoutingID(
    ExtensionFunctionDispatcher* dispatcher) {
  return SelectFileDialogExtension::GetRoutingIDFromWebContents(
      file_manager::util::GetWebContents(dispatcher));
}

}  // namespace

bool FileBrowserPrivateCancelDialogFunction::RunImpl() {
  const SelectFileDialogExtension::RoutingID routing_id =
      GetFileDialogRoutingID(dispatcher());
  SelectFileDialogExtension::OnFileSelectionCanceled(routing_id);
  SendResponse(true);
  return true;
}

bool FileBrowserPrivateSelectFileFunction::RunImpl() {
  using extensions::api::file_browser_private::SelectFile::Params;
  const scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  std::vector<GURL> file_paths;
  file_paths.push_back(GURL(params->selected_path));

  file_manager::util::GetSelectedFileInfoLocalPathOption option =
      file_manager::util::NO_LOCAL_PATH_RESOLUTION;
  if (params->should_return_local_path) {
    option = params->for_opening ?
        file_manager::util::NEED_LOCAL_PATH_FOR_OPENING :
        file_manager::util::NEED_LOCAL_PATH_FOR_SAVING;
  }

  file_manager::util::GetSelectedFileInfo(
      render_view_host(),
      GetProfile(),
      file_paths,
      option,
      base::Bind(
          &FileBrowserPrivateSelectFileFunction::GetSelectedFileInfoResponse,
          this,
          params->index));
  return true;
}

void FileBrowserPrivateSelectFileFunction::GetSelectedFileInfoResponse(
    int index,
    const std::vector<ui::SelectedFileInfo>& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (files.size() != 1) {
    SendResponse(false);
    return;
  }
  const SelectFileDialogExtension::RoutingID routing_id =
      GetFileDialogRoutingID(dispatcher());
  SelectFileDialogExtension::OnFileSelected(routing_id, files[0], index);
  SendResponse(true);
}

bool FileBrowserPrivateSelectFilesFunction::RunImpl() {
  using extensions::api::file_browser_private::SelectFiles::Params;
  const scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params);

  const size_t len = params->selected_paths.size();
  std::vector<GURL> file_urls;
  file_urls.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    file_urls.push_back(GURL(params->selected_paths[i]));
  }

  file_manager::util::GetSelectedFileInfo(
      render_view_host(),
      GetProfile(),
      file_urls,
      params->should_return_local_path ?
          file_manager::util::NEED_LOCAL_PATH_FOR_OPENING :
          file_manager::util::NO_LOCAL_PATH_RESOLUTION,
      base::Bind(
          &FileBrowserPrivateSelectFilesFunction::GetSelectedFileInfoResponse,
          this));
  return true;
}

void FileBrowserPrivateSelectFilesFunction::GetSelectedFileInfoResponse(
    const std::vector<ui::SelectedFileInfo>& files) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  const SelectFileDialogExtension::RoutingID routing_id =
      GetFileDialogRoutingID(dispatcher());
  SelectFileDialogExtension::OnMultiFilesSelected(routing_id, files);
  SendResponse(true);
}

}  // namespace extensions
