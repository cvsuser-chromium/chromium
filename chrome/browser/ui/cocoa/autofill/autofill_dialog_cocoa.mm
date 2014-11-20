// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_cocoa.h"

#include "base/bind.h"
#include "base/mac/bundle_locations.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_nsobject.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/chrome_style.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_account_chooser.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_details_container.h"
#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_constants.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_input_field.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_main_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_overlay_controller.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_section_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_sign_in_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_textfield.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_sheet.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_custom_window.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/generated_resources.h"
#import "ui/base/cocoa/flipped_view.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/platform_font.h"

const CGFloat kAccountChooserHeight = 20.0;
const CGFloat kMinimumContentsHeight = 101;

// Height of all decorations & paddings on main dialog together.
const CGFloat kDecorationHeight = kAccountChooserHeight +
                                  autofill::kDetailVerticalPadding +
                                  chrome_style::kClientBottomPadding +
                                  chrome_style::kTitleTopPadding;

namespace autofill {

// static
AutofillDialogView* AutofillDialogView::Create(
    AutofillDialogViewDelegate* delegate) {
  return new AutofillDialogCocoa(delegate);
}

AutofillDialogCocoa::AutofillDialogCocoa(AutofillDialogViewDelegate* delegate)
    : close_weak_ptr_factory_(this),
      delegate_(delegate) {
}

AutofillDialogCocoa::~AutofillDialogCocoa() {
  // Cancel potential relayout requests, since the AutofillDialogController
  // is about to go away, but relayout requests assume it will still exist.
  [sheet_delegate_ cancelRelayout];
}

void AutofillDialogCocoa::Show() {
  // This should only be called once.
  DCHECK(!sheet_delegate_.get());
  sheet_delegate_.reset([[AutofillDialogWindowController alloc]
       initWithWebContents:delegate_->GetWebContents()
            autofillDialog:this]);
  base::scoped_nsobject<CustomConstrainedWindowSheet> sheet(
      [[CustomConstrainedWindowSheet alloc]
          initWithCustomWindow:[sheet_delegate_ window]]);
  constrained_window_.reset(
      new ConstrainedWindowMac(this, delegate_->GetWebContents(), sheet));
  [sheet_delegate_ show];
}

void AutofillDialogCocoa::Hide() {
  [sheet_delegate_ hide];
}

void AutofillDialogCocoa::PerformClose() {
  if (!close_weak_ptr_factory_.HasWeakPtrs()) {
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&AutofillDialogCocoa::CloseNow,
                   close_weak_ptr_factory_.GetWeakPtr()));
  }
}

void AutofillDialogCocoa::CloseNow() {
  constrained_window_->CloseWebContentsModalDialog();
}

void AutofillDialogCocoa::UpdatesStarted() {
}

void AutofillDialogCocoa::UpdatesFinished() {
}

void AutofillDialogCocoa::UpdateAccountChooser() {
  [sheet_delegate_ updateAccountChooser];
}

void AutofillDialogCocoa::UpdateButtonStrip() {
  [sheet_delegate_ updateButtonStrip];
}

void AutofillDialogCocoa::UpdateOverlay() {
  // TODO(estade): only update the overlay.
  UpdateButtonStrip();
}

void AutofillDialogCocoa::UpdateDetailArea() {
}

void AutofillDialogCocoa::UpdateForErrors() {
}

void AutofillDialogCocoa::UpdateNotificationArea() {
  [sheet_delegate_ updateNotificationArea];
}

void AutofillDialogCocoa::UpdateSection(DialogSection section) {
  [sheet_delegate_ updateSection:section];
}

void AutofillDialogCocoa::FillSection(DialogSection section,
                                      const DetailInput& originating_input) {
  [sheet_delegate_ fillSection:section forInput:originating_input];
}

void AutofillDialogCocoa::GetUserInput(DialogSection section,
                                       DetailOutputMap* output) {
  [sheet_delegate_ getInputs:output forSection:section];
}

