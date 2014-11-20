// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ssl/ssl_blocking_page.h"

#include "base/i18n/rtl.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/ssl/ssl_error_info.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "content/public/browser/cert_store.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/ssl_status.h"
#include "grit/app_locale_settings.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "net/base/hash_value.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/jstemplate_builder.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

using base::TimeTicks;
using content::InterstitialPage;
using content::NavigationController;
using content::NavigationEntry;

namespace {

// These represent the commands sent by ssl_roadblock.html.
enum SSLBlockingPageCommands {
  CMD_DONT_PROCEED,
  CMD_PROCEED,
  CMD_MORE,
  CMD_RELOAD,
};

// Events for UMA.
enum SSLBlockingPageEvent {
  SHOW_ALL,
  SHOW_OVERRIDABLE,
  PROCEED_OVERRIDABLE,
  PROCEED_NAME,
  PROCEED_DATE,
  PROCEED_AUTHORITY,
  DONT_PROCEED_OVERRIDABLE,
  DONT_PROCEED_NAME,
  DONT_PROCEED_DATE,
  DONT_PROCEED_AUTHORITY,
  MORE,
  SHOW_UNDERSTAND,  // Used by the summer 2013 Finch trial. Deprecated.
  SHOW_INTERNAL_HOSTNAME,
  PROCEED_INTERNAL_HOSTNAME,
  SHOW_NEW_SITE,
  PROCEED_NEW_SITE,
  UNUSED_BLOCKING_PAGE_EVENT,
};

void RecordSSLBlockingPageEventStats(SSLBlockingPageEvent event) {
  UMA_HISTOGRAM_ENUMERATION("interstitial.ssl",
                            event,
                            UNUSED_BLOCKING_PAGE_EVENT);
}

void RecordSSLBlockingPageDetailedStats(
    bool proceed,
    int cert_error,
    bool overridable,
    bool internal,
    int num_visits) {
  UMA_HISTOGRAM_ENUMERATION("interstitial.ssl_error_type",
     SSLErrorInfo::NetErrorToErrorType(cert_error), SSLErrorInfo::END_OF_ENUM);
  if (!overridable) {
    // Overridable is false if the user didn't have any option except to turn
    // back. If that's the case, don't record some of the metrics.
    return;
  }
  if (num_visits == 0)
    RecordSSLBlockingPageEventStats(SHOW_NEW_SITE);
  if (proceed) {
    RecordSSLBlockingPageEventStats(PROCEED_OVERRIDABLE);
    if (internal)
      RecordSSLBlockingPageEventStats(PROCEED_INTERNAL_HOSTNAME);
    if (num_visits == 0)
      RecordSSLBlockingPageEventStats(PROCEED_NEW_SITE);
  } else if (!proceed) {
    RecordSSLBlockingPageEventStats(DONT_PROCEED_OVERRIDABLE);
  }
  SSLErrorInfo::ErrorType type = SSLErrorInfo::NetErrorToErrorType(cert_error);
  switch (type) {
    case SSLErrorInfo::CERT_COMMON_NAME_INVALID: {
      if (proceed)
        RecordSSLBlockingPageEventStats(PROCEED_NAME);
      else
        RecordSSLBlockingPageEventStats(DONT_PROCEED_NAME);
      break;
    }
    case SSLErrorInfo::CERT_DATE_INVALID: {
      if (proceed)
        RecordSSLBlockingPageEventStats(PROCEED_DATE);
      else
        RecordSSLBlockingPageEventStats(DONT_PROCEED_DATE);
      break;
    }
    case SSLErrorInfo::CERT_AUTHORITY_INVALID: {
      if (proceed)
        RecordSSLBlockingPageEventStats(PROCEED_AUTHORITY);
      else
        RecordSSLBlockingPageEventStats(DONT_PROCEED_AUTHORITY);
      break;
    }
    default: {
      break;
    }
  }
}

}  // namespace

