// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/compiler_specific.h"
#include "chrome/browser/download/download_danger_prompt.h"
#include "chrome/browser/ui/views/constrained_window_views.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager_delegate.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_danger_type.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_client_view.h"
#include "ui/views/window/dialog_delegate.h"

using content::BrowserThread;
using web_modal::WebContentsModalDialogManager;
using web_modal::WebContentsModalDialogManagerDelegate;

namespace {

const int kMessageWidth = 320;
const int kParagraphPadding = 15;

// Views-specific implementation of download danger prompt dialog. We use this
// class rather than a TabModalConfirmDialog so that we can use custom
// formatting on the text in the body of the dialog.
class DownloadDangerPromptViews : public DownloadDangerPrompt,
                                  public content::DownloadItem::Observer,
                                  public views::DialogDelegate {
 public:
  DownloadDangerPromptViews(content::DownloadItem* item,
                            content::WebContents* web_contents,
                            bool show_context,
                            const OnDone& done);

  // DownloadDangerPrompt methods:
  virtual void InvokeActionForTesting(Action action) OVERRIDE;

  // views::DialogDelegate methods:
  virtual string16 GetDialogButtonLabel(ui::DialogButton button) const OVERRIDE;
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual ui::ModalType GetModalType() const OVERRIDE;
  virtual bool Cancel() OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual bool Close() OVERRIDE;
  // TODO(wittman): Remove this override once we move to the new style frame
  // view on all dialogs.
  virtual views::NonClientFrameView* CreateNonClientFrameView(
      views::Widget* widget) OVERRIDE;
  virtual views::View* GetInitiallyFocusedView() OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;
  virtual const views::Widget* GetWidget() const OVERRIDE;

  // content::DownloadItem::Observer:
  virtual void OnDownloadUpdated(content::DownloadItem* download) OVERRIDE;

 private:
  string16 GetAcceptButtonTitle() const;
  string16 GetCancelButtonTitle() const;
  // The message lead is separated from the main text and is bolded.
  string16 GetMessageLead() const;
  string16 GetMessageBody() const;
  void RunDone(Action action);

  content::DownloadItem* download_;
  content::WebContents* web_contents_;
  bool show_context_;
  OnDone done_;

  views::View* contents_view_;
};

DownloadDangerPromptViews::DownloadDangerPromptViews(
    content::DownloadItem* item,
    content::WebContents* web_contents,
    bool show_context,
    const OnDone& done)
    : download_(item),
      web_contents_(web_contents),
      show_context_(show_context),
      done_(done),
      contents_view_(NULL) {
  DCHECK(!done_.is_null());
  download_->AddObserver(this);

  contents_view_ = new views::View;

  views::GridLayout* layout = views::GridLayout::CreatePanel(contents_view_);
  contents_view_->SetLayoutManager(layout);

  views::ColumnSet* column_set = layout->AddColumnSet(0);
  column_set->AddColumn(views::GridLayout::FILL, views::GridLayout::FILL, 1,
                        views::GridLayout::FIXED, kMessageWidth, 0);

  const string16 message_lead = GetMessageLead();

  if (!message_lead.empty()) {
    views::Label* message_lead_label = new views::Label(message_lead);
    message_lead_label->SetMultiLine(true);
    message_lead_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    message_lead_label->SetAllowCharacterBreak(true);

    gfx::FontList font_list(gfx::Font().DeriveFont(0, gfx::Font::BOLD));
    message_lead_label->SetFontList(font_list);

    layout->StartRow(0, 0);
    layout->AddView(message_lead_label);

    layout->AddPaddingRow(0, kParagraphPadding);
  }

  views::Label* message_body_label = new views::Label(GetMessageBody());
  message_body_label->SetMultiLine(true);
  message_body_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  message_body_label->SetAllowCharacterBreak(true);

  layout->StartRow(0, 0);
  layout->AddView(message_body_label);
}

// DownloadDangerPrompt methods:
void DownloadDangerPromptViews::InvokeActionForTesting(Action action) {
  switch (action) {
    case ACCEPT:
      Accept();
      break;

    case CANCEL:
    case DISMISS:
      Cancel();
      break;

    default:
      NOTREACHED();
      break;
  }
}

// views::DialogDelegate methods:
string16 DownloadDangerPromptViews::GetDialogButtonLabel(
    ui::DialogButton button) const {
  switch (button) {
    case ui::DIALOG_BUTTON_OK:
      return GetAcceptButtonTitle();

    case ui::DIALOG_BUTTON_CANCEL:
      return GetCancelButtonTitle();

    default:
      return DialogDelegate::GetDialogButtonLabel(button);
  };
}

string16 DownloadDangerPromptViews::GetWindowTitle() const {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CONFIRM_KEEP_DANGEROUS_DOWNLOAD_TITLE);
  else
    return l10n_util::GetStringUTF16(IDS_RESTORE_KEEP_DANGEROUS_DOWNLOAD_TITLE);
}