string16 AutofillDialogCocoa::GetCvc() {
  return base::SysNSStringToUTF16([sheet_delegate_ getCvc]);
}

bool AutofillDialogCocoa::HitTestInput(const DetailInput& input,
                                       const gfx::Point& screen_point) {
  // TODO(dbeam): implement.
  return false;
}

bool AutofillDialogCocoa::SaveDetailsLocally() {
  return [sheet_delegate_ saveDetailsLocally];
}

const content::NavigationController* AutofillDialogCocoa::ShowSignIn() {
  return [sheet_delegate_ showSignIn];
}

void AutofillDialogCocoa::HideSignIn() {
  [sheet_delegate_ hideSignIn];
}

void AutofillDialogCocoa::ModelChanged() {
  [sheet_delegate_ modelChanged];
}

void AutofillDialogCocoa::UpdateErrorBubble() {
  [sheet_delegate_ updateErrorBubble];
}

TestableAutofillDialogView* AutofillDialogCocoa::GetTestableView() {
  return this;
}

void AutofillDialogCocoa::OnSignInResize(const gfx::Size& pref_size) {
  [sheet_delegate_ onSignInResize:
      NSMakeSize(pref_size.width(), pref_size.height())];
}

void AutofillDialogCocoa::SubmitForTesting() {
  [sheet_delegate_ accept:nil];
}

void AutofillDialogCocoa::CancelForTesting() {
  [sheet_delegate_ cancel:nil];
}

string16 AutofillDialogCocoa::GetTextContentsOfInput(const DetailInput& input) {
  for (size_t i = SECTION_MIN; i <= SECTION_MAX; ++i) {
    DialogSection section = static_cast<DialogSection>(i);
    DetailOutputMap contents;
    [sheet_delegate_ getInputs:&contents forSection:section];
    DetailOutputMap::const_iterator it = contents.find(&input);
    if (it != contents.end())
      return it->second;
  }

  NOTREACHED();
  return string16();
}

void AutofillDialogCocoa::SetTextContentsOfInput(const DetailInput& input,
                                                 const string16& contents) {
  [sheet_delegate_ setTextContents:base::SysUTF16ToNSString(contents)
                            forInput:input];
}

void AutofillDialogCocoa::SetTextContentsOfSuggestionInput(
    DialogSection section,
    const base::string16& text) {
  [sheet_delegate_ setTextContents:base::SysUTF16ToNSString(text)
              ofSuggestionForSection:section];
}

void AutofillDialogCocoa::ActivateInput(const DetailInput& input) {
  [sheet_delegate_ activateFieldForInput:input];
}

gfx::Size AutofillDialogCocoa::GetSize() const {
  return gfx::Size(NSSizeToCGSize([[sheet_delegate_ window] frame].size));
}

content::WebContents* AutofillDialogCocoa::GetSignInWebContents() {
  return [sheet_delegate_ getSignInWebContents];
}


bool AutofillDialogCocoa::IsShowingOverlay() const {
  return [sheet_delegate_ IsShowingOverlay];
}

void AutofillDialogCocoa::OnConstrainedWindowClosed(
    ConstrainedWindowMac* window) {
  constrained_window_.reset();
  // |this| belongs to |delegate_|, so no self-destruction here.
  delegate_->ViewClosed();
}

}  // autofill

#pragma mark "Loading" Shield

@interface AutofillOpaqueView : NSView
@end

@implementation AutofillOpaqueView

- (BOOL)isOpaque {
  return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  [[[self window] backgroundColor] setFill];
  [NSBezierPath fillRect:[self bounds]];
}

@end


#pragma mark Field Editor

@interface AutofillDialogFieldEditor : NSTextView
@end


@implementation AutofillDialogFieldEditor

