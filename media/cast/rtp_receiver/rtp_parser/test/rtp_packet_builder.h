// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test helper class that builds rtp packets.

#ifndef MEDIA_CAST_RTP_RECEIVER_RTP_PARSER_TEST_RTP_PACKET_BUILDER_H_
#define MEDIA_CAST_RTP_RECEIVER_RTP_PARSER_TEST_RTP_PACKET_BUILDER_H_


#include "media/cast/rtp_common/rtp_defines.h"

namespace media {
namespace cast {

class RtpPacketBuilder {
 public:
  RtpPacketBuilder();
  void SetKeyFrame(bool is_key);
  void SetFrameId(uint8 frame_id);
  void SetPacketId(uint16 packet_id);
  void SetMaxPacketId(uint16 max_packet_id);
  void SetReferenceFrameId(uint8 reference_frame_id, bool is_set);
  void SetTimestamp(uint32 timestamp);
  void SetSequenceNumber(uint16 sequence_number);
  void SetMarkerBit(bool marker);
  void SetPayloadType(int payload_type);
  void SetSsrc(uint32 ssrc);
  void BuildHeader(uint8* data, uint32 data_length);

 private:
  bool is_key_;
  uint8 frame_id_;
  uint16 packet_id_;
  uint16 max_packet_id_;
  uint8 reference_frame_id_;
  bool is_reference_set_;
  uint32 timestamp_;
  uint16 sequence_number_;
  bool marker_;
  int payload_type_;
  uint32 ssrc_;

  void BuildCastHeader(uint8* data, uint32 data_length);
  void BuildCommonHeader(uint8* data, uint32 data_length);
};

}  // namespace cast
}  // namespace media

#endif  // MEDIA_CAST_RTP_RECEIVER_RTP_PARSER_TEST_RTP_PACKET_BUILDER_H_