// Note that we always create a navigation entry with SSL errors.
// No error happening loading a sub-resource triggers an interstitial so far.
SSLBlockingPage::SSLBlockingPage(
    content::WebContents* web_contents,
    int cert_error,
    const net::SSLInfo& ssl_info,
    const GURL& request_url,
    bool overridable,
    bool strict_enforcement,
    const base::Callback<void(bool)>& callback)
    : callback_(callback),
      web_contents_(web_contents),
      cert_error_(cert_error),
      ssl_info_(ssl_info),
      request_url_(request_url),
      overridable_(overridable),
      strict_enforcement_(strict_enforcement),
      internal_(false),
      num_visits_(-1) {
  // For UMA stats.
  if (net::IsHostnameNonUnique(request_url_.HostNoBrackets()))
    internal_ = true;
  RecordSSLBlockingPageEventStats(SHOW_ALL);
  if (overridable_ && !strict_enforcement_) {
    RecordSSLBlockingPageEventStats(SHOW_OVERRIDABLE);
    if (internal_)
      RecordSSLBlockingPageEventStats(SHOW_INTERNAL_HOSTNAME);
    HistoryService* history_service = HistoryServiceFactory::GetForProfile(
        Profile::FromBrowserContext(web_contents->GetBrowserContext()),
        Profile::EXPLICIT_ACCESS);
    if (history_service) {
      history_service->GetVisibleVisitCountToHost(
          request_url_,
          &request_consumer_,
          base::Bind(&SSLBlockingPage::OnGotHistoryCount,
                    base::Unretained(this)));
    }
  }

  interstitial_page_ = InterstitialPage::Create(
      web_contents_, true, request_url, this);
  interstitial_page_->Show();
}

SSLBlockingPage::~SSLBlockingPage() {
  if (!callback_.is_null()) {
    RecordSSLBlockingPageDetailedStats(false,
                                       cert_error_,
                                       overridable_ && !strict_enforcement_,
                                       internal_,
                                       num_visits_);
    // The page is closed without the user having chosen what to do, default to
    // deny.
    NotifyDenyCertificate();
  }
}

