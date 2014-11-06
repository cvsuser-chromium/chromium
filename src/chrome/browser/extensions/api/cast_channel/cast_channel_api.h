// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_CAST_CHANNEL_CAST_CHANNEL_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_CAST_CHANNEL_CAST_CHANNEL_API_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/extensions/api/api_function.h"
#include "chrome/browser/extensions/api/api_resource_manager.h"
#include "chrome/browser/extensions/api/cast_channel/cast_socket.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/extensions/api/cast_channel.h"

class GURL;
class Profile;
class CastChannelAPITest;

namespace extensions {

namespace cast_channel = api::cast_channel;

class CastChannelAPI : public ProfileKeyedAPI,
                       public cast_channel::CastSocket::Delegate {

 public:
  explicit CastChannelAPI(Profile* profile);

  static CastChannelAPI* Get(Profile* profile);

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<CastChannelAPI>* GetFactoryInstance();

  // Returns a new CastSocket that connects to |url| and is to be owned by
  // |extension_id|.
  scoped_ptr<cast_channel::CastSocket> CreateCastSocket(
      const std::string& extension_id, const GURL& gurl);

  // Sets the CastSocket instance to be returned by CreateCastSocket for
  // testing.
  void SetSocketForTest(scoped_ptr<cast_channel::CastSocket> socket_for_test);

 private:
  friend class ProfileKeyedAPIFactory<CastChannelAPI>;
  friend class ::CastChannelAPITest;

  virtual ~CastChannelAPI();

  // CastSocket::Delegate.  Called on IO thread.
  virtual void OnError(const cast_channel::CastSocket* socket,
                       cast_channel::ChannelError error) OVERRIDE;
  virtual void OnMessage(const cast_channel::CastSocket* socket,
                         const cast_channel::MessageInfo& message) OVERRIDE;

  // ProfileKeyedAPI implementation.
  static const char* service_name() {
    return "CastChannelAPI";
  }

  Profile* const profile_;
  scoped_ptr<cast_channel::CastSocket> socket_for_test_;

  DISALLOW_COPY_AND_ASSIGN(CastChannelAPI);
};

class CastChannelAsyncApiFunction : public AsyncApiFunction {
 public:
  CastChannelAsyncApiFunction();

 protected:
  virtual ~CastChannelAsyncApiFunction();

  // AsyncApiFunction:
  virtual bool PrePrepare() OVERRIDE;
  virtual bool Respond() OVERRIDE;

  // Returns the socket corresponding to |channel_id| if one exists.  Otherwise,
  // sets the function result with CHANNEL_ERROR_INVALID_CHANNEL_ID, completes
  // the function, and returns null.
  cast_channel::CastSocket* GetSocketOrCompleteWithError(int channel_id);

  // Adds |socket| to |manager_| and returns the new channel_id.  |manager_|
  // assumes ownership of |socket|.
  int AddSocket(cast_channel::CastSocket* socket);

  // Removes the CastSocket corresponding to |channel_id| from the resource
  // manager.
  void RemoveSocket(int channel_id);

  // Sets the function result to a ChannelInfo obtained from the state of the
  // CastSocket corresponding to |channel_id|.
  void SetResultFromSocket(int channel_id);

  // Sets the function result to a ChannelInfo with |error|.
  void SetResultFromError(cast_channel::ChannelError error);

  // Returns the socket corresponding to |channel_id| if one exists, or null
  // otherwise.
  cast_channel::CastSocket* GetSocket(int channel_id);

 private:
  // Sets the function result from |channel_info|.
  void SetResultFromChannelInfo(
      const cast_channel::ChannelInfo& channel_info);

  // The API resource manager for CastSockets.
  ApiResourceManager<cast_channel::CastSocket>* manager_;

  // The result of the function.
  cast_channel::ChannelError error_;
};

class CastChannelOpenFunction : public CastChannelAsyncApiFunction {
 public:
  CastChannelOpenFunction();

 protected:
  virtual ~CastChannelOpenFunction();

  // AsyncApiFunction:
  virtual bool PrePrepare() OVERRIDE;
  virtual bool Prepare() OVERRIDE;
  virtual void AsyncWorkStart() OVERRIDE;

 private:
  DECLARE_EXTENSION_FUNCTION("cast.channel.open", CAST_CHANNEL_OPEN)

  void OnOpen(int result);

  scoped_ptr<cast_channel::Open::Params> params_;
  // The id of the newly opened socket.
  int new_channel_id_;
  CastChannelAPI* api_;

  DISALLOW_COPY_AND_ASSIGN(CastChannelOpenFunction);
};

class CastChannelSendFunction : public CastChannelAsyncApiFunction {
 public:
  CastChannelSendFunction();

 protected:
  virtual ~CastChannelSendFunction();

  // AsyncApiFunction:
  virtual bool Prepare() OVERRIDE;
  virtual void AsyncWorkStart() OVERRIDE;

 private:
  DECLARE_EXTENSION_FUNCTION("cast.channel.send", CAST_CHANNEL_SEND)

  void OnSend(int result);

  scoped_ptr<cast_channel::Send::Params> params_;

  DISALLOW_COPY_AND_ASSIGN(CastChannelSendFunction);
};

class CastChannelCloseFunction : public CastChannelAsyncApiFunction {
 public:
  CastChannelCloseFunction();

 protected:
  virtual ~CastChannelCloseFunction();

  // AsyncApiFunction:
  virtual bool Prepare() OVERRIDE;
  virtual void AsyncWorkStart() OVERRIDE;

 private:
  DECLARE_EXTENSION_FUNCTION("cast.channel.close", CAST_CHANNEL_CLOSE)

  void OnClose(int result);

  scoped_ptr<cast_channel::Close::Params> params_;

  DISALLOW_COPY_AND_ASSIGN(CastChannelCloseFunction);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_CAST_CHANNEL_CAST_CHANNEL_API_H_
