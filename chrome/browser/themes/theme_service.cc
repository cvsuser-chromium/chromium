// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/themes/theme_service.h"

#include "base/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/managed_mode/managed_user_theme.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/browser_theme_pack.h"
#include "chrome/browser/themes/custom_theme_supplier.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/themes/theme_syncable_service.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/user_metrics.h"
#include "grit/theme_resources.h"
#include "grit/ui_resources.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image_skia.h"

#if defined(OS_WIN)
#include "ui/base/win/shell.h"
#endif

using content::BrowserThread;
using content::UserMetricsAction;
using extensions::Extension;
using extensions::UnloadedExtensionInfo;
using ui::ResourceBundle;

typedef ThemeProperties Properties;

// The default theme if we haven't installed a theme yet or if we've clicked
// the "Use Classic" button.
const char* ThemeService::kDefaultThemeID = "";

namespace {

// The default theme if we've gone to the theme gallery and installed the
// "Default" theme. We have to detect this case specifically. (By the time we
// realize we've installed the default theme, we already have an extension
// unpacked on the filesystem.)
const char* kDefaultThemeGalleryID = "hkacjpbfdknhflllbcmjibkdeoafencn";

// Wait this many seconds after startup to garbage collect unused themes.
// Removing unused themes is done after a delay because there is no
// reason to do it at startup.
// ExtensionService::GarbageCollectExtensions() does something similar.
const int kRemoveUnusedThemesStartupDelay = 30;

SkColor IncreaseLightness(SkColor color, double percent) {
  color_utils::HSL result;
  color_utils::SkColorToHSL(color, &result);
  result.l += (1 - result.l) * percent;
  return color_utils::HSLToSkColor(result, SkColorGetA(color));
}

// Writes the theme pack to disk on a separate thread.
void WritePackToDiskCallback(BrowserThemePack* pack,
                             const base::FilePath& path) {
  if (!pack->WriteToDisk(path))
    NOTREACHED() << "Could not write theme pack to disk";
}

}  // namespace

ThemeService::ThemeService()
    : ready_(false),
      rb_(ResourceBundle::GetSharedInstance()),
      profile_(NULL),
      installed_pending_load_id_(kDefaultThemeID),
      number_of_infobars_(0),
      weak_ptr_factory_(this) {
}

ThemeService::~ThemeService() {
  FreePlatformCaches();
}

void ThemeService::Init(Profile* profile) {
  DCHECK(CalledOnValidThread());
  profile_ = profile;

  LoadThemePrefs();

  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSIONS_READY,
                 content::Source<Profile>(profile_));

  theme_syncable_service_.reset(new ThemeSyncableService(profile_, this));
}

gfx::Image ThemeService::GetImageNamed(int id) const {
  DCHECK(CalledOnValidThread());

  gfx::Image image;
  if (theme_supplier_.get())
    image = theme_supplier_->GetImageNamed(id);

  if (image.IsEmpty())
    image = rb_.GetNativeImageNamed(id);

  return image;
}

gfx::ImageSkia* ThemeService::GetImageSkiaNamed(int id) const {
  gfx::Image image = GetImageNamed(id);
  if (image.IsEmpty())
    return NULL;
  // TODO(pkotwicz): Remove this const cast.  The gfx::Image interface returns
  // its images const. GetImageSkiaNamed() also should but has many callsites.
  return const_cast<gfx::ImageSkia*>(image.ToImageSkia());
}

SkColor ThemeService::GetColor(int id) const {
  DCHECK(CalledOnValidThread());
  SkColor color;
  if (theme_supplier_.get() && theme_supplier_->GetColor(id, &color))
    return color;

  // For backward compat with older themes, some newer colors are generated from
  // older ones if they are missing.
  switch (id) {
    case Properties::COLOR_NTP_SECTION_HEADER_TEXT:
      return IncreaseLightness(GetColor(Properties::COLOR_NTP_TEXT), 0.30);
    case Properties::COLOR_NTP_SECTION_HEADER_TEXT_HOVER:
      return GetColor(Properties::COLOR_NTP_TEXT);
    case Properties::COLOR_NTP_SECTION_HEADER_RULE:
      return IncreaseLightness(GetColor(Properties::COLOR_NTP_TEXT), 0.70);
    case Properties::COLOR_NTP_SECTION_HEADER_RULE_LIGHT:
      return IncreaseLightness(GetColor(Properties::COLOR_NTP_TEXT), 0.86);
    case Properties::COLOR_NTP_TEXT_LIGHT:
      return IncreaseLightness(GetColor(Properties::COLOR_NTP_TEXT), 0.40);
    case Properties::COLOR_MANAGED_USER_LABEL:
      return color_utils::GetReadableColor(
          SK_ColorWHITE,
          GetColor(Properties::COLOR_MANAGED_USER_LABEL_BACKGROUND));
    case Properties::COLOR_MANAGED_USER_LABEL_BACKGROUND:
      return color_utils::BlendTowardOppositeLuminance(
          GetColor(Properties::COLOR_FRAME), 0x80);
    case Properties::COLOR_MANAGED_USER_LABEL_BORDER:
      return color_utils::AlphaBlend(
          GetColor(Properties::COLOR_MANAGED_USER_LABEL_BACKGROUND),
          SK_ColorBLACK,
          230);
  }

  return Properties::GetDefaultColor(id);
}