void DownloadDangerPromptViews::DeleteDelegate() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  delete this;
}

ui::ModalType DownloadDangerPromptViews::GetModalType() const {
#if defined(USE_ASH)
  return ui::MODAL_TYPE_CHILD;
#else
  return views::WidgetDelegate::GetModalType();
#endif
}

bool DownloadDangerPromptViews::Cancel() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  RunDone(CANCEL);
  return true;
}

bool DownloadDangerPromptViews::Accept() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  RunDone(ACCEPT);
  return true;
}

bool DownloadDangerPromptViews::Close() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  RunDone(DISMISS);
  return true;
}

// TODO(wittman): Remove this override once we move to the new style frame
// view on all dialogs.
views::NonClientFrameView* DownloadDangerPromptViews::CreateNonClientFrameView(
    views::Widget* widget) {
  return CreateConstrainedStyleNonClientFrameView(
      widget,
      web_contents_->GetBrowserContext());
}

views::View* DownloadDangerPromptViews::GetInitiallyFocusedView() {
  return GetDialogClientView()->cancel_button();
}

views::View* DownloadDangerPromptViews::GetContentsView() {
  return contents_view_;
}

views::Widget* DownloadDangerPromptViews::GetWidget() {
  return contents_view_->GetWidget();
}

const views::Widget* DownloadDangerPromptViews::GetWidget() const {
  return contents_view_->GetWidget();
}

// content::DownloadItem::Observer:
void DownloadDangerPromptViews::OnDownloadUpdated(
    content::DownloadItem* download) {
  // If the download is nolonger dangerous (accepted externally) or the download
  // is in a terminal state, then the download danger prompt is no longer
  // necessary.
  if (!download_->IsDangerous() || download_->IsDone()) {
    RunDone(DISMISS);
    Cancel();
  }
}

string16 DownloadDangerPromptViews::GetAcceptButtonTitle() const {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST: {
      return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD_AGAIN_MALICIOUS);
    }
    default:
      return l10n_util::GetStringUTF16(IDS_CONFIRM_DOWNLOAD_AGAIN);
  }
}

string16 DownloadDangerPromptViews::GetCancelButtonTitle() const {
  if (show_context_)
    return l10n_util::GetStringUTF16(IDS_CANCEL);
  switch (download_->GetDangerType()) {
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
    case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST: {
      return l10n_util::GetStringUTF16(IDS_CONFIRM_CANCEL_AGAIN_MALICIOUS);
    }
    default:
      return l10n_util::GetStringUTF16(IDS_CANCEL);
  }
}

string16 DownloadDangerPromptViews::GetMessageLead() const {
  if (!show_context_) {
    switch (download_->GetDangerType()) {
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_URL:
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_CONTENT:
      case content::DOWNLOAD_DANGER_TYPE_DANGEROUS_HOST:
        return l10n_util::GetStringUTF16(
            IDS_PROMPT_CONFIRM_KEEP_MALICIOUS_DOWNLOAD_LEAD);

      default:
        break;
    }
  }

  return string16();
}

string16 DownloadDangerPromptViews::GetMessageBody() const {
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

void DownloadDangerPromptViews::RunDone(Action action) {
  // Invoking the callback can cause the download item state to change or cause
  // the window to close, and |callback| refers to a member variable.
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

DownloadDangerPrompt* DownloadDangerPrompt::Create(
    content::DownloadItem* item,
    content::WebContents* web_contents,
    bool show_context,
    const OnDone& done) {
  DownloadDangerPromptViews* download_danger_prompt =
      new DownloadDangerPromptViews(item, web_contents, show_context, done);

  WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      WebContentsModalDialogManager::FromWebContents(web_contents);
  WebContentsModalDialogManagerDelegate* modal_delegate =
      web_contents_modal_dialog_manager->delegate();
  CHECK(modal_delegate);
  views::Widget* dialog = views::Widget::CreateWindowAsFramelessChild(
      download_danger_prompt,
      web_contents->GetView()->GetNativeView(),
      modal_delegate->GetWebContentsModalDialogHost()->GetHostView());
  web_contents_modal_dialog_manager->ShowDialog(dialog->GetNativeView());

  return download_danger_prompt;
}
