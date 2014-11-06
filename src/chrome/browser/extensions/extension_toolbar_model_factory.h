// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_FACTORY_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_FACTORY_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"

class ExtensionToolbarModel;
class Profile;

class ExtensionToolbarModelFactory : public BrowserContextKeyedServiceFactory {
 public:
  static ExtensionToolbarModel* GetForProfile(Profile* profile);

  static ExtensionToolbarModelFactory* GetInstance();

 private:
  friend struct DefaultSingletonTraits<ExtensionToolbarModelFactory>;

  ExtensionToolbarModelFactory();
  virtual ~ExtensionToolbarModelFactory();

  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* profile) const OVERRIDE;
  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
  virtual bool ServiceIsCreatedWithBrowserContext() const OVERRIDE;
  virtual bool ServiceIsNULLWhileTesting() const OVERRIDE;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_TOOLBAR_MODEL_FACTORY_H_
