// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/braille_display_private/braille_display_private_api.h"

#include "base/lazy_instance.h"
#include "chrome/browser/extensions/api/braille_display_private/braille_controller.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#endif

namespace OnDisplayStateChanged =
    extensions::api::braille_display_private::OnDisplayStateChanged;
namespace OnKeyEvent = extensions::api::braille_display_private::OnKeyEvent;
namespace WriteDots = extensions::api::braille_display_private::WriteDots;
using extensions::api::braille_display_private::DisplayState;
using extensions::api::braille_display_private::KeyEvent;
using extensions::api::braille_display_private::BrailleController;

namespace extensions {

class BrailleDisplayPrivateAPI::DefaultEventDelegate
    : public BrailleDisplayPrivateAPI::EventDelegate {
 public:
  DefaultEventDelegate(EventRouter::Observer* observer, Profile* profile);
  virtual ~DefaultEventDelegate();

  virtual void BroadcastEvent(scoped_ptr<Event> event) OVERRIDE;
  virtual bool HasListener() OVERRIDE;

 private:
  EventRouter::Observer* observer_;
  Profile* profile_;
};

BrailleDisplayPrivateAPI::BrailleDisplayPrivateAPI(Profile* profile)
    : profile_(profile), scoped_observer_(this),
      event_delegate_(new DefaultEventDelegate(this, profile_)) {
}

BrailleDisplayPrivateAPI::~BrailleDisplayPrivateAPI() {
}

void BrailleDisplayPrivateAPI::Shutdown() {
}

static base::LazyInstance<ProfileKeyedAPIFactory<BrailleDisplayPrivateAPI> >
g_factory = LAZY_INSTANCE_INITIALIZER;

// static
ProfileKeyedAPIFactory<BrailleDisplayPrivateAPI>*
BrailleDisplayPrivateAPI::GetFactoryInstance() {
  return &g_factory.Get();
}

void BrailleDisplayPrivateAPI::OnDisplayStateChanged(
    const DisplayState& display_state) {
  scoped_ptr<Event> event(new Event(
      OnDisplayStateChanged::kEventName,
      OnDisplayStateChanged::Create(display_state)));
  event_delegate_->BroadcastEvent(event.Pass());
}

void BrailleDisplayPrivateAPI::OnKeyEvent(
    const KeyEvent& key_event) {
  // Key events only go to extensions of the active profile.
  if (!IsProfileActive())
    return;
  scoped_ptr<Event> event(new Event(
      OnKeyEvent::kEventName, OnKeyEvent::Create(key_event)));
  event_delegate_->BroadcastEvent(event.Pass());
}

bool BrailleDisplayPrivateAPI::IsProfileActive() {
#if defined(OS_CHROMEOS)
  Profile* active_profile;
  chromeos::ScreenLocker* screen_locker =
      chromeos::ScreenLocker::default_screen_locker();
  if (screen_locker && screen_locker->locked())
    active_profile = chromeos::ProfileHelper::GetSigninProfile();
  else
    active_profile = ProfileManager::GetDefaultProfile();
  return profile_->IsSameProfile(active_profile);
#else  // !defined(OS_CHROMEOS)
  return true;
#endif
}

void BrailleDisplayPrivateAPI::SetEventDelegateForTest(
    scoped_ptr<EventDelegate> delegate) {
  event_delegate_ = delegate.Pass();
}

void BrailleDisplayPrivateAPI::OnListenerAdded(
    const EventListenerInfo& details) {
  BrailleController* braille_controller = BrailleController::GetInstance();
  if (!scoped_observer_.IsObserving(braille_controller))
    scoped_observer_.Add(braille_controller);
}

void BrailleDisplayPrivateAPI::OnListenerRemoved(
    const EventListenerInfo& details) {
  BrailleController* braille_controller = BrailleController::GetInstance();
  if (!event_delegate_->HasListener() &&
      scoped_observer_.IsObserving(braille_controller)) {
    scoped_observer_.Remove(braille_controller);
  }
}

BrailleDisplayPrivateAPI::DefaultEventDelegate::DefaultEventDelegate(
    EventRouter::Observer* observer, Profile* profile)
    : observer_(observer), profile_(profile) {
  EventRouter* event_router = ExtensionSystem::Get(profile_)->event_router();
  event_router->RegisterObserver(observer_, OnDisplayStateChanged::kEventName);
  event_router->RegisterObserver(observer_, OnKeyEvent::kEventName);
}

BrailleDisplayPrivateAPI::DefaultEventDelegate::~DefaultEventDelegate() {
  ExtensionSystem::Get(profile_)->event_router()->UnregisterObserver(observer_);
}

void BrailleDisplayPrivateAPI::DefaultEventDelegate::BroadcastEvent(
    scoped_ptr<Event> event) {
  ExtensionSystem::Get(profile_)->event_router()->BroadcastEvent(event.Pass());
}

bool BrailleDisplayPrivateAPI::DefaultEventDelegate::HasListener() {
  EventRouter* event_router = ExtensionSystem::Get(profile_)->event_router();
  return (event_router->HasEventListener(OnDisplayStateChanged::kEventName) ||
          event_router->HasEventListener(OnKeyEvent::kEventName));
}

namespace api {
bool BrailleDisplayPrivateGetDisplayStateFunction::Prepare() {
  return true;
}

void BrailleDisplayPrivateGetDisplayStateFunction::Work() {
  SetResult(
      BrailleController::GetInstance()->GetDisplayState()->ToValue().release());
}

bool BrailleDisplayPrivateGetDisplayStateFunction::Respond() {
  return true;
}

BrailleDisplayPrivateWriteDotsFunction::
BrailleDisplayPrivateWriteDotsFunction() {
}

BrailleDisplayPrivateWriteDotsFunction::
~BrailleDisplayPrivateWriteDotsFunction() {
}

bool BrailleDisplayPrivateWriteDotsFunction::Prepare() {
  params_ = WriteDots::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params_);
  return true;
}

void BrailleDisplayPrivateWriteDotsFunction::Work() {
  BrailleController::GetInstance()->WriteDots(params_->cells);
}

bool BrailleDisplayPrivateWriteDotsFunction::Respond() {
  return true;
}
}  // namespace api
}  // namespace extensions
