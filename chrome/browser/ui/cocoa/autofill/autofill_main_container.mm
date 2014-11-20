// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_main_container.h"

#include <algorithm>
#include <cmath>

#include "base/mac/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_dialog_view_delegate.h"
#include "chrome/browser/ui/chrome_style.h"
#include "chrome/browser/ui/cocoa/autofill/autofill_dialog_constants.h"
#import "chrome/browser/ui/cocoa/constrained_window/constrained_window_button.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_details_container.h"
#import "chrome/browser/ui/cocoa/autofill/autofill_notification_container.h"
#import "chrome/browser/ui/cocoa/hyperlink_text_view.h"
#import "chrome/browser/ui/cocoa/key_equivalent_constants.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#import "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/range/range.h"

@interface AutofillMainContainer (Private)
- (void)buildWindowButtons;
- (void)layoutButtons;
- (NSSize)preferredLegalDocumentSizeForWidth:(CGFloat)width;
@end


@implementation AutofillMainContainer

@synthesize target = target_;

- (id)initWithDelegate:(autofill::AutofillDialogViewDelegate*)delegate {
  if (self = [super init]) {
    delegate_ = delegate;
  }
  return self;
}

- (void)loadView {
  [self buildWindowButtons];

  base::scoped_nsobject<NSView> view([[NSView alloc] initWithFrame:NSZeroRect]);
  [view setAutoresizesSubviews:YES];
  [view setSubviews:@[buttonContainer_]];
  [self setView:view];

  [self layoutButtons];

  // Set up Wallet icon.
  buttonStripImage_.reset([[NSImageView alloc] initWithFrame:NSZeroRect]);
  [self updateWalletIcon];
  [[self view] addSubview:buttonStripImage_];

  // Set up "Save in Chrome" checkbox.
  saveInChromeCheckbox_.reset([[NSButton alloc] initWithFrame:NSZeroRect]);
  [saveInChromeCheckbox_ setButtonType:NSSwitchButton];
  [saveInChromeCheckbox_ setTitle:
      base::SysUTF16ToNSString(delegate_->SaveLocallyText())];
  [saveInChromeCheckbox_ sizeToFit];
  [[self view] addSubview:saveInChromeCheckbox_];

  saveInChromeTooltip_.reset([[NSImageView alloc] initWithFrame:NSZeroRect]);
  [saveInChromeTooltip_ setImage:
      ui::ResourceBundle::GetSharedInstance().GetNativeImageNamed(
          IDR_AUTOFILL_TOOLTIP_ICON).ToNSImage()];
  [saveInChromeTooltip_ setToolTip:
      base::SysUTF16ToNSString(delegate_->SaveLocallyTooltip())];
  [saveInChromeTooltip_ setFrameSize:[[saveInChromeTooltip_ image] size]];
  [[self view] addSubview:saveInChromeTooltip_];
  [self updateSaveInChrome];

  detailsContainer_.reset(
      [[AutofillDetailsContainer alloc] initWithDelegate:delegate_]);
  NSSize frameSize = [[detailsContainer_ view] frame].size;
  [[detailsContainer_ view] setFrameOrigin:
      NSMakePoint(0, NSHeight([buttonContainer_ frame]))];
  frameSize.height += NSHeight([buttonContainer_ frame]);
  [[self view] setFrameSize:frameSize];
  [[self view] addSubview:[detailsContainer_ view]];

  legalDocumentsView_.reset(
      [[HyperlinkTextView alloc] initWithFrame:NSZeroRect]);
  [legalDocumentsView_ setEditable:NO];
  [legalDocumentsView_ setBackgroundColor:
      [NSColor colorWithCalibratedRed:0.96
                                green:0.96
                                 blue:0.96
                                alpha:1.0]];
  [legalDocumentsView_ setDrawsBackground:YES];
  [legalDocumentsView_ setHidden:YES];
  [legalDocumentsView_ setDelegate:self];
  legalDocumentsSizeDirty_ = YES;
  [[self view] addSubview:legalDocumentsView_];

  notificationContainer_.reset(
      [[AutofillNotificationContainer alloc] initWithDelegate:delegate_]);
  [[self view] addSubview:[notificationContainer_ view]];
}

// Called when embedded links are clicked.
- (BOOL)textView:(NSTextView*)textView
    clickedOnLink:(id)link
          atIndex:(NSUInteger)charIndex {
  int index = [base::mac::ObjCCastStrict<NSNumber>(link) intValue];
  delegate_->LegalDocumentLinkClicked(
      delegate_->LegalDocumentLinks()[index]);
  return YES;
}

