// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"

#include "ash/ash_switches.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ref_counted_memory.h"
#include "base/values.h"
#include "chrome/browser/browser_about_handler.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/kiosk_mode/kiosk_mode_settings.h"
#include "chrome/browser/chromeos/login/enrollment/enrollment_screen_actor.h"
#include "chrome/browser/chromeos/login/login_display_host_impl.h"
#include "chrome/browser/chromeos/login/screen_locker.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/system/input_device_settings.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/about_ui.h"
#include "chrome/browser/ui/webui/chromeos/login/app_launch_splash_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/enrollment_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/error_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/eula_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/kiosk_app_menu_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/kiosk_autolaunch_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/kiosk_enable_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/locally_managed_user_creation_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/network_dropdown_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/network_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/network_state_informer.h"
#include "chrome/browser/ui/webui/chromeos/login/reset_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/signin_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/terms_of_service_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/update_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/user_image_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/wrong_hwid_screen_handler.h"
#include "chrome/browser/ui/webui/options/chromeos/user_image_source.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/url_constants.h"
#include "chromeos/chromeos_constants.h"
#include "chromeos/chromeos_switches.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "grit/browser_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/web_ui_util.h"

namespace chromeos {

namespace {

// List of known types of OobeUI. Type added as path in chrome://oobe url, for
// example chrome://oobe/user-adding.
const char kOobeDisplay[] = "oobe";
const char kLoginDisplay[] = "login";
const char kLockDisplay[] = "lock";
const char kUserAddingDisplay[] = "user-adding";
const char kAppLaunchSplashDisplay[] = "app-launch-splash";

const char* kKnownDisplayTypes[] = {
  kOobeDisplay,
  kLoginDisplay,
  kLockDisplay,
  kUserAddingDisplay,
  kAppLaunchSplashDisplay
};

const char kStringsJSPath[] = "strings.js";
const char kLoginJSPath[] = "login.js";
const char kOobeJSPath[] = "oobe.js";
const char kKeyboardUtilsJSPath[] = "keyboard_utils.js";
const char kDemoUserLoginJSPath[] = "demo_user_login.js";

// Paths for deferred resource loading.
const char kEnrollmentHTMLPath[] = "enrollment.html";
const char kEnrollmentCSSPath[] = "enrollment.css";
const char kEnrollmentJSPath[] = "enrollment.js";

// Filter handler of chrome://oobe data source.
bool HandleRequestCallback(
    const std::string& path,
    const content::WebUIDataSource::GotDataCallback& callback) {
  if (UserManager::Get()->IsUserLoggedIn() &&
      !UserManager::Get()->IsLoggedInAsStub() &&
      !ScreenLocker::default_screen_locker()) {
    scoped_refptr<base::RefCountedBytes> empty_bytes =
        new base::RefCountedBytes();
    callback.Run(empty_bytes.get());
    return true;
  }

  return false;
}

// Creates a WebUIDataSource for chrome://oobe
content::WebUIDataSource* CreateOobeUIDataSource(
    const base::DictionaryValue& localized_strings,
    const std::string& display_type) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUIOobeHost);
  source->SetUseJsonJSFormatV2();
  source->AddLocalizedStrings(localized_strings);
  source->SetJsonPath(kStringsJSPath);

  if (chromeos::KioskModeSettings::Get()->IsKioskModeEnabled()) {
    source->SetDefaultResource(IDR_DEMO_USER_LOGIN_HTML);
    source->AddResourcePath(kDemoUserLoginJSPath, IDR_DEMO_USER_LOGIN_JS);
    return source;
  }
  if (display_type == kOobeDisplay) {
    source->SetDefaultResource(IDR_OOBE_HTML);
    source->AddResourcePath(kOobeJSPath, IDR_OOBE_JS);
  } else {
    source->SetDefaultResource(IDR_LOGIN_HTML);
    source->AddResourcePath(kLoginJSPath, IDR_LOGIN_JS);
  }
  source->AddResourcePath(kKeyboardUtilsJSPath, IDR_KEYBOARD_UTILS_JS);
  source->OverrideContentSecurityPolicyFrameSrc(
      "frame-src chrome://terms/ "
      "chrome-extension://mfffpogegjflfpflabcdkioaeobkgjik/;");

