// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/account_chooser_model.h"

#include "base/bind.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

namespace autofill {

const int AccountChooserModel::kAutofillItemId = 0;
const int AccountChooserModel::kWalletAccountsStartId = 1;

AccountChooserModelDelegate::~AccountChooserModelDelegate() {}

AccountChooserModel::AccountChooserModel(
    AccountChooserModelDelegate* delegate,
    Profile* profile,
    const AutofillMetrics& metric_logger)
    : ui::SimpleMenuModel(this),
      delegate_(delegate),
      checked_item_(kWalletAccountsStartId),
      active_wallet_account_(0U),
      had_wallet_error_(false),
      metric_logger_(metric_logger) {
  if (profile->GetPrefs()->GetBoolean(
          ::prefs::kAutofillDialogPayWithoutWallet) ||
      profile->IsOffTheRecord()) {
    checked_item_ = kAutofillItemId;
  }

  ReconstructMenuItems();
}

AccountChooserModel::~AccountChooserModel() {
}

void AccountChooserModel::MenuWillShow() {
  ui::SimpleMenuModel::MenuWillShow();
}

void AccountChooserModel::SelectActiveWalletAccount() {
  ExecuteCommand(kWalletAccountsStartId + active_wallet_account_, 0);
}

void AccountChooserModel::SelectUseAutofill() {
  ExecuteCommand(kAutofillItemId, 0);
}

bool AccountChooserModel::HasAccountsToChoose() const {
  return !wallet_accounts_.empty();
}

void AccountChooserModel::SetWalletAccounts(
    const std::vector<std::string>& accounts) {
  wallet_accounts_ = accounts;
  ReconstructMenuItems();
  delegate_->UpdateAccountChooserView();
}

void AccountChooserModel::ClearWalletAccounts() {
  wallet_accounts_.clear();
  if (WalletIsSelected())
    checked_item_ = kWalletAccountsStartId;

  ReconstructMenuItems();
  delegate_->UpdateAccountChooserView();
}

base::string16 AccountChooserModel::GetActiveWalletAccountName() const {
  if (wallet_accounts_.empty())
    return base::string16();

  return UTF8ToUTF16(wallet_accounts_[GetActiveWalletAccountIndex()]);
}

size_t AccountChooserModel::GetActiveWalletAccountIndex() const {
  return active_wallet_account_;
}

bool AccountChooserModel::IsCommandIdChecked(int command_id) const {
  return command_id == checked_item_;
}

bool AccountChooserModel::IsCommandIdEnabled(int command_id) const {
  // Currently, _any_ (non-sign-in) error disables _all_ Wallet accounts.
  if (command_id != kAutofillItemId && had_wallet_error_)
    return false;

  return true;
}

bool AccountChooserModel::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) {
  return false;
}

void AccountChooserModel::ExecuteCommand(int command_id, int event_flags) {
  if (checked_item_ == command_id)
    return;

  // Log metrics.
  AutofillMetrics::DialogUiEvent chooser_event;
  if (command_id == kAutofillItemId) {
    chooser_event =
        AutofillMetrics::DIALOG_UI_ACCOUNT_CHOOSER_SWITCHED_TO_AUTOFILL;
  } else if (checked_item_ == kAutofillItemId) {
    chooser_event =
        AutofillMetrics::DIALOG_UI_ACCOUNT_CHOOSER_SWITCHED_TO_WALLET;
  } else {
    chooser_event =
        AutofillMetrics::DIALOG_UI_ACCOUNT_CHOOSER_SWITCHED_WALLET_ACCOUNT;
  }
  metric_logger_.LogDialogUiEvent(chooser_event);

  checked_item_ = command_id;
  if (checked_item_ >= kWalletAccountsStartId)
    active_wallet_account_ = checked_item_ - kWalletAccountsStartId;

  ReconstructMenuItems();
  delegate_->AccountChoiceChanged();
}

void AccountChooserModel::MenuWillShow(ui::SimpleMenuModel* source) {
  delegate_->AccountChooserWillShow();
}

void AccountChooserModel::SetHadWalletError() {
  // Any non-sign-in error disables all Wallet accounts.
  had_wallet_error_ = true;
  ClearWalletAccounts();
  ExecuteCommand(kAutofillItemId, 0);
}

void AccountChooserModel::SetHadWalletSigninError() {
  ClearWalletAccounts();
  ExecuteCommand(kAutofillItemId, 0);
}

bool AccountChooserModel::WalletIsSelected() const {
  return checked_item_ != kAutofillItemId;
}

void AccountChooserModel::ReconstructMenuItems() {
  Clear();
  const gfx::Image& wallet_icon =
      ui::ResourceBundle::GetSharedInstance().GetImageNamed(IDR_WALLET_ICON);

  if (!wallet_accounts_.empty()) {
    for (size_t i = 0; i < wallet_accounts_.size(); ++i) {
      int item_id = kWalletAccountsStartId + i;
      AddCheckItem(item_id, UTF8ToUTF16(wallet_accounts_[i]));
      SetIcon(GetIndexOfCommandId(item_id), wallet_icon);
    }
  } else if (checked_item_ == kWalletAccountsStartId) {
    // A selected active Wallet account without account names means
    // that the sign-in attempt is in progress.
    AddCheckItem(kWalletAccountsStartId,
                 l10n_util::GetStringUTF16(IDS_AUTOFILL_DIALOG_GOOGLE_WALLET));
  }

  AddCheckItemWithStringId(kAutofillItemId,
                           IDS_AUTOFILL_DIALOG_PAY_WITHOUT_WALLET);
}

}  // namespace autofill
