// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_DEVTOOLS_ADB_BRIDGE_H_
#define CHROME_BROWSER_DEVTOOLS_DEVTOOLS_ADB_BRIDGE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/devtools/android_device.h"
#include "chrome/browser/devtools/refcounted_adb_thread.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service.h"
#include "components/browser_context_keyed_service/browser_context_keyed_service_factory.h"
#include "content/public/browser/browser_thread.h"
#include "net/socket/tcp_client_socket.h"
#include "ui/gfx/size.h"

template<typename T> struct DefaultSingletonTraits;

namespace base {
class MessageLoop;
class DictionaryValue;
class ListValue;
class Thread;
}

namespace content {
class BrowserContext;
}

namespace crypto {
class RSAPrivateKey;
}

class DevToolsTargetImpl;
class Profile;

// The format used for constructing DevTools server socket names.
extern const char kDevToolsChannelNameFormat[];

class DevToolsAdbBridge
    : public base::RefCountedThreadSafe<
          DevToolsAdbBridge,
          content::BrowserThread::DeleteOnUIThread> {
 public:
  typedef base::Callback<void(int result,
                              const std::string& response)> Callback;
  typedef std::vector<scoped_refptr<AndroidDeviceProvider> > DeviceProviders;


  class Wrapper : public BrowserContextKeyedService {
   public:
    Wrapper();
    virtual ~Wrapper();

    DevToolsAdbBridge* Get();
   private:
    scoped_refptr<DevToolsAdbBridge> bridge_;
  };

  class Factory : public BrowserContextKeyedServiceFactory {
   public:
    // Returns singleton instance of DevToolsAdbBridge.
    static Factory* GetInstance();

    // Returns DevToolsAdbBridge associated with |profile|.
    static DevToolsAdbBridge* GetForProfile(Profile* profile);

   private:
    friend struct DefaultSingletonTraits<Factory>;

    Factory();
    virtual ~Factory();

    // BrowserContextKeyedServiceFactory overrides:
    virtual BrowserContextKeyedService* BuildServiceInstanceFor(
        content::BrowserContext* context) const OVERRIDE;
    DISALLOW_COPY_AND_ASSIGN(Factory);
  };

  class RemoteBrowser : public base::RefCounted<RemoteBrowser> {
   public:
    RemoteBrowser(
        scoped_refptr<RefCountedAdbThread> adb_thread,
        scoped_refptr<AndroidDevice> device,
        const std::string& socket);

    scoped_refptr<RefCountedAdbThread> adb_thread() { return adb_thread_; }

    scoped_refptr<AndroidDevice> device() { return device_; }
    std::string socket() { return socket_; }

    std::string display_name() { return display_name_; }
    void set_display_name(const std::string& name) { display_name_ = name; }

    std::string version() { return version_; }
    void set_version(const std::string& version) { version_ = version; }

    bool IsChrome() const;

    typedef std::vector<int> ParsedVersion;
    ParsedVersion GetParsedVersion() const;

    std::vector<DevToolsTargetImpl*> CreatePageTargets();
    void SetPageDescriptors(const base::ListValue&);

    void SendJsonRequest(const std::string& request, base::Closure callback);
    void SendProtocolCommand(const std::string& debug_url,
                             const std::string& method,
                             base::DictionaryValue* params);

    void Open(const std::string& url);

   private:
    friend class base::RefCounted<RemoteBrowser>;
    virtual ~RemoteBrowser();

    void PageCreatedOnHandlerThread(
        const std::string& url, int result, const std::string& response);

    void PageCreatedOnUIThread(
        const std::string& response, const std::string& url);

    scoped_refptr<RefCountedAdbThread> adb_thread_;
    scoped_refptr<AndroidDevice> device_;
    const std::string socket_;
    std::string display_name_;
    std::string version_;
    scoped_ptr<base::ListValue> page_descriptors_;

    DISALLOW_COPY_AND_ASSIGN(RemoteBrowser);
  };

  class RemotePackage : public base::RefCounted<RemotePackage> {
   public:
    RemotePackage(
        scoped_refptr<RefCountedAdbThread> adb_thread,
        scoped_refptr<AndroidDevice> device,
        const std::string& display_name,
        const std::string& package_name,
        const std::string& launch_activity);

    scoped_refptr<AndroidDevice> device() { return device_; }
    std::string display_name() { return display_name_; }
    std::string package_name() { return package_name_; }

    void Launch();

   private:
    friend class base::RefCounted<RemotePackage>;
    virtual ~RemotePackage();

    scoped_refptr<RefCountedAdbThread> adb_thread_;
    scoped_refptr<AndroidDevice> device_;
    const std::string display_name_;
    const std::string package_name_;
    const std::string launch_activity_;

    DISALLOW_COPY_AND_ASSIGN(RemotePackage);
  };

  typedef std::vector<scoped_refptr<RemoteBrowser> > RemoteBrowsers;
  typedef std::vector<scoped_refptr<RemotePackage> > RemotePackages;

  class RemoteDevice : public base::RefCounted<RemoteDevice> {
   public:
    explicit RemoteDevice(scoped_refptr<AndroidDevice> device);

    std::string GetSerial();
    std::string GetModel();
    bool IsConnected();
    void AddBrowser(scoped_refptr<RemoteBrowser> browser);
    void AddPackage(scoped_refptr<RemotePackage> package);

    scoped_refptr<AndroidDevice> device() { return device_; }
    RemoteBrowsers& browsers() { return browsers_; }
    RemotePackages& packages() { return packages_; }
    gfx::Size screen_size() { return screen_size_; }
    void set_screen_size(const gfx::Size& size) { screen_size_ = size; }

   private:
    friend class base::RefCounted<RemoteDevice>;
    virtual ~RemoteDevice();

    scoped_refptr<AndroidDevice> device_;
    RemoteBrowsers browsers_;
    RemotePackages packages_;
    gfx::Size screen_size_;

    DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
  };

  typedef std::vector<scoped_refptr<RemoteDevice> > RemoteDevices;

  typedef std::vector<scoped_refptr<AndroidDevice> > AndroidDevices;
  typedef base::Callback<void(const AndroidDevices&)> AndroidDevicesCallback;

  class Listener {
   public:
    virtual void RemoteDevicesChanged(RemoteDevices* devices) = 0;
   protected:
    virtual ~Listener() {}
  };

  DevToolsAdbBridge();
  void AddListener(Listener* listener);
  void RemoveListener(Listener* listener);

  void set_device_providers(DeviceProviders device_providers) {
    device_providers_ = device_providers;
  }
  static bool HasDevToolsWindow(const std::string& agent_id);

 private:
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<DevToolsAdbBridge>;

  virtual ~DevToolsAdbBridge();

  void RequestRemoteDevices();
  void ReceivedRemoteDevices(RemoteDevices* devices);

  scoped_refptr<RefCountedAdbThread> adb_thread_;
  bool has_message_loop_;
  typedef std::vector<Listener*> Listeners;
  Listeners listeners_;
  DeviceProviders device_providers_;
  DISALLOW_COPY_AND_ASSIGN(DevToolsAdbBridge);
};

#endif  // CHROME_BROWSER_DEVTOOLS_DEVTOOLS_ADB_BRIDGE_H_