  // Serve deferred resources.
  source->AddResourcePath(kEnrollmentHTMLPath, IDR_OOBE_ENROLLMENT_HTML);
  source->AddResourcePath(kEnrollmentCSSPath, IDR_OOBE_ENROLLMENT_CSS);
  source->AddResourcePath(kEnrollmentJSPath, IDR_OOBE_ENROLLMENT_JS);

  return source;
}

std::string GetDisplayType(const GURL& url) {
  std::string path = url.path().size() ? url.path().substr(1) : "";
  if (std::find(kKnownDisplayTypes,
                kKnownDisplayTypes + arraysize(kKnownDisplayTypes),
                path) == kKnownDisplayTypes + arraysize(kKnownDisplayTypes)) {
    LOG(ERROR) << "Unknown display type '" << path << "'. Setting default.";
    return kLoginDisplay;
  }
  return path;
}

}  // namespace

// static
const char OobeUI::kScreenOobeNetwork[]     = "connect";
const char OobeUI::kScreenOobeEula[]        = "eula";
const char OobeUI::kScreenOobeUpdate[]      = "update";
const char OobeUI::kScreenOobeEnrollment[]  = "oauth-enrollment";
const char OobeUI::kScreenGaiaSignin[]      = "gaia-signin";
const char OobeUI::kScreenAccountPicker[]   = "account-picker";
const char OobeUI::kScreenKioskAutolaunch[] = "autolaunch";
const char OobeUI::kScreenKioskEnable[]     = "kiosk-enable";
const char OobeUI::kScreenErrorMessage[]    = "error-message";
const char OobeUI::kScreenUserImagePicker[] = "user-image";
const char OobeUI::kScreenTpmError[]        = "tpm-error-message";
const char OobeUI::kScreenPasswordChanged[] = "password-changed";
const char OobeUI::kScreenManagedUserCreationFlow[]
                                            = "managed-user-creation";
const char OobeUI::kScreenTermsOfService[]  = "terms-of-service";
const char OobeUI::kScreenWrongHWID[]       = "wrong-hwid";
const char OobeUI::kScreenAppLaunchSplash[] = "app-launch-splash";
const char OobeUI::kScreenConfirmPassword[] = "confirm-password";
const char OobeUI::kScreenMessageBox[]      = "message-box";

