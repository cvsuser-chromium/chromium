// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_CONTROLLER_IMPL_H_
#define CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_CONTROLLER_IMPL_H_

#include <set>
#include <vector>

#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "chrome/browser/ui/autofill/account_chooser_model.h"
#include "chrome/browser/ui/autofill/autofill_dialog_controller.h"
#include "chrome/browser/ui/autofill/autofill_dialog_models.h"
#include "chrome/browser/ui/autofill/autofill_dialog_types.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller_impl.h"
#include "chrome/browser/ui/autofill/country_combobox_model.h"
#include "components/autofill/content/browser/wallet/wallet_client.h"
#include "components/autofill/content/browser/wallet/wallet_client_delegate.h"
#include "components/autofill/content/browser/wallet/wallet_items.h"
#include "components/autofill/content/browser/wallet/wallet_signin_helper_delegate.h"
#include "components/autofill/core/browser/autofill_manager_delegate.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/autofill_popup_delegate.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/ssl_status.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "url/gurl.h"

class Profile;

namespace content {
class WebContents;
}

namespace autofill {

class AutofillDataModel;
class AutofillDialogView;
class AutofillPopupControllerImpl;
class DataModelWrapper;
class TestableAutofillDialogView;

namespace risk {
class Fingerprint;
}

namespace wallet {
class WalletSigninHelper;
}

// This class drives the dialog that appears when a site uses the imperative
// autocomplete API to fill out a form.
class AutofillDialogControllerImpl : public AutofillDialogViewDelegate,
                                     public AutofillDialogController,
                                     public AutofillPopupDelegate,
                                     public content::NotificationObserver,
                                     public content::WebContentsObserver,
                                     public SuggestionsMenuModelDelegate,
                                     public wallet::WalletClientDelegate,
                                     public wallet::WalletSigninHelperDelegate,
                                     public PersonalDataManagerObserver,
                                     public AccountChooserModelDelegate,
                                     public gfx::AnimationDelegate {
 public:
  virtual ~AutofillDialogControllerImpl();

  static base::WeakPtr<AutofillDialogControllerImpl> Create(
      content::WebContents* contents,
      const FormData& form_structure,
      const GURL& source_url,
      const base::Callback<void(const FormStructure*)>& callback);

  // AutofillDialogController implementation.
  virtual void Show() OVERRIDE;
  virtual void Hide() OVERRIDE;
  virtual void TabActivated() OVERRIDE;

  // Returns |view_| as a testable version of itself (if |view_| exists and
  // actually implements |AutofillDialogView::GetTestableView()|).
  TestableAutofillDialogView* GetTestableView();

  // AutofillDialogViewDelegate implementation.
  virtual string16 DialogTitle() const OVERRIDE;
  virtual string16 AccountChooserText() const OVERRIDE;
  virtual string16 SignInLinkText() const OVERRIDE;
  virtual string16 SpinnerText() const OVERRIDE;
  virtual string16 EditSuggestionText() const OVERRIDE;
  virtual string16 CancelButtonText() const OVERRIDE;
  virtual string16 ConfirmButtonText() const OVERRIDE;
  virtual string16 SaveLocallyText() const OVERRIDE;
  virtual string16 SaveLocallyTooltip() const OVERRIDE;
  virtual string16 LegalDocumentsText() OVERRIDE;
  virtual bool ShouldDisableSignInLink() const OVERRIDE;
  virtual bool ShouldShowSpinner() const OVERRIDE;
  virtual bool ShouldShowSignInWebView() const OVERRIDE;
  virtual GURL SignInUrl() const OVERRIDE;
  virtual bool ShouldOfferToSaveInChrome() const OVERRIDE;
  virtual bool ShouldSaveInChrome() const OVERRIDE;
  virtual ui::MenuModel* MenuModelForAccountChooser() OVERRIDE;
  virtual gfx::Image AccountChooserImage() OVERRIDE;
  virtual gfx::Image ButtonStripImage() const OVERRIDE;
  virtual int GetDialogButtons() const OVERRIDE;
  virtual bool IsDialogButtonEnabled(ui::DialogButton button) const OVERRIDE;
  virtual DialogOverlayState GetDialogOverlay() OVERRIDE;
  virtual const std::vector<gfx::Range>& LegalDocumentLinks() OVERRIDE;
  virtual bool SectionIsActive(DialogSection section) const OVERRIDE;
  virtual const DetailInputs& RequestedFieldsForSection(DialogSection section)
      const OVERRIDE;
  virtual ui::ComboboxModel* ComboboxModelForAutofillType(
      ServerFieldType type) OVERRIDE;
  virtual ui::MenuModel* MenuModelForSection(DialogSection section) OVERRIDE;
  virtual string16 LabelForSection(DialogSection section) const OVERRIDE;
  virtual SuggestionState SuggestionStateForSection(
      DialogSection section) OVERRIDE;
  virtual FieldIconMap IconsForFields(const FieldValueMap& user_inputs)
      const OVERRIDE;
  virtual bool FieldControlsIcons(ServerFieldType type) const OVERRIDE;
  virtual string16 TooltipForField(ServerFieldType type) const OVERRIDE;
  virtual bool InputIsEditable(const DetailInput& input, DialogSection section)
      OVERRIDE;
  virtual string16 InputValidityMessage(DialogSection section,
                                        ServerFieldType type,
                                        const string16& value) OVERRIDE;
  virtual ValidityMessages InputsAreValid(
      DialogSection section, const DetailOutputMap& inputs) OVERRIDE;
  virtual void UserEditedOrActivatedInput(DialogSection section,
                                          const DetailInput* input,
                                          gfx::NativeView parent_view,
                                          const gfx::Rect& content_bounds,
                                          const string16& field_contents,
                                          bool was_edit) OVERRIDE;
  virtual bool HandleKeyPressEventInInput(
      const content::NativeWebKeyboardEvent& event) OVERRIDE;
  virtual void FocusMoved() OVERRIDE;
  virtual bool ShouldShowErrorBubble() const OVERRIDE;
  virtual void ViewClosed() OVERRIDE;
  virtual std::vector<DialogNotification> CurrentNotifications() OVERRIDE;
  virtual void LinkClicked(const GURL& url) OVERRIDE;
  virtual void SignInLinkClicked() OVERRIDE;
  virtual void NotificationCheckboxStateChanged(DialogNotification::Type type,
                                                bool checked) OVERRIDE;
  virtual void LegalDocumentLinkClicked(const gfx::Range& range) OVERRIDE;
  virtual bool OnCancel() OVERRIDE;
  virtual bool OnAccept() OVERRIDE;
  virtual Profile* profile() OVERRIDE;
  virtual content::WebContents* GetWebContents() OVERRIDE;

  // AutofillPopupDelegate implementation.
  virtual void OnPopupShown() OVERRIDE;
  virtual void OnPopupHidden() OVERRIDE;
  virtual bool ShouldRepostEvent(const ui::MouseEvent& event) OVERRIDE;
  virtual void DidSelectSuggestion(int identifier) OVERRIDE;
  virtual void DidAcceptSuggestion(const string16& value,
                                   int identifier) OVERRIDE;
  virtual void RemoveSuggestion(const string16& value,
                                int identifier) OVERRIDE;
  virtual void ClearPreviewedForm() OVERRIDE;

  // content::NotificationObserver implementation.
  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;

  // SuggestionsMenuModelDelegate implementation.
  virtual void SuggestionsMenuWillShow() OVERRIDE;
  virtual void SuggestionItemSelected(SuggestionsMenuModel* model,
                                      size_t index) OVERRIDE;

  // wallet::WalletClientDelegate implementation.
  virtual const AutofillMetrics& GetMetricLogger() const OVERRIDE;
  virtual std::string GetRiskData() const OVERRIDE;
  virtual std::string GetWalletCookieValue() const OVERRIDE;
  virtual bool IsShippingAddressRequired() const OVERRIDE;
  virtual void OnDidAcceptLegalDocuments() OVERRIDE;
  virtual void OnDidAuthenticateInstrument(bool success) OVERRIDE;
  virtual void OnDidGetFullWallet(
      scoped_ptr<wallet::FullWallet> full_wallet) OVERRIDE;
  virtual void OnDidGetWalletItems(
      scoped_ptr<wallet::WalletItems> wallet_items) OVERRIDE;
  virtual void OnDidSaveToWallet(
      const std::string& instrument_id,
      const std::string& address_id,
      const std::vector<wallet::RequiredAction>& required_actions,
      const std::vector<wallet::FormFieldError>& form_field_errors) OVERRIDE;
  virtual void OnWalletError(
      wallet::WalletClient::ErrorType error_type) OVERRIDE;

  // PersonalDataManagerObserver implementation.
  virtual void OnPersonalDataChanged() OVERRIDE;

  // AccountChooserModelDelegate implementation.
  virtual void AccountChooserWillShow() OVERRIDE;
  virtual void AccountChoiceChanged() OVERRIDE;
  virtual void UpdateAccountChooserView() OVERRIDE;

  // wallet::WalletSigninHelperDelegate implementation.
  virtual void OnPassiveSigninSuccess(const std::vector<std::string>& username)
      OVERRIDE;
  virtual void OnPassiveSigninFailure(
      const GoogleServiceAuthError& error) OVERRIDE;
  virtual void OnUserNameFetchSuccess(const std::vector<std::string>& username)
      OVERRIDE;
  virtual void OnUserNameFetchFailure(
      const GoogleServiceAuthError& error) OVERRIDE;
  virtual void OnDidFetchWalletCookieValue(
      const std::string& cookie_value) OVERRIDE;

  // gfx::AnimationDelegate implementation.
  virtual void AnimationEnded(const gfx::Animation* animation) OVERRIDE;
  virtual void AnimationProgressed(const gfx::Animation* animation) OVERRIDE;

 protected:
  enum DialogSignedInState {
    NOT_CHECKED,
    REQUIRES_RESPONSE,
    REQUIRES_SIGN_IN,
    REQUIRES_PASSIVE_SIGN_IN,
    SIGNED_IN,
    SIGN_IN_DISABLED,
  };

  // Exposed for testing.
  AutofillDialogControllerImpl(
      content::WebContents* contents,
      const FormData& form_structure,
      const GURL& source_url,
      const base::Callback<void(const FormStructure*)>& callback);

  // Exposed for testing.
  AutofillDialogView* view() { return view_.get(); }
  virtual AutofillDialogView* CreateView();
  const DetailInput* input_showing_popup() const {
    return input_showing_popup_;
  }

  // Returns the PersonalDataManager for |profile_|.
  virtual PersonalDataManager* GetManager();

  // Returns the WalletClient* this class uses to talk to Online Wallet. Exposed
  // for testing.
  virtual wallet::WalletClient* GetWalletClient();

  // Call to disable communication to Online Wallet for this dialog.
  // Exposed for testing.
  void DisableWallet(wallet::WalletClient::ErrorType error_type);

  // Returns whether Wallet is the current data source. Exposed for testing.
  virtual bool IsPayingWithWallet() const;

  // Asks risk module to asynchronously load fingerprint data. Data will be
  // returned via |OnDidLoadRiskFingerprintData()|. Exposed for testing.
  virtual void LoadRiskFingerprintData();
  virtual void OnDidLoadRiskFingerprintData(
      scoped_ptr<risk::Fingerprint> fingerprint);

  // Opens the given URL in a new foreground tab.
  virtual void OpenTabWithUrl(const GURL& url);

  // Whether |section| was sent into edit mode based on existing data. This
  // happens when a user clicks "Edit" or a suggestion is invalid.
  virtual bool IsEditingExistingData(DialogSection section) const;

  // Whether the user has chosen to enter all new data in |section|. This
  // happens via choosing "Add a new X..." from a section's suggestion menu.
  bool IsManuallyEditingSection(DialogSection section) const;

  // Should be called on the Wallet sign-in error.
  virtual void OnWalletSigninError();

  // Whether the information input in this dialog will be securely transmitted
  // to the requesting site.
  virtual bool TransmissionWillBeSecure() const;

  // Whether submission is currently waiting for |action| to be handled.
  bool IsSubmitPausedOn(wallet::RequiredAction action) const;

  // Shows a new credit card saved bubble and passes ownership of |new_card| and
  // |billing_profile| to the bubble. Exposed for testing.
  virtual void ShowNewCreditCardBubble(
      scoped_ptr<CreditCard> new_card,
      scoped_ptr<AutofillProfile> billing_profile);

  // Called when there's nothing left to accept, update, save, or authenticate
  // in order to fill |form_structure_| and pass data back to the invoking page.
  void DoFinishSubmit();

  // Delays enabling submit button for a short period of time. Exposed for
  // testing.
  virtual void SubmitButtonDelayBegin();

  // Ends the delay for enabling the submit button. Called only from tests.
  // Without this method, the tests would have to wait for the delay timer to
  // finish, which would be flaky.
  void SubmitButtonDelayEndForTesting();

  // Resets |last_wallet_items_fetch_timestamp_| for testing.
  void ClearLastWalletItemsFetchTimestampForTesting();

  // Allows tests to inspect the state of the account chooser.
  const AccountChooserModel& AccountChooserModelForTesting() const;

  // Returns whether |url| matches the sign in continue URL.
  virtual bool IsSignInContinueUrl(const GURL& url) const;

  // Whether the user is known to be signed in.
  DialogSignedInState SignedInState() const;

 private:
  // Whether or not the current request wants credit info back.
  bool RequestingCreditCardInfo() const;

  // Initializes or updates |suggested_cc_| et al.
  void SuggestionsUpdated();

  // Starts fetching the wallet items from Online Wallet.
  void GetWalletItems();

  // Stop showing sign in flow.
  void HideSignIn();

  // Handles the SignedInState() on Wallet or sign-in state update.
  // Triggers the user name fetch and passive sign-in.
  void SignedInStateUpdated();

  // Refreshes the model on Wallet or sign-in state update.
  void OnWalletOrSigninUpdate();

  // Called when a Save or Update call to Wallet has validation errors.
  void OnWalletFormFieldError(
      const std::vector<wallet::FormFieldError>& form_field_errors);

  // Calculates |legal_documents_text_| and |legal_document_link_ranges_|.
  void ConstructLegalDocumentsText();

  // Clears previously entered manual input and removes |section| from
  // |section_editing_state_|. Does not update the view.
  void ResetSectionInput(DialogSection section);

  // Force |section| into edit mode if the current suggestion is invalid.
  void ShowEditUiIfBadSuggestion(DialogSection section);

  // Whether the |value| of |input| should be preserved on account change.
  bool InputWasEdited(ServerFieldType type,
                      const base::string16& value);

  // Takes a snapshot of the newly inputted user data in |view_| (if it exists).
  DetailOutputMap TakeUserInputSnapshot();

  // Fills the detail inputs from a previously taken user input snapshot. Does
  // not update the view.
  void RestoreUserInputFromSnapshot(const DetailOutputMap& snapshot);

  // Tells the view to update |section|.
  void UpdateSection(DialogSection section);

  // Tells |view_| to update the validity status of its detail inputs (if
  // |view_| is non-null). Currently this is used solely for highlighting
  // invalid suggestions, so if no sections are based on existing data,
  // |view_->UpdateForErrors()| is not called.
  void UpdateForErrors();

  // Renders and returns one frame of the generated card animation.
  gfx::Image GetGeneratedCardImage(const base::string16& card_number,
                                   const base::string16& name,
                                   const SkColor& gradient_top,
                                   const SkColor& gradient_bottom);

  // Kicks off |card_scrambling_refresher_|.
  void StartCardScramblingRefresher();

  // Changes |scrambled_card_number_| and pushes an update to the view.
  void RefreshCardScramblingOverlay();

  // Tells the view to update the overlay.
  void PushOverlayUpdate();

  // Creates a DataModelWrapper item for the item that's checked in the
  // suggestion model for |section|. This may represent Autofill
  // data or Wallet data, depending on whether Wallet is currently enabled.
  scoped_ptr<DataModelWrapper> CreateWrapper(DialogSection section);

  // Helper to return the current Wallet instrument or address. If the dialog
  // isn't using Wallet or the user is adding a new instrument or address, NULL
  // will be returned.
  const wallet::WalletItems::MaskedInstrument* ActiveInstrument() const;
  const wallet::Address* ActiveShippingAddress() const;

  // Fills in |section|-related fields in |output_| according to the state of
  // |view_|.
  void FillOutputForSection(DialogSection section);
  // As above, but uses |compare| to determine whether a DetailInput matches
  // a field. Saves any new Autofill data to the PersonalDataManager.
  void FillOutputForSectionWithComparator(DialogSection section,
                                          const InputFieldComparator& compare);

  // Returns whether |form_structure|_| has any fields that match the fieldset
  // represented by |section|.
  bool FormStructureCaresAboutSection(DialogSection section) const;

  // Finds all fields of the given |type| in |form_structure_|, if any, and sets
  // each field's value to |output|.
  void SetOutputForFieldsOfType(ServerFieldType type, const string16& output);

  // Gets the value for |type| in |section|, whether it comes from manual user
  // input or the active suggestion.
  string16 GetValueFromSection(DialogSection section,
                               ServerFieldType type);

  // Gets the SuggestionsMenuModel for |section|.
  SuggestionsMenuModel* SuggestionsMenuModelForSection(DialogSection section);
  const SuggestionsMenuModel* SuggestionsMenuModelForSection(
      DialogSection section) const;
  // And the reverse.
  DialogSection SectionForSuggestionsMenuModel(
      const SuggestionsMenuModel& model);

  // Suggested text and icons for sections. Suggestion text is used to show an
  // abridged overview of the currently used suggestion. Extra text is used when
  // part of a section is suggested but part must be manually input (e.g. during
  // a CVC challenge or when using Autofill's CC section [never stores CVC]).
  bool SuggestionTextForSection(DialogSection section,
                                base::string16* vertically_compact,
                                base::string16* horizontally_compact);
  string16 RequiredActionTextForSection(DialogSection section) const;
  gfx::Image SuggestionIconForSection(DialogSection section);
  string16 ExtraSuggestionTextForSection(DialogSection section) const;
  gfx::Image ExtraSuggestionIconForSection(DialogSection section);

  // Loads profiles that can suggest data for |type|. |field_contents| is the
  // part the user has already typed. |inputs| is the rest of section.
  // Identifying info is loaded into the last three outparams as well as
  // |popup_guids_|.
  void GetProfileSuggestions(
      ServerFieldType type,
      const string16& field_contents,
      const DetailInputs& inputs,
      std::vector<string16>* popup_values,
      std::vector<string16>* popup_labels,
      std::vector<string16>* popup_icons);

  // Like RequestedFieldsForSection, but returns a pointer.
  DetailInputs* MutableRequestedFieldsForSection(DialogSection section);

  // Hides |popup_controller_|'s popup view, if it exists.
  void HidePopup();

  // Set whether the currently editing |section| was originally based on
  // existing Wallet or Autofill data.
  void SetEditingExistingData(DialogSection section, bool editing);

  // Whether the user has chosen to enter all new data in at least one section.
  bool IsManuallyEditingAnySection() const;

  // Returns validity message for a given credit card number.
  base::string16 CreditCardNumberValidityMessage(
      const base::string16& number) const;

  // Whether all of the input fields currently showing in the dialog have valid
  // contents. This validates only by checking "sure" messages, i.e. messages
  // that would have been displayed to the user during editing, as opposed to
  // submission.
  bool AllSectionsAreValid();

  // Whether all of the input fields currently showing in the given |section| of
  // the dialog have valid contents. This validates only by checking "sure"
  // messages - see AllSectionsAreValid.
  bool SectionIsValid(DialogSection section);

  // Whether the currently active credit card expiration date is valid.
  bool IsCreditCardExpirationValid(const base::string16& year,
                                   const base::string16& month) const;

  // Returns true if |key| refers to a suggestion, as opposed to some control
  // menu item.
  bool IsASuggestionItemKey(const std::string& key) const;

  // Whether the billing section should be used to fill in the shipping details.
  bool ShouldUseBillingForShipping();

  // Whether the user wishes to save information locally to Autofill.
  bool ShouldSaveDetailsLocally();

  // Change whether the controller is currently submitting details to Autofill
  // or Online Wallet (|is_submitting_|) and update the view.
  void SetIsSubmitting(bool submitting);

  // Whether the user has accepted all the current legal documents' terms.
  bool AreLegalDocumentsCurrent() const;

  // Accepts any pending legal documents now that the user has pressed Submit.
  void AcceptLegalTerms();

  // Start the submit proccess to interact with Online Wallet (might do various
  // things like accept documents, save details, update details, respond to
  // required actions, etc.).
  void SubmitWithWallet();

  // Creates an instrument based on |views_|' contents.
  scoped_ptr<wallet::Instrument> CreateTransientInstrument();

  // Creates an address based on the contents of |view_|.
  scoped_ptr<wallet::Address> CreateTransientAddress();

  // Gets a full wallet from Online Wallet so the user can purchase something.
  // This information is decoded to reveal a fronting (proxy) card.
  void GetFullWallet();

  // Updates the state of the controller and |view_| based on any required
  // actions returned by Save or Update calls to Wallet.
  void HandleSaveOrUpdateRequiredActions(
      const std::vector<wallet::RequiredAction>& required_actions);

  // Shows a card generation overlay if necessary, then calls DoFinishSubmit.
  void FinishSubmit();

  // Writes to prefs the choice of AutofillDataModel for |section|.
  void PersistAutofillChoice(DialogSection section,
                             const std::string& guid);

  // Sets the outparams to the default AutofillDataModel for |section| (which is
  // the first one in the menu that is a suggestion item).
  void GetDefaultAutofillChoice(DialogSection section,
                                std::string* guid);

  // Reads from prefs the choice of AutofillDataModel for |section|. Returns
  // whether there was a setting to read.
  bool GetAutofillChoice(DialogSection section,
                         std::string* guid);

  // Logs metrics when the dialog is submitted.
  void LogOnFinishSubmitMetrics();

  // Logs metrics when the dialog is canceled.
  void LogOnCancelMetrics();

  // Logs metrics when the edit ui is shown for the given |section|.
  void LogEditUiShownMetric(DialogSection section);

  // Logs metrics when a suggestion item from the given |model| is selected.
  void LogSuggestionItemSelectedMetric(const SuggestionsMenuModel& model);

  // Logs the time elapsed from when the dialog was shown to when the user could
  // interact with it.
  void LogDialogLatencyToShow();

  // Returns the metric corresponding to the user's initial state when
  // interacting with this dialog.
  AutofillMetrics::DialogInitialUserStateMetric GetInitialUserState() const;

  // Shows an educational bubble if a new credit card was saved or the first few
  // times an Online Wallet fronting card was generated.
  void MaybeShowCreditCardBubble();

  // Called when the delay for enabling the submit button ends.
  void OnSubmitButtonDelayEnd();

  // Initiates a fetch of the user's current Wallet cookie and Google username.
  void FetchWalletCookieAndUserName();

  // The |profile| for |contents_|.
  Profile* const profile_;

  // For logging UMA metrics.
  const AutofillMetrics metric_logger_;
  base::Time dialog_shown_timestamp_;
  AutofillMetrics::DialogInitialUserStateMetric initial_user_state_;

  FormStructure form_structure_;

  // Whether the URL visible to the user when this dialog was requested to be
  // invoked is the same as |source_url_|.
  bool invoked_from_same_origin_;

  // The URL of the invoking site.
  GURL source_url_;

  // The callback via which we return the collected data.
  base::Callback<void(const FormStructure*)> callback_;

  // The AccountChooserModel acts as the MenuModel for the account chooser,
  // and also tracks which data source the dialog is using.
  AccountChooserModel account_chooser_model_;

  // The sign-in helper to fetch the user's Wallet cookie and to perform passive
  // sign-in. The helper is set only during fetch/sign-in, and NULL otherwise.
  scoped_ptr<wallet::WalletSigninHelper> signin_helper_;

  // The sign-in helper to fetch the user's human-readable username. The helper
  // is set only while fetching the username, and NULL otherwise.
  scoped_ptr<wallet::WalletSigninHelper> username_fetcher_;

  // A client to talk to the Online Wallet API.
  wallet::WalletClient wallet_client_;

  // True if |this| has ever called GetWalletItems().
  bool wallet_items_requested_;

  // True when the user has clicked the "Use Wallet" link and we're waiting to
  // figure out whether we need to ask them to actively sign in.
  bool handling_use_wallet_link_click_;

  // Recently received items retrieved via |wallet_client_|.
  scoped_ptr<wallet::WalletItems> wallet_items_;
  scoped_ptr<wallet::FullWallet> full_wallet_;

  // The default active instrument and shipping address object IDs as of the
  // last time Wallet items were fetched. These variables are only set
  // (i.e. non-empty) when the Wallet items are being re-fetched.
  std::string previous_default_instrument_id_;
  std::string previous_default_shipping_address_id_;
  // The last active instrument and shipping address object IDs. These
  // variables are only set (i.e. non-empty) when the Wallet items are being
  // re-fetched.
  std::string previously_selected_instrument_id_;
  std::string previously_selected_shipping_address_id_;

  // When the Wallet items were last fetched.
  base::TimeTicks last_wallet_items_fetch_timestamp_;

  // Local machine signals to pass along on each request to trigger (or
  // discourage) risk challenges; sent if the user is up to date on legal docs.
  std::string risk_data_;

  // The text to display when the user is accepting new terms of service, etc.
  string16 legal_documents_text_;
  // The ranges within |legal_documents_text_| to linkify.
  std::vector<gfx::Range> legal_document_link_ranges_;

  // The instrument and address IDs from the Online Wallet server to be used
  // when getting a full wallet.
  std::string active_instrument_id_;
  std::string active_address_id_;

  // The fields for billing and shipping which the page has actually requested.
  DetailInputs requested_cc_fields_;
  DetailInputs requested_billing_fields_;
  DetailInputs requested_cc_billing_fields_;
  DetailInputs requested_shipping_fields_;

  // Models for the credit card expiration inputs.
  MonthComboboxModel cc_exp_month_combobox_model_;
  YearComboboxModel cc_exp_year_combobox_model_;

  // Model for the country input.
  CountryComboboxModel country_combobox_model_;

  // Models for the suggestion views.
  SuggestionsMenuModel suggested_cc_;
  SuggestionsMenuModel suggested_billing_;
  SuggestionsMenuModel suggested_cc_billing_;
  SuggestionsMenuModel suggested_shipping_;

  // |DialogSection|s that are in edit mode that are based on existing data.
  std::set<DialogSection> section_editing_state_;

  // Whether |form_structure_| has asked for any details that would indicate
  // we should show a shipping section.
  bool cares_about_shipping_;

  // The GUIDs for the currently showing unverified profiles popup.
  std::vector<PersonalDataManager::GUIDPair> popup_guids_;

  // The controller for the currently showing popup (which helps users when
  // they're manually filling the dialog).
  base::WeakPtr<AutofillPopupControllerImpl> popup_controller_;

  // The input for which |popup_controller_| is currently showing a popup
  // (if any).
  const DetailInput* input_showing_popup_;

  scoped_ptr<AutofillDialogView> view_;

  // A NotificationRegistrar for tracking the completion of sign-in.
  content::NotificationRegistrar signin_registrar_;

  base::WeakPtrFactory<AutofillDialogControllerImpl> weak_ptr_factory_;

  // Set to true when the user presses the sign in link, until we're ready to
  // show the normal dialog again. This is used to hide the buttons while
  // the spinner is showing after an explicit sign in.
  bool waiting_for_explicit_sign_in_response_;

  // Whether a user accepted legal documents while this dialog is running.
  bool has_accepted_legal_documents_;

  // True after the user first accepts the dialog and presses "Submit". May
  // continue to be true while processing required actions.
  bool is_submitting_;

  // True if the last call to |GetFullWallet()| returned a
  // CHOOSE_ANOTHER_INSTRUMENT_OR_ADDRESS required action, indicating that the
  // selected instrument or address had become invalid since it was originally
  // returned in |GetWalletItems()|.
  bool choose_another_instrument_or_address_;

  // Whether or not the server side validation errors returned by Wallet were
  // recoverable.
  bool wallet_server_validation_recoverable_;

  // Whether |callback_| was Run() with a filled |form_structure_|.
  bool data_was_passed_back_;

  typedef std::map<ServerFieldType,
      std::pair<base::string16, base::string16> > TypeErrorInputMap;
  typedef std::map<DialogSection, TypeErrorInputMap> WalletValidationErrors;
  // Wallet validation errors. section->type->(error_msg, input_value).
  WalletValidationErrors wallet_errors_;

  // The notification that describes the current wallet error, if any.
  scoped_ptr<DialogNotification> wallet_error_notification_;

  // Whether the latency to display to the UI was logged to UMA yet.
  bool was_ui_latency_logged_;

  // The Google Wallet cookie value, set as an authorization header on requests
  // to Wallet.
  std::string wallet_cookie_value_;

  // A map from dialog sections to the GUID of a newly saved Autofill data
  // models for that section. No entries present that don't have newly saved
  // data models.
  std::map<DialogSection, std::string> newly_saved_data_model_guids_;

  // Populated if the user chose to save a newly inputted credit card. Used to
  // show a bubble as the dialog closes to confirm a user's new card info was
  // saved. Never populated while incognito (as nothing's actually saved).
  scoped_ptr<CreditCard> newly_saved_card_;

  // The last four digits of the backing card used for the current run of the
  // dialog. Only applies to Wallet and is populated on submit.
  base::string16 backing_card_last_four_;

  // The timer that delays enabling submit button for a short period of time on
  // startup.
  base::OneShotTimer<AutofillDialogControllerImpl> submit_button_delay_timer_;

  // The card scrambling animation displays a random number in place of an
  // actual credit card number. This is that random number.
  base::string16 scrambled_card_number_;

  // Two timers to deal with the card scrambling animation. The first provides
  // a one second delay before the numbers start scrambling. The second controls
  // the rate of refresh for the number scrambling.
  base::OneShotTimer<AutofillDialogControllerImpl> card_scrambling_delay_;
  base::RepeatingTimer<AutofillDialogControllerImpl> card_scrambling_refresher_;

  // An animation which controls the background fade when the card is done
  // scrambling.
  gfx::LinearAnimation card_generated_animation_;

  // A username string we display in the card scrambling/generated overlay.
  base::string16 submitted_cardholder_name_;

  DISALLOW_COPY_AND_ASSIGN(AutofillDialogControllerImpl);
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_AUTOFILL_DIALOG_CONTROLLER_IMPL_H_