- (void)mouseDown:(NSEvent*)event {
  // Delegate _must_ be notified before mouseDown is complete, since it needs
  // to distinguish between mouseDown for already focused fields, and fields
  // that will receive focus as part of the mouseDown.
  AutofillTextField* textfield =
      base::mac::ObjCCastStrict<AutofillTextField>([self delegate]);
  [textfield onEditorMouseDown:self];
  [super mouseDown:event];
}

@end


#pragma mark Window Controller

@interface AutofillDialogWindowController ()

// Compute maximum allowed height for the dialog.
- (CGFloat)maxHeight;

// Update size constraints on sign-in container.
- (void)updateSignInSizeConstraints;

// Notification that the WebContent's view frame has changed.
- (void)onContentViewFrameDidChange:(NSNotification*)notification;

@end


@implementation AutofillDialogWindowController (NSWindowDelegate)

- (id)windowWillReturnFieldEditor:(NSWindow*)window toObject:(id)client {
  AutofillTextField* textfield = base::mac::ObjCCast<AutofillTextField>(client);
  if (!textfield)
    return nil;

  if (!fieldEditor_) {
    fieldEditor_.reset([[AutofillDialogFieldEditor alloc] init]);
    [fieldEditor_ setFieldEditor:YES];
  }
  return fieldEditor_.get();
}

@end


@implementation AutofillDialogWindowController

