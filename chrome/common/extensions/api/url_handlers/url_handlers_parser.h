// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_EXTENSIONS_API_URL_HANDLERS_URL_HANDLERS_PARSER_H_
#define CHROME_COMMON_EXTENSIONS_API_URL_HANDLERS_URL_HANDLERS_PARSER_H_

#include <string>
#include <vector>

#include "chrome/common/extensions/extension.h"
#include "extensions/common/manifest_handler.h"
#include "extensions/common/url_pattern.h"

class GURL;

namespace extensions {

struct UrlHandlerInfo {
  UrlHandlerInfo();
  ~UrlHandlerInfo();

  // ID identifying this handler in the manifest.
  std::string id;
  // Handler title to display in all relevant UI.
  std::string title;
  // URL patterns associated with this handler.
  URLPatternSet patterns;
};

struct UrlHandlers : public Extension::ManifestData {
  UrlHandlers();
  virtual ~UrlHandlers();

  // Returns an array of URL handlers |extension| has defined in its manifest.
  static const std::vector<UrlHandlerInfo>* GetUrlHandlers(
      const Extension* extension);

  // Determines whether |extension| has at least one URL handler that matches
  // |url|.
  static bool CanExtensionHandleUrl(
      const Extension* extension,
      const GURL& url);

  // Finds a matching URL handler for |extension|, if any. Returns NULL in none
  // are found.
  static const UrlHandlerInfo* FindMatchingUrlHandler(
      const Extension* extension,
      const GURL& url);

  std::vector<UrlHandlerInfo> handlers;
};

// Parses the "url_handlers" manifest key.
class UrlHandlersParser : public ManifestHandler {
 public:
  UrlHandlersParser();
  virtual ~UrlHandlersParser();

  // ManifestHandler API
  virtual bool Parse(Extension* extension, string16* error) OVERRIDE;

 private:
  virtual const std::vector<std::string> Keys() const OVERRIDE;

  DISALLOW_COPY_AND_ASSIGN(UrlHandlersParser);
};

}  // namespace extensions

#endif  // CHROME_COMMON_EXTENSIONS_API_URL_HANDLERS_URL_HANDLERS_PARSER_H_