int ThemeService::GetDisplayProperty(int id) const {
  int result = 0;
  if (theme_supplier_.get() &&
      theme_supplier_->GetDisplayProperty(id, &result)) {
    return result;
  }

  if (id == Properties::NTP_LOGO_ALTERNATE &&
      !UsingDefaultTheme() &&
      !UsingNativeTheme()) {
    // Use the alternate logo for themes from the web store except for
    // |kDefaultThemeGalleryID|.
    return 1;
  }

  return Properties::GetDefaultDisplayProperty(id);
}

bool ThemeService::ShouldUseNativeFrame() const {
  if (HasCustomImage(IDR_THEME_FRAME))
    return false;
#if defined(OS_WIN)
  return ui::win::IsAeroGlassEnabled();
#else
  return false;
#endif
}

bool ThemeService::HasCustomImage(int id) const {
  if (!Properties::IsThemeableImage(id))
    return false;

  if (theme_supplier_.get())
    return theme_supplier_->HasCustomImage(id);

  return false;
}

base::RefCountedMemory* ThemeService::GetRawData(
    int id,
    ui::ScaleFactor scale_factor) const {
  // Check to see whether we should substitute some images.
  int ntp_alternate = GetDisplayProperty(Properties::NTP_LOGO_ALTERNATE);
  if (id == IDR_PRODUCT_LOGO && ntp_alternate != 0)
    id = IDR_PRODUCT_LOGO_WHITE;

  base::RefCountedMemory* data = NULL;
  if (theme_supplier_.get())
    data = theme_supplier_->GetRawData(id, scale_factor);
  if (!data)
    data = rb_.LoadDataResourceBytesForScale(id, ui::SCALE_FACTOR_100P);

  return data;
}