std::string SSLBlockingPage::GetHTMLContents() {
  DictionaryValue strings;
  int resource_id;
  if (overridable_ && !strict_enforcement_) {
    // Let's build the overridable error page.
    SSLErrorInfo error_info =
        SSLErrorInfo::CreateError(
            SSLErrorInfo::NetErrorToErrorType(cert_error_),
            ssl_info_.cert.get(),
            request_url_);

    resource_id = IDR_SSL_ROAD_BLOCK_HTML;
    strings.SetString("headLine", error_info.title());
    strings.SetString("description", error_info.details());
    strings.SetString("moreInfoTitle",
        l10n_util::GetStringUTF16(IDS_CERT_ERROR_EXTRA_INFO_TITLE));
    SetExtraInfo(&strings, error_info.extra_information());

    strings.SetString(
        "exit", l10n_util::GetStringUTF16(IDS_SSL_OVERRIDABLE_PAGE_EXIT));
    strings.SetString(
        "title", l10n_util::GetStringUTF16(IDS_SSL_OVERRIDABLE_PAGE_TITLE));
    strings.SetString(
        "proceed", l10n_util::GetStringUTF16(IDS_SSL_OVERRIDABLE_PAGE_PROCEED));
    strings.SetString(
        "reasonForNotProceeding", l10n_util::GetStringUTF16(
            IDS_SSL_OVERRIDABLE_PAGE_SHOULD_NOT_PROCEED));
    strings.SetString("errorType", "overridable");
    strings.SetString("textdirection", base::i18n::IsRTL() ? "rtl" : "ltr");
  } else {
    // Let's build the blocking error page.
    resource_id = IDR_SSL_BLOCKING_HTML;

    // Strings that are not dependent on the URL.
    strings.SetString(
        "title", l10n_util::GetStringUTF16(IDS_SSL_BLOCKING_PAGE_TITLE));
    strings.SetString(
        "reloadMsg", l10n_util::GetStringUTF16(IDS_ERRORPAGES_BUTTON_RELOAD));
    strings.SetString(
        "more", l10n_util::GetStringUTF16(IDS_ERRORPAGES_BUTTON_MORE));
    strings.SetString(
        "less", l10n_util::GetStringUTF16(IDS_ERRORPAGES_BUTTON_LESS));
    strings.SetString(
        "moreTitle",
        l10n_util::GetStringUTF16(IDS_SSL_BLOCKING_PAGE_MORE_TITLE));
    strings.SetString(
        "techTitle",
        l10n_util::GetStringUTF16(IDS_SSL_BLOCKING_PAGE_TECH_TITLE));

    // Strings that are dependent on the URL.
    string16 url(ASCIIToUTF16(request_url_.host()));
    bool rtl = base::i18n::IsRTL();
    strings.SetString("textDirection", rtl ? "rtl" : "ltr");
    if (rtl)
      base::i18n::WrapStringWithLTRFormatting(&url);
    strings.SetString(
        "headline", l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_HEADLINE,
                                               url.c_str()));
    strings.SetString(
        "message", l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_BODY_TEXT,
                                              url.c_str()));
    strings.SetString(
        "moreMessage",
        l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_MORE_TEXT,
                                   url.c_str()));
    strings.SetString("reloadUrl", request_url_.spec());

    // Strings that are dependent on the error type.
    SSLErrorInfo::ErrorType type =
        SSLErrorInfo::NetErrorToErrorType(cert_error_);
    string16 errorType;
    if (type == SSLErrorInfo::CERT_REVOKED) {
      errorType = string16(ASCIIToUTF16("Key revocation"));
      strings.SetString(
          "failure",
          l10n_util::GetStringUTF16(IDS_SSL_BLOCKING_PAGE_REVOKED));
    } else if (type == SSLErrorInfo::CERT_INVALID) {
      errorType = string16(ASCIIToUTF16("Malformed certificate"));
      strings.SetString(
          "failure",
          l10n_util::GetStringUTF16(IDS_SSL_BLOCKING_PAGE_FORMATTED));
    } else if (type == SSLErrorInfo::CERT_PINNED_KEY_MISSING) {
      errorType = string16(ASCIIToUTF16("Certificate pinning failure"));
      strings.SetString(
          "failure",
          l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_PINNING,
                                     url.c_str()));
    } else if (type == SSLErrorInfo::CERT_WEAK_KEY_DH) {
      errorType = string16(ASCIIToUTF16("Weak DH public key"));
      strings.SetString(
          "failure",
          l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_WEAK_DH,
                                     url.c_str()));
    } else {
      // HSTS failure.
      errorType = string16(ASCIIToUTF16("HSTS failure"));
      strings.SetString(
          "failure",
          l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_HSTS, url.c_str()));
    }
    if (rtl)
      base::i18n::WrapStringWithLTRFormatting(&errorType);
    strings.SetString(
        "errorType", l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_ERROR,
                                                errorType.c_str()));

    // Strings that display the invalid cert.
    string16 subject(ASCIIToUTF16(ssl_info_.cert->subject().GetDisplayName()));
    string16 issuer(ASCIIToUTF16(ssl_info_.cert->issuer().GetDisplayName()));
    std::string hashes;
    for (std::vector<net::HashValue>::iterator it =
            ssl_info_.public_key_hashes.begin();
         it != ssl_info_.public_key_hashes.end();
         ++it) {
      base::StringAppendF(&hashes, "%s ", it->ToString().c_str());
    }
    string16 fingerprint(ASCIIToUTF16(hashes));
    if (rtl) {
      // These are always going to be LTR.
      base::i18n::WrapStringWithLTRFormatting(&subject);
      base::i18n::WrapStringWithLTRFormatting(&issuer);
      base::i18n::WrapStringWithLTRFormatting(&fingerprint);
    }
    strings.SetString(
        "subject", l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_SUBJECT,
                                              subject.c_str()));
    strings.SetString(
        "issuer", l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_ISSUER,
                                             issuer.c_str()));
    strings.SetString(
        "fingerprint",
        l10n_util::GetStringFUTF16(IDS_SSL_BLOCKING_PAGE_HASHES,
                                   fingerprint.c_str()));
  }

  base::StringPiece html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          resource_id));
  return webui::GetI18nTemplateHtml(html, &strings);
}