- (id)initWithWebContents:(content::WebContents*)webContents
      autofillDialog:(autofill::AutofillDialogCocoa*)autofillDialog {
  DCHECK(webContents);

  base::scoped_nsobject<ConstrainedWindowCustomWindow> window(
      [[ConstrainedWindowCustomWindow alloc]
          initWithContentRect:ui::kWindowSizeDeterminedLater]);

  if ((self = [super initWithWindow:window])) {
    [window setDelegate:self];
    webContents_ = webContents;
    autofillDialog_ = autofillDialog;

    mainContainer_.reset([[AutofillMainContainer alloc]
                             initWithDelegate:autofillDialog->delegate()]);
    [mainContainer_ setTarget:self];

    signInContainer_.reset(
        [[AutofillSignInContainer alloc] initWithDialog:autofillDialog]);
    [[signInContainer_ view] setHidden:YES];

    NSRect clientRect = [[mainContainer_ view] frame];
    clientRect.origin = NSMakePoint(chrome_style::kClientBottomPadding,
                                    chrome_style::kHorizontalPadding);
    [[mainContainer_ view] setFrame:clientRect];
    [[signInContainer_ view] setFrame:clientRect];

    // Set dialog title.
    titleTextField_.reset([[NSTextField alloc] initWithFrame:NSZeroRect]);
    [titleTextField_ setEditable:NO];
    [titleTextField_ setBordered:NO];
    [titleTextField_ setDrawsBackground:NO];
    [titleTextField_ setFont:[NSFont systemFontOfSize:15.0]];
    [titleTextField_ setStringValue:
        base::SysUTF16ToNSString(autofillDialog->delegate()->DialogTitle())];
    [titleTextField_ sizeToFit];

    NSRect headerRect = clientRect;
    headerRect.size.height = kAccountChooserHeight;
    headerRect.origin.y = NSMaxY(clientRect);
    accountChooser_.reset([[AutofillAccountChooser alloc]
                              initWithFrame:headerRect
                                 delegate:autofillDialog->delegate()]);

    loadingShieldTextField_.reset(
        [[NSTextField alloc] initWithFrame:NSZeroRect]);
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    NSFont* loadingFont = rb.GetFont(
        ui::ResourceBundle::BaseFont).DeriveFont(15).GetNativeFont();
    [loadingShieldTextField_ setFont:loadingFont];
    [loadingShieldTextField_ setEditable:NO];
    [loadingShieldTextField_ setBordered:NO];
    [loadingShieldTextField_ setDrawsBackground:NO];

    base::scoped_nsobject<AutofillOpaqueView> loadingShieldView(
        [[AutofillOpaqueView alloc] initWithFrame:NSZeroRect]);
    [loadingShieldView setHidden:YES];
    [loadingShieldView addSubview:loadingShieldTextField_];

    overlayController_.reset(
        [[AutofillOverlayController alloc] initWithDelegate:
            autofillDialog->delegate()]);
    [[overlayController_ view] setHidden:YES];

    // This needs a flipped content view because otherwise the size
    // animation looks odd. However, replacing the contentView for constrained
    // windows does not work - it does custom rendering.
    base::scoped_nsobject<NSView> flippedContentView(
        [[FlippedView alloc] initWithFrame:
            [[[self window] contentView] frame]]);
    [flippedContentView setSubviews:
        @[accountChooser_,
          titleTextField_,
          [mainContainer_ view],
          [signInContainer_ view],
          loadingShieldView,
          [overlayController_ view]]];
    [flippedContentView setAutoresizingMask:
        (NSViewWidthSizable | NSViewHeightSizable)];
    [[[self window] contentView] addSubview:flippedContentView];
    [mainContainer_ setAnchorView:[[accountChooser_ subviews] objectAtIndex:1]];

    NSRect contentRect = clientRect;
    contentRect.origin = NSZeroPoint;
    contentRect.size.width += 2 * chrome_style::kHorizontalPadding;
    contentRect.size.height += NSHeight(headerRect) +
                               chrome_style::kClientBottomPadding +
                               chrome_style::kTitleTopPadding;
  }
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (CGFloat)maxHeight {
  NSRect dialogFrameRect = [[self window] frame];
  NSRect browserFrameRect =
      [webContents_->GetView()->GetTopLevelNativeWindow() frame];
  dialogFrameRect.size.height =
      NSMaxY(dialogFrameRect) - NSMinY(browserFrameRect);
  dialogFrameRect = [[self window] contentRectForFrameRect:dialogFrameRect];
  return NSHeight(dialogFrameRect);
}

- (void)updateSignInSizeConstraints {
  // Adjust for the size of all decorations and paddings outside main content.
  CGFloat minHeight = kMinimumContentsHeight - kDecorationHeight;
  CGFloat maxHeight = std::max([self maxHeight] - kDecorationHeight, minHeight);
  CGFloat width = NSWidth([[[self window] contentView] frame]);

  [signInContainer_ constrainSizeToMinimum:NSMakeSize(width, minHeight)
                                   maximum:NSMakeSize(width, maxHeight)];
}

- (void)onContentViewFrameDidChange:(NSNotification*)notification {
  [self updateSignInSizeConstraints];
  if ([[signInContainer_ view] isHidden])
    [self requestRelayout];
}

- (void)cancelRelayout {
  [NSObject cancelPreviousPerformRequestsWithTarget:self
                                           selector:@selector(performLayout)
                                             object:nil];
}

- (void)requestRelayout {
  [self cancelRelayout];
  [self performSelector:@selector(performLayout) withObject:nil afterDelay:0.0];
}

- (NSSize)preferredSize {
  NSSize contentSize;

  // Overall size is determined by either main container or sign in view.
  if ([[signInContainer_ view] isHidden])
    contentSize = [mainContainer_ preferredSize];
  else
    contentSize = [signInContainer_ preferredSize];

  // Always make room for the header.
  NSSize headerSize = NSMakeSize(contentSize.width, kAccountChooserHeight);
  NSSize size = contentSize;
  size.height += kDecorationHeight;

  // Show as much of the main view as is possible without going past the
  // bottom of the browser window.
  size.height = std::min(size.height, [self maxHeight]);

  if (![[overlayController_ view] isHidden]) {
    CGFloat height = [overlayController_ heightForWidth:size.width];
    // TODO(groby): This currently reserves size on top of the overlay image
    // equivalent to the height of the header. Clarify with UX what the final
    // padding will be.
    if (height != 0.0) {
      size.height =
          height + headerSize.height + autofill::kDetailVerticalPadding;
    }
  }

  return size;
}

- (void)performLayout {
  NSRect contentRect = NSZeroRect;
  contentRect.size = [self preferredSize];
  NSRect clientRect = contentRect;
  clientRect.origin.y = chrome_style::kClientBottomPadding;
  clientRect.size.height -= chrome_style::kTitleTopPadding +
                            chrome_style::kClientBottomPadding;

  [titleTextField_ setStringValue:
      base::SysUTF16ToNSString(autofillDialog_->delegate()->DialogTitle())];
  [titleTextField_ sizeToFit];

  NSRect headerRect, mainRect, titleRect, dummyRect;
  NSDivideRect(clientRect, &headerRect, &mainRect,
               kAccountChooserHeight, NSMinYEdge);
  NSDivideRect(mainRect, &dummyRect, &mainRect,
               autofill::kDetailVerticalPadding, NSMinYEdge);
  headerRect = NSInsetRect(
      headerRect, chrome_style::kHorizontalPadding, 0);
  NSDivideRect(headerRect, &titleRect, &headerRect,
               NSWidth([titleTextField_ frame]), NSMinXEdge);

  // Align baseline of title with bottom of accountChooser.
  base::scoped_nsobject<NSLayoutManager> layout_manager(
      [[NSLayoutManager alloc] init]);
  NSFont* titleFont = [titleTextField_ font];
  titleRect.origin.y += NSHeight(titleRect) -
      [layout_manager defaultBaselineOffsetForFont:titleFont];
  [titleTextField_ setFrame:titleRect];

  [accountChooser_ setFrame:headerRect];
  [accountChooser_ performLayout];
  if ([[signInContainer_ view] isHidden]) {
    [[mainContainer_ view] setFrame:mainRect];
    [mainContainer_ performLayout];
  } else {
    [[signInContainer_ view] setFrame:mainRect];
  }

  // Loading shield has text centered in the content rect.
  NSRect textFrame = [loadingShieldTextField_ frame];
  textFrame.origin.x =
      std::ceil((NSWidth(contentRect) - NSWidth(textFrame)) / 2.0);
  textFrame.origin.y =
    std::ceil((NSHeight(contentRect) - NSHeight(textFrame)) / 2.0);
  [loadingShieldTextField_ setFrame:textFrame];
  [[loadingShieldTextField_ superview] setFrame:contentRect];

  [[overlayController_ view] setFrame:contentRect];
  [overlayController_ performLayout];

  NSRect frameRect = [[self window] frameRectForContentRect:contentRect];
  [[self window] setFrame:frameRect display:YES];
  [[self window] recalculateKeyViewLoop];
}

- (IBAction)accept:(id)sender {
  if ([mainContainer_ validate])
    autofillDialog_->delegate()->OnAccept();
}

- (IBAction)cancel:(id)sender {
  autofillDialog_->delegate()->OnCancel();
  autofillDialog_->PerformClose();
}

- (void)show {
  // Resizing the browser causes the ConstrainedWindow to move.
  // Observe that to allow resizes based on browser size.
  // NOTE: This MUST come last after all initial setup is done, because there
  // is an immediate notification post registration.
  DCHECK([self window]);
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(onContentViewFrameDidChange:)
             name:NSWindowDidMoveNotification
           object:[self window]];

  [self updateAccountChooser];
  [self updateNotificationArea];
  [self requestRelayout];
}

