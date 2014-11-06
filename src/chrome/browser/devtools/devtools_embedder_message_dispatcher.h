// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_DEVTOOLS_EMBEDDER_MESSAGE_DISPATCHER_H_
#define CHROME_BROWSER_DEVTOOLS_DEVTOOLS_EMBEDDER_MESSAGE_DISPATCHER_H_

#include <map>
#include <string>

#include "base/callback.h"

namespace base {
class ListValue;
}

/**
 * Dispatcher for messages sent from the DevTools frontend running in an
 * isolated renderer (on chrome-devtools://) to the embedder in the browser.
 *
 * The messages are sent via InspectorFrontendHost.sendMessageToEmbedder method.
 */
class DevToolsEmbedderMessageDispatcher {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}

    virtual void ActivateWindow() = 0;
    virtual void CloseWindow() = 0;
    virtual void SetWindowBounds(int x, int y, int width, int height) = 0;
    virtual void MoveWindow(int x, int y) = 0;
    virtual void SetDockSide(const std::string& side) = 0;
    virtual void OpenInNewTab(const std::string& url) = 0;
    virtual void SaveToFile(const std::string& url,
                            const std::string& content,
                            bool save_as) = 0;
    virtual void AppendToFile(const std::string& url,
                              const std::string& content) = 0;
    virtual void RequestFileSystems() = 0;
    virtual void AddFileSystem() = 0;
    virtual void RemoveFileSystem(const std::string& file_system_path) = 0;
    virtual void UpgradeDraggedFileSystemPermissions(
        const std::string& file_system_url) = 0;
    virtual void IndexPath(int request_id,
                           const std::string& file_system_path) = 0;
    virtual void StopIndexing(int request_id) = 0;
    virtual void SearchInPath(int request_id,
                              const std::string& file_system_path,
                              const std::string& query) = 0;
  };

  explicit DevToolsEmbedderMessageDispatcher(Delegate* delegate);

  ~DevToolsEmbedderMessageDispatcher();

  std::string Dispatch(const std::string& method, base::ListValue* params);

 private:
  typedef base::Callback<bool(const base::ListValue&)> Handler;
  void RegisterHandler(const std::string& method, const Handler& handler);

  typedef std::map<std::string, Handler> HandlerMap;
  HandlerMap handlers_;
};

#endif  // CHROME_BROWSER_DEVTOOLS_DEVTOOLS_EMBEDDER_MESSAGE_DISPATCHER_H_