- (NSSize)preferredSize {
  // Overall width is determined by |detailsContainer_|.
  NSSize buttonSize = [buttonContainer_ frame].size;
  NSSize buttonStripImageSize = [buttonStripImage_ frame].size;
  NSSize buttonStripSize =
      NSMakeSize(buttonSize.width + chrome_style::kHorizontalPadding +
                     buttonStripImageSize.width,
                 std::max(buttonSize.height, buttonStripImageSize.height));

  NSSize detailsSize = [detailsContainer_ preferredSize];

  NSSize size = NSMakeSize(std::max(buttonStripSize.width, detailsSize.width),
                           buttonStripSize.height + detailsSize.height);
  size.height += 2 * autofill::kDetailVerticalPadding;

  if (![legalDocumentsView_ isHidden]) {
    NSSize legalDocumentSize =
        [self preferredLegalDocumentSizeForWidth:detailsSize.width];
    size.height += legalDocumentSize.height + autofill::kVerticalSpacing;
  }

  NSSize notificationSize =
      [notificationContainer_ preferredSizeForWidth:detailsSize.width];
  size.height += notificationSize.height;
  return size;
}

- (void)performLayout {
  NSRect bounds = [[self view] bounds];

  CGFloat currentY = 0.0;
  if (![legalDocumentsView_ isHidden]) {
    [legalDocumentsView_ setFrameSize:
        [self preferredLegalDocumentSizeForWidth:NSWidth(bounds)]];
    currentY = NSMaxY([legalDocumentsView_ frame]) + autofill::kVerticalSpacing;
  }

  NSRect buttonFrame = [buttonContainer_ frame];
  buttonFrame.origin.y = currentY;
  [buttonContainer_ setFrameOrigin:buttonFrame.origin];
  currentY = NSMaxY(buttonFrame) + autofill::kDetailVerticalPadding;

  NSPoint walletIconOrigin =
      NSMakePoint(chrome_style::kHorizontalPadding, buttonFrame.origin.y);
  [buttonStripImage_ setFrameOrigin:walletIconOrigin];
  currentY = std::max(currentY, NSMaxY([buttonStripImage_ frame]));

  NSRect checkboxFrame = [saveInChromeCheckbox_ frame];
  [saveInChromeCheckbox_ setFrameOrigin:
      NSMakePoint(chrome_style::kHorizontalPadding,
                  NSMidY(buttonFrame) - NSHeight(checkboxFrame) / 2.0)];

  NSRect tooltipFrame = [saveInChromeTooltip_ frame];
  [saveInChromeTooltip_ setFrameOrigin:
      NSMakePoint(NSMaxX([saveInChromeCheckbox_ frame]) + autofill::kButtonGap,
                  NSMidY(buttonFrame) - (NSHeight(tooltipFrame) / 2.0))];

  NSRect notificationFrame = NSZeroRect;
  notificationFrame.size = [notificationContainer_ preferredSizeForWidth:
      NSWidth(bounds)];

  // Buttons/checkbox/legal take up lower part of view, notifications the
  // upper part. Adjust the detailsContainer to take up the remainder.
  CGFloat remainingHeight =
      NSHeight(bounds) - currentY - NSHeight(notificationFrame) -
      autofill::kDetailVerticalPadding;
  NSRect containerFrame =
      NSMakeRect(0, currentY, NSWidth(bounds), remainingHeight);
  [[detailsContainer_ view] setFrame:containerFrame];
  [detailsContainer_ performLayout];

  notificationFrame.origin =
      NSMakePoint(0, NSMaxY(containerFrame) + autofill::kDetailVerticalPadding);
  [[notificationContainer_ view] setFrame:notificationFrame];
  [notificationContainer_ performLayout];
}

- (void)buildWindowButtons {
  if (buttonContainer_.get())
    return;

  buttonContainer_.reset([[GTMWidthBasedTweaker alloc] initWithFrame:
      ui::kWindowSizeDeterminedLater]);
  [buttonContainer_
      setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];

  base::scoped_nsobject<NSButton> button(
      [[ConstrainedWindowButton alloc] initWithFrame:NSZeroRect]);
  [button setTitle:l10n_util::GetNSStringWithFixup(IDS_CANCEL)];
  [button setKeyEquivalent:kKeyEquivalentEscape];
  [button setTarget:target_];
  [button setAction:@selector(cancel:)];
  [button sizeToFit];
  [buttonContainer_ addSubview:button];

  CGFloat nextX = NSMaxX([button frame]) + autofill::kButtonGap;
  button.reset([[ConstrainedWindowButton alloc] initWithFrame:NSZeroRect]);
  [button setFrameOrigin:NSMakePoint(nextX, 0)];
  [button  setTitle:l10n_util::GetNSStringWithFixup(
       IDS_AUTOFILL_DIALOG_SUBMIT_BUTTON)];
  [button setKeyEquivalent:kKeyEquivalentReturn];
  [button setTarget:target_];
  [button setAction:@selector(accept:)];
  [button sizeToFit];
  [buttonContainer_ addSubview:button];

  NSRect frame = NSMakeRect(
      -NSMaxX([button frame]) - chrome_style::kHorizontalPadding, 0,
      NSMaxX([button frame]), NSHeight([button frame]));
  [buttonContainer_ setFrame:frame];
}