void ThemeService::Observe(int type,
                           const content::NotificationSource& source,
                           const content::NotificationDetails& details) {
  using content::Details;
  switch (type) {
    case chrome::NOTIFICATION_EXTENSIONS_READY:
      registrar_.Remove(this, chrome::NOTIFICATION_EXTENSIONS_READY,
          content::Source<Profile>(profile_));
      OnExtensionServiceReady();
      break;
    case chrome::NOTIFICATION_EXTENSION_INSTALLED:
    {
      // The theme may be initially disabled. Wait till it is loaded (if ever).
      Details<const extensions::InstalledExtensionInfo> installed_details(
          details);
      if (installed_details->extension->is_theme())
        installed_pending_load_id_ = installed_details->extension->id();
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_LOADED:
    {
      const Extension* extension = Details<const Extension>(details).ptr();
      if (extension->is_theme() &&
          installed_pending_load_id_ != kDefaultThemeID &&
          installed_pending_load_id_ == extension->id()) {
        SetTheme(extension);
      }
      installed_pending_load_id_ = kDefaultThemeID;
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_ENABLED:
    {
      const Extension* extension = Details<const Extension>(details).ptr();
      if (extension->is_theme())
        SetTheme(extension);
      break;
    }
    case chrome::NOTIFICATION_EXTENSION_UNLOADED:
    {
      Details<const UnloadedExtensionInfo> unloaded_details(details);
      if (unloaded_details->reason != UnloadedExtensionInfo::REASON_UPDATE &&
          unloaded_details->extension->is_theme() &&
          unloaded_details->extension->id() == GetThemeID()) {
        UseDefaultTheme();
      }
      break;
    }
  }
}

void ThemeService::SetTheme(const Extension* extension) {
  DCHECK(extension->is_theme());
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!service->IsExtensionEnabled(extension->id())) {
    // |extension| is disabled when reverting to the previous theme via an
    // infobar.
    service->EnableExtension(extension->id());
    // Enabling the extension will call back to SetTheme().
    return;
  }

  std::string previous_theme_id = GetThemeID();

  // Clear our image cache.
  FreePlatformCaches();

  BuildFromExtension(extension);
  SaveThemeID(extension->id());

  NotifyThemeChanged();
  content::RecordAction(UserMetricsAction("Themes_Installed"));

  if (previous_theme_id != kDefaultThemeID &&
      previous_theme_id != extension->id()) {
    // Disable the old theme.
    service->DisableExtension(previous_theme_id,
                              extensions::Extension::DISABLE_USER_ACTION);
  }
}

void ThemeService::SetCustomDefaultTheme(
    scoped_refptr<CustomThemeSupplier> theme_supplier) {
  ClearAllThemeData();
  SwapThemeSupplier(theme_supplier);
  NotifyThemeChanged();
}

bool ThemeService::ShouldInitWithNativeTheme() const {
  return false;
}

void ThemeService::RemoveUnusedThemes(bool ignore_infobars) {
  // We do not want to garbage collect themes on startup (|ready_| is false).
  // Themes will get garbage collected after |kRemoveUnusedThemesStartupDelay|.
  if (!profile_ || !ready_)
    return;
  if (!ignore_infobars && number_of_infobars_ != 0)
    return;

  ExtensionService* service = profile_->GetExtensionService();
  if (!service)
    return;
  std::string current_theme = GetThemeID();
  std::vector<std::string> remove_list;
  scoped_ptr<const ExtensionSet> extensions(
      service->GenerateInstalledExtensionsSet());
  extensions::ExtensionPrefs* prefs = service->extension_prefs();
  for (ExtensionSet::const_iterator it = extensions->begin();
       it != extensions->end(); ++it) {
    const extensions::Extension* extension = *it;
    if (extension->is_theme() &&
        extension->id() != current_theme) {
      // Only uninstall themes which are not disabled or are disabled with
      // reason DISABLE_USER_ACTION. We cannot blanket uninstall all disabled
      // themes because externally installed themes are initially disabled.
      int disable_reason = prefs->GetDisableReasons(extension->id());
      if (!prefs->IsExtensionDisabled(extension->id()) ||
          disable_reason == Extension::DISABLE_USER_ACTION) {
        remove_list.push_back((*it)->id());
      }
    }
  }
  // TODO: Garbage collect all unused themes. This method misses themes which
  // are installed but not loaded because they are blacklisted by a management
  // policy provider.

  for (size_t i = 0; i < remove_list.size(); ++i)
    service->UninstallExtension(remove_list[i], false, NULL);
}

void ThemeService::UseDefaultTheme() {
  if (ready_)
    content::RecordAction(UserMetricsAction("Themes_Reset"));
  if (IsManagedUser()) {
    SetManagedUserTheme();
    return;
  }
  ClearAllThemeData();
  NotifyThemeChanged();
}

void ThemeService::SetNativeTheme() {
  UseDefaultTheme();
}

bool ThemeService::UsingDefaultTheme() const {
  std::string id = GetThemeID();
  return id == ThemeService::kDefaultThemeID ||
      id == kDefaultThemeGalleryID;
}

bool ThemeService::UsingNativeTheme() const {
  return UsingDefaultTheme();
}

std::string ThemeService::GetThemeID() const {
  return profile_->GetPrefs()->GetString(prefs::kCurrentThemeID);
}

color_utils::HSL ThemeService::GetTint(int id) const {
  DCHECK(CalledOnValidThread());

  color_utils::HSL hsl;
  if (theme_supplier_.get() && theme_supplier_->GetTint(id, &hsl))
    return hsl;

  return ThemeProperties::GetDefaultTint(id);
}

void ThemeService::ClearAllThemeData() {
  if (!ready_)
    return;

  SwapThemeSupplier(NULL);

  // Clear our image cache.
  FreePlatformCaches();

  profile_->GetPrefs()->ClearPref(prefs::kCurrentThemePackFilename);
  SaveThemeID(kDefaultThemeID);

  // There should be no more infobars. This may not be the case because of
  // http://crbug.com/62154
  // RemoveUnusedThemes is called on a task because ClearAllThemeData() may
  // be called as a result of NOTIFICATION_EXTENSION_UNLOADED.
  base::MessageLoop::current()->PostTask(FROM_HERE,
      base::Bind(&ThemeService::RemoveUnusedThemes,
                 weak_ptr_factory_.GetWeakPtr(),
                 true));
}

void ThemeService::LoadThemePrefs() {
  PrefService* prefs = profile_->GetPrefs();

  std::string current_id = GetThemeID();
  if (current_id == kDefaultThemeID) {
    // Managed users have a different default theme.
    if (IsManagedUser())
      SetManagedUserTheme();
    else if (ShouldInitWithNativeTheme())
      SetNativeTheme();
    else
      UseDefaultTheme();
    set_ready();
    return;
  }

  bool loaded_pack = false;

  // If we don't have a file pack, we're updating from an old version.
  base::FilePath path = prefs->GetFilePath(prefs::kCurrentThemePackFilename);
  if (path != base::FilePath()) {
    SwapThemeSupplier(BrowserThemePack::BuildFromDataPack(path, current_id));
    loaded_pack = theme_supplier_.get() != NULL;
  }

  if (loaded_pack) {
    content::RecordAction(UserMetricsAction("Themes.Loaded"));
    set_ready();
  }
  // Else: wait for the extension service to be ready so that the theme pack
  // can be recreated from the extension.
}

void ThemeService::NotifyThemeChanged() {
  if (!ready_)
    return;

  DVLOG(1) << "Sending BROWSER_THEME_CHANGED";
  // Redraw!
  content::NotificationService* service =
      content::NotificationService::current();
  service->Notify(chrome::NOTIFICATION_BROWSER_THEME_CHANGED,
                  content::Source<ThemeService>(this),
                  content::NotificationService::NoDetails());
#if defined(OS_MACOSX)
  NotifyPlatformThemeChanged();
#endif  // OS_MACOSX

  // Notify sync that theme has changed.
  if (theme_syncable_service_.get()) {
    theme_syncable_service_->OnThemeChange();
  }
}

#if defined(OS_WIN) || defined(USE_AURA)
void ThemeService::FreePlatformCaches() {
  // Views (Skia) has no platform image cache to clear.
}
#endif

void ThemeService::OnExtensionServiceReady() {
  if (!ready_) {
    // If the ThemeService is not ready yet, the custom theme data pack needs to
    // be recreated from the extension.
    MigrateTheme();
    set_ready();

    // Send notification in case anyone requested data and cached it when the
    // theme service was not ready yet.
    NotifyThemeChanged();
  }

  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_INSTALLED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_LOADED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_ENABLED,
                 content::Source<Profile>(profile_));
  registrar_.Add(this,
                 chrome::NOTIFICATION_EXTENSION_UNLOADED,
                 content::Source<Profile>(profile_));

  base::MessageLoop::current()->PostDelayedTask(FROM_HERE,
      base::Bind(&ThemeService::RemoveUnusedThemes,
                 weak_ptr_factory_.GetWeakPtr(),
                 false),
      base::TimeDelta::FromSeconds(kRemoveUnusedThemesStartupDelay));
}