- (void)hide {
  autofillDialog_->delegate()->OnCancel();
  autofillDialog_->PerformClose();
}

- (void)updateNotificationArea {
  [mainContainer_ updateNotificationArea];
}

- (void)updateAccountChooser {
  [accountChooser_ update];
  [mainContainer_ updateLegalDocuments];

  NSString* newLoadingMessage = @"";
  if (autofillDialog_->delegate()->ShouldShowSpinner())
    newLoadingMessage = l10n_util::GetNSStringWithFixup(IDS_TAB_LOADING_TITLE);
  if (![newLoadingMessage isEqualToString:
       [loadingShieldTextField_ stringValue]]) {
    NSView* loadingShieldView = [loadingShieldTextField_ superview];
    [loadingShieldTextField_ setStringValue:newLoadingMessage];
    [loadingShieldTextField_ sizeToFit];

    BOOL showShield = ([newLoadingMessage length] != 0);

    // For the duration of the loading shield, hide the main contents.
    // This prevents the currently focused text field "shining through".
    // No need to remember previous state, because the loading shield
    // always flows through to the main container.
    [[mainContainer_ view] setHidden:showShield];
    [loadingShieldView setHidden:!showShield];
    [self requestRelayout];
  }
}

- (void)updateButtonStrip {
  [overlayController_ updateState];
}