OobeUI::OobeUI(content::WebUI* web_ui, const GURL& url)
    : WebUIController(web_ui),
      core_handler_(NULL),
      network_dropdown_handler_(NULL),
      update_screen_handler_(NULL),
      network_screen_actor_(NULL),
      eula_screen_actor_(NULL),
      reset_screen_actor_(NULL),
      autolaunch_screen_actor_(NULL),
      kiosk_enable_screen_actor_(NULL),
      wrong_hwid_screen_actor_(NULL),
      locally_managed_user_creation_screen_actor_(NULL),
      error_screen_handler_(NULL),
      signin_screen_handler_(NULL),
      terms_of_service_screen_actor_(NULL),
      user_image_screen_actor_(NULL),
      kiosk_app_menu_handler_(NULL),
      current_screen_(SCREEN_UNKNOWN),
      ready_(false) {
  display_type_ = GetDisplayType(url);
  InitializeScreenMaps();

  network_state_informer_ = new NetworkStateInformer();
  network_state_informer_->Init();

  core_handler_ = new CoreOobeHandler(this);
  AddScreenHandler(core_handler_);
  core_handler_->SetDelegate(this);

  network_dropdown_handler_ = new NetworkDropdownHandler();
  AddScreenHandler(network_dropdown_handler_);

  update_screen_handler_ = new UpdateScreenHandler();
  AddScreenHandler(update_screen_handler_);
  network_dropdown_handler_->AddObserver(update_screen_handler_);

  if (display_type_ == kOobeDisplay) {
    NetworkScreenHandler* network_screen_handler =
        new NetworkScreenHandler(core_handler_);
    network_screen_actor_ = network_screen_handler;
    AddScreenHandler(network_screen_handler);
  }

  EulaScreenHandler* eula_screen_handler = new EulaScreenHandler(core_handler_);
  eula_screen_actor_ = eula_screen_handler;
  AddScreenHandler(eula_screen_handler);

  ResetScreenHandler* reset_screen_handler = new ResetScreenHandler();
  reset_screen_actor_ = reset_screen_handler;
  AddScreenHandler(reset_screen_handler);

  KioskAutolaunchScreenHandler* autolaunch_screen_handler =
      new KioskAutolaunchScreenHandler();
  autolaunch_screen_actor_ = autolaunch_screen_handler;
  AddScreenHandler(autolaunch_screen_handler);

  KioskEnableScreenHandler* kiosk_enable_screen_handler =
      new KioskEnableScreenHandler();
  kiosk_enable_screen_actor_ = kiosk_enable_screen_handler;
  AddScreenHandler(kiosk_enable_screen_handler);

  LocallyManagedUserCreationScreenHandler*
      locally_managed_user_creation_screen_handler =
          new LocallyManagedUserCreationScreenHandler();
  locally_managed_user_creation_screen_actor_ =
      locally_managed_user_creation_screen_handler;
  AddScreenHandler(locally_managed_user_creation_screen_handler);

  WrongHWIDScreenHandler* wrong_hwid_screen_handler =
      new WrongHWIDScreenHandler();
  wrong_hwid_screen_actor_ = wrong_hwid_screen_handler;
  AddScreenHandler(wrong_hwid_screen_handler);

  EnrollmentScreenHandler* enrollment_screen_handler =
      new EnrollmentScreenHandler();
  enrollment_screen_actor_ = enrollment_screen_handler;
  AddScreenHandler(enrollment_screen_handler);

  TermsOfServiceScreenHandler* terms_of_service_screen_handler =
      new TermsOfServiceScreenHandler;
  terms_of_service_screen_actor_ = terms_of_service_screen_handler;
  AddScreenHandler(terms_of_service_screen_handler);

  UserImageScreenHandler* user_image_screen_handler =
      new UserImageScreenHandler();
  user_image_screen_actor_ = user_image_screen_handler;
  AddScreenHandler(user_image_screen_handler);

  error_screen_handler_ = new ErrorScreenHandler(network_state_informer_);
  AddScreenHandler(error_screen_handler_);

  signin_screen_handler_ = new SigninScreenHandler(network_state_informer_,
                                                   error_screen_handler_,
                                                   core_handler_);
  AddScreenHandler(signin_screen_handler_);

  AppLaunchSplashScreenHandler* app_launch_splash_screen_handler =
      new AppLaunchSplashScreenHandler();
  AddScreenHandler(app_launch_splash_screen_handler);
  app_launch_splash_screen_actor_ = app_launch_splash_screen_handler;

  // Initialize KioskAppMenuHandler. Note that it is NOT a screen handler.
  kiosk_app_menu_handler_ = new KioskAppMenuHandler;
  web_ui->AddMessageHandler(kiosk_app_menu_handler_);

  base::DictionaryValue localized_strings;
  GetLocalizedStrings(&localized_strings);

  Profile* profile = Profile::FromWebUI(web_ui);
  // Set up the chrome://theme/ source, for Chrome logo.
  ThemeSource* theme = new ThemeSource(profile);
  content::URLDataSource::Add(profile, theme);

  // Set up the chrome://terms/ data source, for EULA content.
  AboutUIHTMLSource* about_source =
      new AboutUIHTMLSource(chrome::kChromeUITermsHost, profile);
  content::URLDataSource::Add(profile, about_source);

  // Set up the chrome://oobe/ source.
  content::WebUIDataSource::Add(profile,
                                CreateOobeUIDataSource(localized_strings,
                                                       display_type_));

  // Set up the chrome://userimage/ source.
  options::UserImageSource* user_image_source =
      new options::UserImageSource();
  content::URLDataSource::Add(profile, user_image_source);
}

