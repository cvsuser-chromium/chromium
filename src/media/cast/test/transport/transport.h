// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAST_TEST_TRANSPORT_TRANSPORT_H_
#define MEDIA_CAST_TEST_TRANSPORT_TRANSPORT_H_

#include "base/memory/scoped_ptr.h"
#include "media/cast/cast_config.h"
#include "media/cast/cast_environment.h"
#include "net/udp/udp_server_socket.h"
#include "net/udp/udp_socket.h"


namespace media {
namespace cast {
namespace test {

class LocalUdpTransportData;
class LocalPacketSender;

// Helper class for Cast test applications.
class Transport {
 public:
  Transport(scoped_refptr<CastEnvironment> cast_environment);
  ~Transport();

  // Specifies the ports and IP address to receive packets on.
  // Will start listening immediately.
  void SetLocalReceiver(PacketReceiver* packet_receiver,
                        std::string ip_address,
                        int port);

  // Specifies the destination port and IP address.
  void SetSendDestination(std::string ip_address, int port);

  PacketSender* packet_sender();

  void SetSendSidePacketLoss(int percentage);

  void StopReceiving();

 private:
  scoped_ptr<net::DatagramServerSocket> udp_socket_;
  scoped_ptr<LocalPacketSender> packet_sender_;
  scoped_ptr<LocalUdpTransportData> local_udp_transport_data_;

  DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace test
}  // namespace cast
}  // namespace media

#endif  // MEDIA_CAST_TEST_TRANSPORT_TRANSPORT_H_
