// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DOM_DISTILLER_CONTENT_DOM_DISTILLER_SERVICE_FACTORY_H_
#define COMPONENTS_DOM_DISTILLER_CONTENT_DOM_DISTILLER_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"
#include "components/dom_distiller/core/dom_distiller_service.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace dom_distiller {

// A simple wrapper for DomDistillerService to expose it as a
// BrowserContextKeyedService.
class DomDistillerContextKeyedService : public BrowserContextKeyedService,
                                        public DomDistillerService {
 public:
  DomDistillerContextKeyedService(
      scoped_ptr<DomDistillerStoreInterface> store,
      scoped_ptr<DistillerFactory> distiller_factory);
  virtual ~DomDistillerContextKeyedService() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DomDistillerContextKeyedService);
};

class DomDistillerServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static DomDistillerServiceFactory* GetInstance();
  static DomDistillerContextKeyedService* GetForBrowserContext(
      content::BrowserContext* context);

 private:
  friend struct DefaultSingletonTraits<DomDistillerServiceFactory>;

  DomDistillerServiceFactory();
  virtual ~DomDistillerServiceFactory();

  virtual BrowserContextKeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const OVERRIDE;

  virtual content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const OVERRIDE;
};

}  // namespace dom_distiller

#endif