- (void)layoutButtons {
  base::scoped_nsobject<GTMUILocalizerAndLayoutTweaker> layoutTweaker(
      [[GTMUILocalizerAndLayoutTweaker alloc] init]);
  [layoutTweaker tweakUI:buttonContainer_];
}

// Compute the preferred size for the legal documents text, given a width.
- (NSSize)preferredLegalDocumentSizeForWidth:(CGFloat)width {
  // Only recompute if necessary (On text or frame width change).
  if (!legalDocumentsSizeDirty_ && abs(legalDocumentsSize_.width-width) < 1.0)
    return legalDocumentsSize_;

  // There's no direct API to compute desired sizes - use layouting instead.
  // Layout in a rect with fixed width and "infinite" height.
  NSRect currentFrame = [legalDocumentsView_ frame];
  [legalDocumentsView_ setFrame:NSMakeRect(0, 0, width, CGFLOAT_MAX)];

  // Now use the layout manager to compute layout.
  NSLayoutManager* layoutManager = [legalDocumentsView_ layoutManager];
  NSTextContainer* textContainer = [legalDocumentsView_ textContainer];
  [layoutManager ensureLayoutForTextContainer:textContainer];
  NSRect newFrame = [layoutManager usedRectForTextContainer:textContainer];

  // And finally, restore old frame.
  [legalDocumentsView_ setFrame:currentFrame];
  newFrame.size.width = width;

  legalDocumentsSizeDirty_ = NO;
  legalDocumentsSize_ = newFrame.size;
  return legalDocumentsSize_;
}

- (AutofillSectionContainer*)sectionForId:(autofill::DialogSection)section {
  return [detailsContainer_ sectionForId:section];
}

- (void)modelChanged {
  [self updateSaveInChrome];
  [self updateWalletIcon];
  [detailsContainer_ modelChanged];
}

- (BOOL)saveDetailsLocally {
  return [saveInChromeCheckbox_ state] == NSOnState;
}

- (void)updateLegalDocuments {
  NSString* text = base::SysUTF16ToNSString(delegate_->LegalDocumentsText());

  if ([text length]) {
    NSFont* font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    [legalDocumentsView_ setMessage:text
                           withFont:font
                       messageColor:[NSColor blackColor]];

    const std::vector<gfx::Range>& link_ranges =
        delegate_->LegalDocumentLinks();
    for (size_t i = 0; i < link_ranges.size(); ++i) {
      NSRange range = link_ranges[i].ToNSRange();
      [legalDocumentsView_ addLinkRange:range
                               withName:@(i)
                              linkColor:[NSColor blueColor]];
    }
    legalDocumentsSizeDirty_ = YES;
  }
  [legalDocumentsView_ setHidden:[text length] == 0];

  // Always request re-layout on state change.
  id delegate = [[[self view] window] windowController];
  if ([delegate respondsToSelector:@selector(requestRelayout)])
    [delegate performSelector:@selector(requestRelayout)];
}

- (void)updateNotificationArea {
  [notificationContainer_ setNotifications:delegate_->CurrentNotifications()];
  id delegate = [[[self view] window] windowController];
  if ([delegate respondsToSelector:@selector(requestRelayout)])
    [delegate performSelector:@selector(requestRelayout)];
}

- (void)setAnchorView:(NSView*)anchorView {
  [notificationContainer_ setAnchorView:anchorView];
}

- (BOOL)validate {
  return [detailsContainer_ validate];
}

- (void)updateSaveInChrome {
  [saveInChromeCheckbox_ setHidden:!delegate_->ShouldOfferToSaveInChrome()];
  [saveInChromeTooltip_ setHidden:[saveInChromeCheckbox_ isHidden]];
  [saveInChromeCheckbox_ setState:
      (delegate_->ShouldSaveInChrome() ? NSOnState : NSOffState)];
}

- (void)updateWalletIcon {
  gfx::Image image = delegate_->ButtonStripImage();
  [buttonStripImage_ setHidden:image.IsEmpty()];
  if (![buttonStripImage_ isHidden]) {
    [buttonStripImage_ setImage:image.ToNSImage()];
    [buttonStripImage_ setFrameSize:[[buttonStripImage_ image] size]];
  }
}

- (void)updateErrorBubble {
  [detailsContainer_ updateErrorBubble];
}

@end


@implementation AutofillMainContainer (Testing)

- (NSButton*)saveInChromeCheckboxForTesting {
  return saveInChromeCheckbox_.get();
}

- (NSImageView*)buttonStripImageForTesting {
  return buttonStripImage_.get();
}

- (NSImageView*)saveInChromeTooltipForTesting {
  return saveInChromeTooltip_.get();
}

@end
