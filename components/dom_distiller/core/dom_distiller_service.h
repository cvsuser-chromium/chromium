// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_SERVICE_H_
#define COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_SERVICE_H_

#include <string>
#include <vector>

#include "components/dom_distiller/core/article_entry.h"
#include "components/dom_distiller/core/distiller.h"
#include "url/gurl.h"

namespace syncer {
class SyncableService;
}

namespace dom_distiller {

class DistillerFactory;
class DomDistillerStoreInterface;
class ViewerContext;

// A handle to a request to view a DOM distiller entry or URL. The request will
// be cancelled when the handle is destroyed.
class ViewerHandle {
 public:
  ViewerHandle();
  ~ViewerHandle();

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewerHandle);
};

// Provide a view of the article list and ways of interacting with it.
class DomDistillerService {
 public:
  DomDistillerService(scoped_ptr<DomDistillerStoreInterface> store,
                      scoped_ptr<DistillerFactory> distiller_factory);
  ~DomDistillerService();

  syncer::SyncableService* GetSyncableService() const;

  // Distill the article at |url| and add the resulting entry to the DOM
  // distiller list.
  void AddToList(const GURL& url);

  // Gets the full list of entries.
  std::vector<ArticleEntry> GetEntries() const;

  // Request to view an article by entry id. Returns a null pointer if no entry
  // with |entry_id| exists.
  scoped_ptr<ViewerHandle> ViewEntry(ViewerContext* context,
                                     const std::string& entry_id);

  // Request to view an article by url.
  scoped_ptr<ViewerHandle> ViewUrl(ViewerContext* context, const GURL& url);

 private:
  scoped_ptr<DomDistillerStoreInterface> store_;
  scoped_ptr<DistillerFactory> distiller_factory_;

  DISALLOW_COPY_AND_ASSIGN(DomDistillerService);
};

}  // namespace dom_distiller

#endif  // COMPONENTS_DOM_DISTILLER_CORE_DOM_DISTILLER_SERVICE_H_
