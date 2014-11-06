// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SYSTEM_MESSAGE_PIPE_H_
#define MOJO_SYSTEM_MESSAGE_PIPE_H_

#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "mojo/public/system/core.h"
#include "mojo/public/system/system_export.h"
#include "mojo/system/message_in_transit.h"

namespace mojo {
namespace system {

class Channel;
class Dispatcher;
class MessagePipeEndpoint;
class Waiter;

// |MessagePipe| is the secondary object implementing a message pipe (see the
// explanatory comment in core_impl.cc). It is typically owned by the
// dispatcher(s) corresponding to the local endpoints. This class is
// thread-safe.
class MOJO_SYSTEM_EXPORT MessagePipe :
    public base::RefCountedThreadSafe<MessagePipe> {
 public:
  MessagePipe(scoped_ptr<MessagePipeEndpoint> endpoint_0,
              scoped_ptr<MessagePipeEndpoint> endpoint_1);

  // Convenience constructor that constructs a |MessagePipe| with two new
  // |LocalMessagePipeEndpoint|s.
  MessagePipe();

  // Gets the other port number (i.e., 0 -> 1, 1 -> 0).
  static unsigned GetPeerPort(unsigned port);

  // These are called by the dispatcher to implement its methods of
  // corresponding names. In all cases, the port |port| must be open.
  void CancelAllWaiters(unsigned port);
  void Close(unsigned port);
  // Unlike |MessagePipeDispatcher::WriteMessage()|, this does not validate its
  // arguments.
  MojoResult WriteMessage(unsigned port,
                          const void* bytes, uint32_t num_bytes,
                          const std::vector<Dispatcher*>* dispatchers,
                          MojoWriteMessageFlags flags);
  // Unlike |MessagePipeDispatcher::ReadMessage()|, this does not validate its
  // arguments.
  MojoResult ReadMessage(unsigned port,
                         void* bytes, uint32_t* num_bytes,
                         uint32_t max_num_dispatchers,
                         std::vector<scoped_refptr<Dispatcher> >* dispatchers,
                         MojoReadMessageFlags flags);
  MojoResult AddWaiter(unsigned port,
                       Waiter* waiter,
                       MojoWaitFlags flags,
                       MojoResult wake_result);
  void RemoveWaiter(unsigned port, Waiter* waiter);

  // This is used internally by |WriteMessage()| and by |Channel| to enqueue
  // messages (typically to a |LocalMessagePipeEndpoint|). Unlike
  // |WriteMessage()|, |port| is the *destination* port. Takes ownership of
  // |message|.
  MojoResult EnqueueMessage(unsigned port, MessageInTransit* message);

  // These are used by |Channel|.
  void Attach(unsigned port,
              scoped_refptr<Channel> channel,
              MessageInTransit::EndpointId local_id);
  void Run(unsigned port, MessageInTransit::EndpointId remote_id);

 private:
  friend class base::RefCountedThreadSafe<MessagePipe>;
  virtual ~MessagePipe();

  // Used by |EnqueueMessage()| to handle control messages that are actually
  // meant for us.
  MojoResult HandleControlMessage(unsigned port, MessageInTransit* message);

  base::Lock lock_;  // Protects the following members.
  scoped_ptr<MessagePipeEndpoint> endpoints_[2];

  DISALLOW_COPY_AND_ASSIGN(MessagePipe);
};

}  // namespace system
}  // namespace mojo

#endif  // MOJO_SYSTEM_MESSAGE_PIPE_H_