OobeUI::~OobeUI() {
  core_handler_->SetDelegate(NULL);
  network_dropdown_handler_->RemoveObserver(update_screen_handler_);
}

void OobeUI::ShowScreen(WizardScreen* screen) {
  screen->Show();
}

void OobeUI::HideScreen(WizardScreen* screen) {
  screen->Hide();
}

UpdateScreenActor* OobeUI::GetUpdateScreenActor() {
  return update_screen_handler_;
}

NetworkScreenActor* OobeUI::GetNetworkScreenActor() {
  return network_screen_actor_;
}

EulaScreenActor* OobeUI::GetEulaScreenActor() {
  return eula_screen_actor_;
}

EnrollmentScreenActor* OobeUI::GetEnrollmentScreenActor() {
  return enrollment_screen_actor_;
}

ResetScreenActor* OobeUI::GetResetScreenActor() {
  return reset_screen_actor_;
}

KioskAutolaunchScreenActor* OobeUI::GetKioskAutolaunchScreenActor() {
  return autolaunch_screen_actor_;
}

KioskEnableScreenActor* OobeUI::GetKioskEnableScreenActor() {
  return kiosk_enable_screen_actor_;
}

TermsOfServiceScreenActor* OobeUI::GetTermsOfServiceScreenActor() {
  return terms_of_service_screen_actor_;
}

WrongHWIDScreenActor* OobeUI::GetWrongHWIDScreenActor() {
  return wrong_hwid_screen_actor_;
}

UserImageScreenActor* OobeUI::GetUserImageScreenActor() {
  return user_image_screen_actor_;
}

ErrorScreenActor* OobeUI::GetErrorScreenActor() {
  return error_screen_handler_;
}

LocallyManagedUserCreationScreenHandler*
    OobeUI::GetLocallyManagedUserCreationScreenActor() {
  return locally_managed_user_creation_screen_actor_;
}

AppLaunchSplashScreenActor*
      OobeUI::GetAppLaunchSplashScreenActor() {
  return app_launch_splash_screen_actor_;
}

void OobeUI::GetLocalizedStrings(base::DictionaryValue* localized_strings) {
  // Note, handlers_[0] is a GenericHandler used by the WebUI.
  for (size_t i = 0; i < handlers_.size(); ++i)
    handlers_[i]->GetLocalizedStrings(localized_strings);
  webui::SetFontAndTextDirection(localized_strings);
  kiosk_app_menu_handler_->GetLocalizedStrings(localized_strings);

#if defined(GOOGLE_CHROME_BUILD)
  localized_strings->SetString("buildType", "chrome");
#else
  localized_strings->SetString("buildType", "chromium");
#endif

  // If we're not doing boot animation then WebUI should trigger
  // wallpaper load on boot.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableBootAnimation)) {
    localized_strings->SetString("bootIntoWallpaper", "on");
  } else {
    localized_strings->SetString("bootIntoWallpaper", "off");
  }

  bool keyboard_driven_oobe =
      system::keyboard_settings::ForceKeyboardDrivenUINavigation();
  localized_strings->SetString("highlightStrength",
                               keyboard_driven_oobe ? "strong" : "normal");
}