- (void)updateSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] update];
  [mainContainer_ updateSaveInChrome];
}

- (void)fillSection:(autofill::DialogSection)section
           forInput:(const autofill::DetailInput&)input {
  [[mainContainer_ sectionForId:section] fillForInput:input];
  [mainContainer_ updateSaveInChrome];
}

- (content::NavigationController*)showSignIn {
  [self updateSignInSizeConstraints];
  [signInContainer_ loadSignInPage];
  [[mainContainer_ view] setHidden:YES];
  [[signInContainer_ view] setHidden:NO];
  [self requestRelayout];

  return [signInContainer_ navigationController];
}

- (void)getInputs:(autofill::DetailOutputMap*)output
       forSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] getInputs:output];
}

- (NSString*)getCvc {
  autofill::DialogSection section = autofill::SECTION_CC;
  NSString* value = [[mainContainer_ sectionForId:section] suggestionText];
  if (!value) {
    section = autofill::SECTION_CC_BILLING;
    value = [[mainContainer_ sectionForId:section] suggestionText];
  }
  return value;
}

- (BOOL)saveDetailsLocally {
  return [mainContainer_ saveDetailsLocally];
}

- (void)hideSignIn {
  [[signInContainer_ view] setHidden:YES];
  [[mainContainer_ view] setHidden:NO];
  [self requestRelayout];
}

- (void)modelChanged {
  [mainContainer_ modelChanged];
}

- (void)updateErrorBubble {
  [mainContainer_ updateErrorBubble];
}

- (void)onSignInResize:(NSSize)size {
  [signInContainer_ setPreferredSize:size];
  [self requestRelayout];
}

@end


@implementation AutofillDialogWindowController (TestableAutofillDialogView)

- (void)setTextContents:(NSString*)text
               forInput:(const autofill::DetailInput&)input {
  for (size_t i = autofill::SECTION_MIN; i <= autofill::SECTION_MAX; ++i) {
    autofill::DialogSection section = static_cast<autofill::DialogSection>(i);
    // TODO(groby): Need to find the section for an input directly - wasteful.
    [[mainContainer_ sectionForId:section] setFieldValue:text forInput:input];
  }
}

- (void)setTextContents:(NSString*)text
 ofSuggestionForSection:(autofill::DialogSection)section {
  [[mainContainer_ sectionForId:section] setSuggestionFieldValue:text];
}

- (void)activateFieldForInput:(const autofill::DetailInput&)input {
  for (size_t i = autofill::SECTION_MIN; i <= autofill::SECTION_MAX; ++i) {
    autofill::DialogSection section = static_cast<autofill::DialogSection>(i);
    [[mainContainer_ sectionForId:section] activateFieldForInput:input];
  }
}

- (content::WebContents*)getSignInWebContents {
  return [signInContainer_ webContents];
}

- (BOOL)IsShowingOverlay {
  return ![[overlayController_ view] isHidden];
}

@end