void ThemeService::MigrateTheme() {
  // TODO(erg): We need to pop up a dialog informing the user that their
  // theme is being migrated.
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  const Extension* extension = service ?
      service->GetExtensionById(GetThemeID(), false) : NULL;
  if (extension) {
    DLOG(ERROR) << "Migrating theme";
    BuildFromExtension(extension);
    content::RecordAction(UserMetricsAction("Themes.Migrated"));
  } else {
    DLOG(ERROR) << "Theme is mysteriously gone.";
    ClearAllThemeData();
    content::RecordAction(UserMetricsAction("Themes.Gone"));
  }
}

void ThemeService::SwapThemeSupplier(
    scoped_refptr<CustomThemeSupplier> theme_supplier) {
  if (theme_supplier_.get())
    theme_supplier_->StopUsingTheme();
  theme_supplier_ = theme_supplier;
  if (theme_supplier_.get())
    theme_supplier_->StartUsingTheme();
}

void ThemeService::SavePackName(const base::FilePath& pack_path) {
  profile_->GetPrefs()->SetFilePath(
      prefs::kCurrentThemePackFilename, pack_path);
}

void ThemeService::SaveThemeID(const std::string& id) {
  profile_->GetPrefs()->SetString(prefs::kCurrentThemeID, id);
}

void ThemeService::BuildFromExtension(const Extension* extension) {
  scoped_refptr<BrowserThemePack> pack(
      BrowserThemePack::BuildFromExtension(extension));
  if (!pack.get()) {
    // TODO(erg): We've failed to install the theme; perhaps we should tell the
    // user? http://crbug.com/34780
    LOG(ERROR) << "Could not load theme.";
    return;
  }

  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!service)
    return;

  // Write the packed file to disk.
  base::FilePath pack_path =
      extension->path().Append(chrome::kThemePackFilename);
  service->GetFileTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&WritePackToDiskCallback, pack, pack_path));

  SavePackName(pack_path);
  SwapThemeSupplier(pack);
}

bool ThemeService::IsManagedUser() const {
  return profile_->IsManaged();
}

void ThemeService::SetManagedUserTheme() {
  SetCustomDefaultTheme(new ManagedUserTheme);
}

void ThemeService::OnInfobarDisplayed() {
  number_of_infobars_++;
}

void ThemeService::OnInfobarDestroyed() {
  number_of_infobars_--;

  if (number_of_infobars_ == 0)
    RemoveUnusedThemes(false);
}

ThemeSyncableService* ThemeService::GetThemeSyncableService() const {
  return theme_syncable_service_.get();
}