void OobeUI::InitializeScreenMaps() {
  screen_names_.resize(SCREEN_UNKNOWN);
  screen_names_[SCREEN_OOBE_NETWORK] = kScreenOobeNetwork;
  screen_names_[SCREEN_OOBE_EULA] = kScreenOobeEula;
  screen_names_[SCREEN_OOBE_UPDATE] = kScreenOobeUpdate;
  screen_names_[SCREEN_OOBE_ENROLLMENT] = kScreenOobeEnrollment;
  screen_names_[SCREEN_GAIA_SIGNIN] = kScreenGaiaSignin;
  screen_names_[SCREEN_ACCOUNT_PICKER] = kScreenAccountPicker;
  screen_names_[SCREEN_KIOSK_AUTOLAUNCH] = kScreenKioskAutolaunch;
  screen_names_[SCREEN_KIOSK_ENABLE] = kScreenKioskEnable;
  screen_names_[SCREEN_ERROR_MESSAGE] = kScreenErrorMessage;
  screen_names_[SCREEN_USER_IMAGE_PICKER] = kScreenUserImagePicker;
  screen_names_[SCREEN_TPM_ERROR] = kScreenTpmError;
  screen_names_[SCREEN_PASSWORD_CHANGED] = kScreenPasswordChanged;
  screen_names_[SCREEN_CREATE_MANAGED_USER_FLOW] =
      kScreenManagedUserCreationFlow;
  screen_names_[SCREEN_TERMS_OF_SERVICE] = kScreenTermsOfService;
  screen_names_[SCREEN_WRONG_HWID] = kScreenWrongHWID;
  screen_names_[SCREEN_APP_LAUNCH_SPLASH] = kScreenAppLaunchSplash;
  screen_names_[SCREEN_CONFIRM_PASSWORD] = kScreenConfirmPassword;
  screen_names_[SCREEN_MESSAGE_BOX] = kScreenMessageBox;

  screen_ids_.clear();
  for (size_t i = 0; i < screen_names_.size(); ++i)
    screen_ids_[screen_names_[i]] = static_cast<Screen>(i);
}

void OobeUI::AddScreenHandler(BaseScreenHandler* handler) {
  web_ui()->AddMessageHandler(handler);
  handlers_.push_back(handler);
}

void OobeUI::InitializeHandlers() {
  ready_ = true;
  for (size_t i = 0; i < ready_callbacks_.size(); ++i)
    ready_callbacks_[i].Run();
  ready_callbacks_.clear();

  for (size_t i = 0; i < handlers_.size(); ++i)
    handlers_[i]->InitializeBase();
}

bool OobeUI::IsJSReady(const base::Closure& display_is_ready_callback) {
  if (!ready_)
    ready_callbacks_.push_back(display_is_ready_callback);
  return ready_;
}

void OobeUI::ShowOobeUI(bool show) {
  core_handler_->ShowOobeUI(show);
}

void OobeUI::ShowRetailModeLoginSpinner() {
  signin_screen_handler_->ShowRetailModeLoginSpinner();
}

void OobeUI::ShowSigninScreen(SigninScreenHandlerDelegate* delegate,
                              NativeWindowDelegate* native_window_delegate) {
  signin_screen_handler_->SetDelegate(delegate);
  signin_screen_handler_->SetNativeWindowDelegate(native_window_delegate);
  signin_screen_handler_->Show(core_handler_->show_oobe_ui());
}

void OobeUI::ResetSigninScreenHandlerDelegate() {
  signin_screen_handler_->SetDelegate(NULL);
  signin_screen_handler_->SetNativeWindowDelegate(NULL);
}


void OobeUI::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void OobeUI::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

const std::string& OobeUI::GetScreenName(Screen screen) const {
  DCHECK(screen >= 0 && screen < SCREEN_UNKNOWN);
  return screen_names_[static_cast<size_t>(screen)];
}

void OobeUI::OnCurrentScreenChanged(const std::string& screen) {
  if (screen_ids_.count(screen)) {
    Screen new_screen = screen_ids_[screen];
    FOR_EACH_OBSERVER(Observer,
                      observer_list_,
                      OnCurrentScreenChanged(current_screen_, new_screen));
    current_screen_ = new_screen;
  } else {
    NOTREACHED() << "Screen should be registered in InitializeScreenMaps()";
    current_screen_ = SCREEN_UNKNOWN;
  }
}

}  // namespace chromeos
