// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_danger_prompt.h"

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_stats.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_delegate.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_item.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// TODO(wittman): Create a native web contents modal dialog implementation of
// this dialog for non-Views platforms, to support bold formatting of the
// message lead.

// Implements DownloadDangerPrompt using a TabModalConfirmDialog.
class DownloadDangerPromptImpl : public DownloadDangerPrompt,
                                 public content::DownloadItem::Observer,
                                 public TabModalConfirmDialogDelegate {
 public:
  DownloadDangerPromptImpl(content::DownloadItem* item,
                           content::WebContents* web_contents,
                           bool show_context,
                           const OnDone& done);
  virtual ~DownloadDangerPromptImpl();

  // DownloadDangerPrompt:
  virtual void InvokeActionForTesting(Action action) OVERRIDE;

 private:
  // content::DownloadItem::Observer:
  virtual void OnDownloadUpdated(content::DownloadItem* download) OVERRIDE;

  // TabModalConfirmDialogDelegate:
  virtual string16 GetTitle() OVERRIDE;
  virtual string16 GetMessage() OVERRIDE;
  virtual string16 GetAcceptButtonTitle() OVERRIDE;
  virtual string16 GetCancelButtonTitle() OVERRIDE;
  virtual void OnAccepted() OVERRIDE;
  virtual void OnCanceled() OVERRIDE;
  virtual void OnClosed() OVERRIDE;

  void RunDone(Action action);

  content::DownloadItem* download_;
  bool show_context_;
  OnDone done_;

  DISALLOW_COPY_AND_ASSIGN(DownloadDangerPromptImpl);
};

DownloadDangerPromptImpl::DownloadDangerPromptImpl(
    content::DownloadItem* download,
    content::WebContents* web_contents,
    bool show_context,
    const OnDone& done)
    : TabModalConfirmDialogDelegate(web_contents),
      download_(download),
      show_context_(show_context),
      done_(done) {
  DCHECK(!done_.is_null());
  download_->AddObserver(this);
  RecordOpenedDangerousConfirmDialog(download_->GetDangerType());
}

DownloadDangerPromptImpl::~DownloadDangerPromptImpl() {
  // |this| might be deleted without invoking any callbacks. E.g. pressing Esc
  // on GTK or if the user navigates away from the page showing the prompt.
  RunDone(DISMISS);
}

void DownloadDangerPromptImpl::InvokeActionForTesting(Action action) {
  switch (action) {
    case ACCEPT: Accept(); break;
    case CANCEL: Cancel(); break;
    case DISMISS:
      RunDone(DISMISS);
      Cancel();
      break;
  }
}

void DownloadDangerPromptImpl::OnDownloadUpdated(
    content::DownloadItem* download) {
  // If the download is nolonger dangerous (accepted externally) or the download
  // is in a terminal state, then the download danger prompt is no longer
  // necessary.
  if (!download->IsDangerous() || download->IsDone()) {
    RunDone(DISMISS);
    Cancel();
  }
}

string16 DownloadDangerPromptImpl::GetTitle() {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CONFIRM_KEEP_DANGEROUS_DOWNLOAD_TITLE);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED: {
      return l10n_util::GetStringUTF16(
          IDS_RESTORE_KEEP_DANGEROUS_DOWNLOAD_TITLE);
    }
    default: {
      return l10n_util::GetStringUTF16(
          IDS_CONFIRM_KEEP_DANGEROUS_DOWNLOAD_TITLE);
    }
  }
}

string16 DownloadDangerPromptImpl::GetMessage() {
  if (show_context_) {
    switch (download_->GetDangerType()) {
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_FILE: {
        return l10n_util::GetStringFUTF16(
            IDS_PROMPT_DANGEROUS_DOWNLOAD,
            download_->GetFileNameToReportUser().LossyDisplayName());
      }
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL: // Fall through
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST: {
        return l10n_util::GetStringFUTF16(
            IDS_PROMPT_MALICIOUS_DOWNLOAD_CONTENT,
            download_->GetFileNameToReportUser().LossyDisplayName());
      }
      case content::DOWNLOAD_DANGER_TYPE_UNCOMMON_CONTENT: {
        return l10n_util::GetStringFUTF16(
            IDS_PROMPT_UNCOMMON_DOWNLOAD_CONTENT,
            download_->GetFileNameToReportUser().LossyDisplayName());
      }
      case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED: {
        return l10n_util::GetStringFUTF16(
            IDS_PROMPT_DOWNLOAD_CHANGES_SETTINGS,
            download_->GetFileNameToReportUser().LossyDisplayName());
      }
      case content::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS:
      case content::DOWNLOAD_DANGER_TYPE_MAYBE_DANGEROUS_CONTENT:
      case content::DOWNLOAD_DANGER_TYPE_USER_VALIDATED:
      case content::DOWNLOAD_DANGER_TYPE_MAX: {
        break;
      }
    }
  } else {
    switch (download_->GetDangerType()) {
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST: {
        return l10n_util::GetStringUTF16(
            IDS_PROMPT_CONFIRM_KEEP_MALICIOUS_DOWNLOAD_LEAD) +
            ASCIIToUTF16("\n\n") +
            l10n_util::GetStringUTF16(
                IDS_PROMPT_CONFIRM_KEEP_MALICIOUS_DOWNLOAD_BODY);
      }
      default: {
        return l10n_util::GetStringUTF16(
            IDS_PROMPT_CONFIRM_KEEP_DANGEROUS_DOWNLOAD);
      }
    }
  }
  NOTREACHED();
  return string16();
}

string16 DownloadDangerPromptImpl::GetAcceptButtonTitle() {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED: {
      return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD_AGAIN_MALICIOUS);
    }
    default:
      return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD_AGAIN);
  }
}

string16 DownloadDangerPromptImpl::GetCancelButtonTitle() {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CANCEL);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
    case content::DOWNLOAD_DANGER_TYPE_POTENTIALLY_UNWANTED: {
      return l10n_util::GetStringUTF16(IDS_CONFIRM_CANCEL_AGAIN_MALICIOUS);
    }
    default:
      return l10n_util::GetStringUTF16(IDS_CANCEL);
  }
}

void DownloadDangerPromptImpl::OnAccepted() {
  RunDone(ACCEPT);
}

void DownloadDangerPromptImpl::OnCanceled() {
  RunDone(CANCEL);
}

void DownloadDangerPromptImpl::OnClosed() {
  RunDone(DISMISS);
}

void DownloadDangerPromptImpl::RunDone(Action action) {
  // Invoking the callback can cause the download item state to change or cause
  // the constrained window to close, and |callback| refers to a member
  // variable.
  OnDone done = done_;
  done_.Reset();
  if (download_ != NULL) {
    download_->RemoveObserver(this);
    download_ = NULL;
  }
  if (!done.is_null())
    done.Run(action);
}

}  // namespace

#if !(defined(OS_WIN) || defined(USE_AURA))
// static
DownloadDangerPrompt* DownloadDangerPrompt::Create(
    content::DownloadItem* item,
    content::WebContents* web_contents,
    bool show_context,
    const OnDone& done) {
  DownloadDangerPromptImpl* prompt = new DownloadDangerPromptImpl(
      item, web_contents, show_context, done);
  // |prompt| will be deleted when the dialog is done.
  TabModalConfirmDialog::Create(prompt, web_contents);
  return prompt;
}
#endif