void SSLBlockingPage::OverrideEntry(NavigationEntry* entry) {
  int cert_id = content::CertStore::GetInstance()->StoreCert(
      ssl_info_.cert.get(), web_contents_->GetRenderProcessHost()->GetID());

  entry->GetSSL().security_style =
      content::SECURITY_STYLE_AUTHENTICATION_BROKEN;
  entry->GetSSL().cert_id = cert_id;
  entry->GetSSL().cert_status = ssl_info_.cert_status;
  entry->GetSSL().security_bits = ssl_info_.security_bits;
#if !defined(OS_ANDROID)
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents_);
  if (browser)
    browser->VisibleSSLStateChanged(web_contents_);
#endif  // !defined(OS_ANDROID)
}

// Matches events defined in ssl_error.html and ssl_roadblock.html.
void SSLBlockingPage::CommandReceived(const std::string& command) {
  int cmd = atoi(command.c_str());
  if (cmd == CMD_DONT_PROCEED) {
    interstitial_page_->DontProceed();
  } else if (cmd == CMD_PROCEED) {
    interstitial_page_->Proceed();
  } else if (cmd == CMD_MORE) {
    RecordSSLBlockingPageEventStats(MORE);
  } else if (cmd == CMD_RELOAD) {
    // The interstitial can't refresh itself.
    content::NavigationController* controller = &web_contents_->GetController();
    controller->Reload(true);
  }
}

void SSLBlockingPage::OverrideRendererPrefs(
      content::RendererPreferences* prefs) {
  Profile* profile = Profile::FromBrowserContext(
      web_contents_->GetBrowserContext());
  renderer_preferences_util::UpdateFromSystemSettings(prefs, profile);
}

void SSLBlockingPage::OnProceed() {
  RecordSSLBlockingPageDetailedStats(true,
                                     cert_error_,
                                     overridable_ && !strict_enforcement_,
                                     internal_,
                                     num_visits_);
  // Accepting the certificate resumes the loading of the page.
  NotifyAllowCertificate();
}

void SSLBlockingPage::OnDontProceed() {
  RecordSSLBlockingPageDetailedStats(false,
                                     cert_error_,
                                     overridable_ && !strict_enforcement_,
                                     internal_,
                                     num_visits_);
  NotifyDenyCertificate();
}

void SSLBlockingPage::NotifyDenyCertificate() {
  // It's possible that callback_ may not exist if the user clicks "Proceed"
  // followed by pressing the back button before the interstitial is hidden.
  // In that case the certificate will still be treated as allowed.
  if (callback_.is_null())
    return;

  callback_.Run(false);
  callback_.Reset();
}

void SSLBlockingPage::NotifyAllowCertificate() {
  DCHECK(!callback_.is_null());

  callback_.Run(true);
  callback_.Reset();
}

// static
void SSLBlockingPage::SetExtraInfo(
    DictionaryValue* strings,
    const std::vector<string16>& extra_info) {
  DCHECK_LT(extra_info.size(), 5U);  // We allow 5 paragraphs max.
  const char* keys[5] = {
      "moreInfo1", "moreInfo2", "moreInfo3", "moreInfo4", "moreInfo5"
  };
  int i;
  for (i = 0; i < static_cast<int>(extra_info.size()); i++) {
    strings->SetString(keys[i], extra_info[i]);
  }
  for (; i < 5; i++) {
    strings->SetString(keys[i], std::string());
  }
}

void SSLBlockingPage::OnGotHistoryCount(HistoryService::Handle handle,
                                        bool success,
                                        int num_visits,
                                        base::Time first_visit) {
  num_visits_ = num_visits;
}
