// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/video_receiver/video_receiver.h"

#include <algorithm>
#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "media/cast/cast_defines.h"
#include "media/cast/framer/framer.h"
#include "media/cast/video_receiver/video_decoder.h"

namespace media {
namespace cast {

const int64 kMinSchedulingDelayMs = 1;

static const int64 kMinTimeBetweenOffsetUpdatesMs = 2000;
static const int kTimeOffsetFilter = 8;
static const int64_t kMinProcessIntervalMs = 5;

// Local implementation of RtpData (defined in rtp_rtcp_defines.h).
// Used to pass payload data into the video receiver.
class LocalRtpVideoData : public RtpData {
 public:
  explicit LocalRtpVideoData(base::TickClock* clock,
                             VideoReceiver* video_receiver)
      : clock_(clock),
        video_receiver_(video_receiver),
        time_updated_(false),
        incoming_rtp_timestamp_(0) {
  }
  virtual ~LocalRtpVideoData() {}

  virtual void OnReceivedPayloadData(const uint8* payload_data,
                                     size_t payload_size,
                                     const RtpCastHeader* rtp_header) OVERRIDE {
    base::TimeTicks now = clock_->NowTicks();
    if (time_incoming_packet_.is_null() || now - time_incoming_packet_ >
        base::TimeDelta::FromMilliseconds(kMinTimeBetweenOffsetUpdatesMs)) {
      incoming_rtp_timestamp_ = rtp_header->webrtc.header.timestamp;
      time_incoming_packet_ = now;
      time_updated_ = true;
    }
    video_receiver_->IncomingRtpPacket(payload_data, payload_size, *rtp_header);
  }

  bool GetPacketTimeInformation(base::TimeTicks* time_incoming_packet,
                                uint32* incoming_rtp_timestamp) {
    *time_incoming_packet = time_incoming_packet_;
    *incoming_rtp_timestamp = incoming_rtp_timestamp_;
    bool time_updated = time_updated_;
    time_updated_ = false;
    return time_updated;
  }

 private:
  base::TickClock* clock_;  // Not owned by this class.
  VideoReceiver* video_receiver_;
  bool time_updated_;
  base::TimeTicks time_incoming_packet_;
  uint32 incoming_rtp_timestamp_;
};

// Local implementation of RtpPayloadFeedback (defined in rtp_defines.h)
// Used to convey cast-specific feedback from receiver to sender.
// Callback triggered by the Framer (cast message builder).
class LocalRtpVideoFeedback : public RtpPayloadFeedback {
 public:
  explicit LocalRtpVideoFeedback(VideoReceiver* video_receiver)
      : video_receiver_(video_receiver) {
  }

  virtual void CastFeedback(const RtcpCastMessage& cast_message) OVERRIDE {
    video_receiver_->CastFeedback(cast_message);
  }

 private:
  VideoReceiver* video_receiver_;
};

// Local implementation of RtpReceiverStatistics (defined by rtcp.h).
// Used to pass statistics data from the RTP module to the RTCP module.
class LocalRtpReceiverStatistics : public RtpReceiverStatistics {
 public:
  explicit LocalRtpReceiverStatistics(RtpReceiver* rtp_receiver)
     : rtp_receiver_(rtp_receiver) {
  }

  virtual void GetStatistics(uint8* fraction_lost,
                             uint32* cumulative_lost,  // 24 bits valid.
                             uint32* extended_high_sequence_number,
                             uint32* jitter) OVERRIDE {
    rtp_receiver_->GetStatistics(fraction_lost,
                                 cumulative_lost,
                                 extended_high_sequence_number,
                                 jitter);
  }

 private:
  RtpReceiver* rtp_receiver_;
};

VideoReceiver::VideoReceiver(scoped_refptr<CastEnvironment> cast_environment,
                             const VideoReceiverConfig& video_config,
                             PacedPacketSender* const packet_sender)
      : cast_environment_(cast_environment),
        codec_(video_config.codec),
        incoming_ssrc_(video_config.incoming_ssrc),
        target_delay_delta_(
            base::TimeDelta::FromMilliseconds(video_config.rtp_max_delay_ms)),
        frame_delay_(base::TimeDelta::FromMilliseconds(
            1000 / video_config.max_frame_rate)),
        incoming_payload_callback_(
            new LocalRtpVideoData(cast_environment_->Clock(), this)),
        incoming_payload_feedback_(new LocalRtpVideoFeedback(this)),
        rtp_receiver_(cast_environment_->Clock(), NULL, &video_config,
                      incoming_payload_callback_.get()),
        rtp_video_receiver_statistics_(
            new LocalRtpReceiverStatistics(&rtp_receiver_)),
        weak_factory_(this) {
  int max_unacked_frames = video_config.rtp_max_delay_ms *
      video_config.max_frame_rate / 1000;
  DCHECK(max_unacked_frames) << "Invalid argument";

  framer_.reset(new Framer(cast_environment->Clock(),
                           incoming_payload_feedback_.get(),
                           video_config.incoming_ssrc,
                           video_config.decoder_faster_than_max_frame_rate,
                           max_unacked_frames));
  if (!video_config.use_external_decoder) {
    video_decoder_.reset(new VideoDecoder(video_config, cast_environment));
  }

  rtcp_.reset(
      new Rtcp(cast_environment_->Clock(),
               NULL,
               packet_sender,
               NULL,
               rtp_video_receiver_statistics_.get(),
               video_config.rtcp_mode,
               base::TimeDelta::FromMilliseconds(video_config.rtcp_interval),
               false,
               video_config.feedback_ssrc,
               video_config.rtcp_c_name));

  rtcp_->SetRemoteSSRC(video_config.incoming_ssrc);
  ScheduleNextRtcpReport();
  ScheduleNextCastMessage();
}

VideoReceiver::~VideoReceiver() {}

void VideoReceiver::GetRawVideoFrame(
    const VideoFrameDecodedCallback& callback) {
  GetEncodedVideoFrame(base::Bind(&VideoReceiver::DecodeVideoFrame,
                                  weak_factory_.GetWeakPtr(),
                                  callback));
}

// Called when we have a frame to decode.
void VideoReceiver::DecodeVideoFrame(
    const VideoFrameDecodedCallback& callback,
    scoped_ptr<EncodedVideoFrame> encoded_frame,
    const base::TimeTicks& render_time) {
  // Hand the ownership of the encoded frame to the decode thread.
  cast_environment_->PostTask(CastEnvironment::VIDEO_DECODER, FROM_HERE,
      base::Bind(&VideoReceiver::DecodeVideoFrameThread,
                 weak_factory_.GetWeakPtr(), base::Passed(&encoded_frame),
                 render_time, callback));
}

// Utility function to run the decoder on a designated decoding thread.
void VideoReceiver::DecodeVideoFrameThread(
    scoped_ptr<EncodedVideoFrame> encoded_frame,
    const base::TimeTicks render_time,
    const VideoFrameDecodedCallback& frame_decoded_callback) {
  DCHECK(cast_environment_->CurrentlyOn(CastEnvironment::VIDEO_DECODER));
  DCHECK(video_decoder_);

  if (!(video_decoder_->DecodeVideoFrame(encoded_frame.get(), render_time,
                                        frame_decoded_callback))) {
    // This will happen if we decide to decode but not show a frame.
    cast_environment_->PostTask(CastEnvironment::MAIN, FROM_HERE,
        base::Bind(&VideoReceiver::GetRawVideoFrame,
                   weak_factory_.GetWeakPtr(), frame_decoded_callback));
  }
}

// Called from the main cast thread.
void VideoReceiver::GetEncodedVideoFrame(
    const VideoFrameEncodedCallback& callback) {
  scoped_ptr<EncodedVideoFrame> encoded_frame(new EncodedVideoFrame());
  uint32 rtp_timestamp = 0;
  bool next_frame = false;

  if (!framer_->GetEncodedVideoFrame(encoded_frame.get(), &rtp_timestamp,
                                     &next_frame)) {
    // We have no video frames. Wait for new packet(s).
    queued_encoded_callbacks_.push_back(callback);
    return;
  }
  base::TimeTicks render_time;
  if (PullEncodedVideoFrame(rtp_timestamp, next_frame, &encoded_frame,
                            &render_time)) {
    cast_environment_->PostTask(CastEnvironment::MAIN, FROM_HERE,
        base::Bind(callback, base::Passed(&encoded_frame), render_time));
  } else {
    // We have a video frame; however we are missing packets and we have time
    // to wait for new packet(s).
    queued_encoded_callbacks_.push_back(callback);
  }
}

// Should we pull the encoded video frame from the framer? decided by if this is
// the next frame or we are running out of time and have to pull the following
// frame.
// If the frame it too old to be rendered we set the don't show flag in the
// video bitstream where possible.
bool VideoReceiver::PullEncodedVideoFrame(uint32 rtp_timestamp,
    bool next_frame, scoped_ptr<EncodedVideoFrame>* encoded_frame,
    base::TimeTicks* render_time) {
  base::TimeTicks now = cast_environment_->Clock()->NowTicks();
  *render_time = GetRenderTime(now, rtp_timestamp);

  // Minimum time before a frame is due to be rendered before we pull it for
  // decode.
  base::TimeDelta min_wait_delta = frame_delay_;
  base::TimeDelta time_until_render = *render_time - now;
  if (!next_frame && (time_until_render > min_wait_delta)) {
    // Example:
    // We have decoded frame 1 and we have received the complete frame 3, but
    // not frame 2. If we still have time before frame 3 should be rendered we
    // will wait for 2 to arrive, however if 2 never show up this timer will hit
    // and we will pull out frame 3 for decoding and rendering.
    base::TimeDelta time_until_release = time_until_render - min_wait_delta;
    cast_environment_->PostDelayedTask(CastEnvironment::MAIN, FROM_HERE,
        base::Bind(&VideoReceiver::PlayoutTimeout, weak_factory_.GetWeakPtr()),
        time_until_release);
    VLOG(0) << "Wait before releasing frame "
            << static_cast<int>((*encoded_frame)->frame_id)
            << " time " << time_until_release.InMilliseconds();
    return false;
  }

  base::TimeDelta dont_show_timeout_delta =
      base::TimeDelta::FromMilliseconds(-kDontShowTimeoutMs);
  if (codec_ == kVp8 && time_until_render < dont_show_timeout_delta) {
    (*encoded_frame)->data[0] &= 0xef;
    VLOG(0) << "Don't show frame "
            << static_cast<int>((*encoded_frame)->frame_id)
            << " time_until_render:" << time_until_render.InMilliseconds();
  } else {
    VLOG(1) << "Show frame "
            << static_cast<int>((*encoded_frame)->frame_id)
            << " time_until_render:" << time_until_render.InMilliseconds();
  }
  // We have a copy of the frame, release this one.
  framer_->ReleaseFrame((*encoded_frame)->frame_id);
  (*encoded_frame)->codec = codec_;
  return true;
}

void VideoReceiver::PlayoutTimeout() {
  if (queued_encoded_callbacks_.empty()) return;

  uint32 rtp_timestamp = 0;
  bool next_frame = false;
  scoped_ptr<EncodedVideoFrame> encoded_frame(new EncodedVideoFrame());

  if (!framer_->GetEncodedVideoFrame(encoded_frame.get(), &rtp_timestamp,
                                     &next_frame)) {
    // We have no video frames. Wait for new packet(s).
    // Since the application can post multiple VideoFrameEncodedCallback and
    // we only check the next frame to play out we might have multiple timeout
    // events firing after each other; however this should be a rare event.
    VLOG(1) << "Failed to retrieved a complete frame at this point in time";
    return;
  }
  VLOG(1) << "PlayoutTimeout retrieved frame "
          << static_cast<int>(encoded_frame->frame_id);

  base::TimeTicks render_time;
  if (PullEncodedVideoFrame(rtp_timestamp, next_frame, &encoded_frame,
                            &render_time)) {
    if (!queued_encoded_callbacks_.empty()) {
      VideoFrameEncodedCallback callback = queued_encoded_callbacks_.front();
      queued_encoded_callbacks_.pop_front();
      cast_environment_->PostTask(CastEnvironment::MAIN, FROM_HERE,
          base::Bind(callback, base::Passed(&encoded_frame), render_time));
    }
  } else {
    // We have a video frame; however we are missing packets and we have time
    // to wait for new packet(s).
  }
}

base::TimeTicks VideoReceiver::GetRenderTime(base::TimeTicks now,
                                             uint32 rtp_timestamp) {
  // Senders time in ms when this frame was captured.
  // Note: the senders clock and our local clock might not be synced.
  base::TimeTicks rtp_timestamp_in_ticks;
  base::TimeTicks time_incoming_packet;
  uint32 incoming_rtp_timestamp;

  if (time_offset_.InMilliseconds() == 0) {
    incoming_payload_callback_->GetPacketTimeInformation(
        &time_incoming_packet, &incoming_rtp_timestamp);

    if (!rtcp_->RtpTimestampInSenderTime(kVideoFrequency,
                                         incoming_rtp_timestamp,
                                         &rtp_timestamp_in_ticks)) {
      // We have not received any RTCP to sync the stream play it out as soon as
      // possible.
      return now;
    }
    time_offset_ = time_incoming_packet - rtp_timestamp_in_ticks;
  } else if (incoming_payload_callback_->GetPacketTimeInformation(
      &time_incoming_packet, &incoming_rtp_timestamp)) {
    if (rtcp_->RtpTimestampInSenderTime(kVideoFrequency,
                                        incoming_rtp_timestamp,
                                        &rtp_timestamp_in_ticks)) {
      // Time to update the time_offset.
      base::TimeDelta time_offset =
          time_incoming_packet - rtp_timestamp_in_ticks;
      time_offset_ = ((kTimeOffsetFilter - 1) * time_offset_ + time_offset)
          / kTimeOffsetFilter;
    }
  }
  if (!rtcp_->RtpTimestampInSenderTime(kVideoFrequency,
                                       rtp_timestamp,
                                       &rtp_timestamp_in_ticks)) {
    // This can fail if we have not received any RTCP packets in a long time.
    return now;
  }
  return (rtp_timestamp_in_ticks + time_offset_ + target_delay_delta_);
}

void VideoReceiver::IncomingPacket(const uint8* packet, size_t length,
                                   const base::Closure callback) {
  if (Rtcp::IsRtcpPacket(packet, length)) {
    rtcp_->IncomingRtcpPacket(packet, length);
  } else {
    rtp_receiver_.ReceivedPacket(packet, length);
  }
  cast_environment_->PostTask(CastEnvironment::MAIN, FROM_HERE, callback);
}

void VideoReceiver::IncomingRtpPacket(const uint8* payload_data,
                                      size_t payload_size,
                                      const RtpCastHeader& rtp_header) {
  bool complete = framer_->InsertPacket(payload_data, payload_size, rtp_header);

  if (!complete) return;  // Video frame not complete; wait for more packets.
  if (queued_encoded_callbacks_.empty()) return;  // No pending callback.

  VideoFrameEncodedCallback callback = queued_encoded_callbacks_.front();
  queued_encoded_callbacks_.pop_front();
  cast_environment_->PostTask(CastEnvironment::MAIN, FROM_HERE,
      base::Bind(&VideoReceiver::GetEncodedVideoFrame,
          weak_factory_.GetWeakPtr(), callback));
}

// Send a cast feedback message. Actual message created in the framer (cast
// message builder).
void VideoReceiver::CastFeedback(const RtcpCastMessage& cast_message) {
  rtcp_->SendRtcpCast(cast_message);
  time_last_sent_cast_message_= cast_environment_->Clock()->NowTicks();
}

// Cast messages should be sent within a maximum interval. Schedule a call
// if not triggered elsewhere, e.g. by the cast message_builder.
void VideoReceiver::ScheduleNextCastMessage() {
  base::TimeTicks send_time;
  framer_->TimeToSendNextCastMessage(&send_time);

  base::TimeDelta time_to_send = send_time -
      cast_environment_->Clock()->NowTicks();
  time_to_send = std::max(time_to_send,
      base::TimeDelta::FromMilliseconds(kMinSchedulingDelayMs));
  cast_environment_->PostDelayedTask(CastEnvironment::MAIN, FROM_HERE,
      base::Bind(&VideoReceiver::SendNextCastMessage,
                 weak_factory_.GetWeakPtr()), time_to_send);
}

void VideoReceiver::SendNextCastMessage() {
  framer_->SendCastMessage();  // Will only send a message if it is time.
  ScheduleNextCastMessage();
}

// Schedule the next RTCP report to be sent back to the sender.
void VideoReceiver::ScheduleNextRtcpReport() {
  base::TimeDelta time_to_next = rtcp_->TimeToSendNextRtcpReport() -
      cast_environment_->Clock()->NowTicks();

  time_to_next = std::max(time_to_next,
      base::TimeDelta::FromMilliseconds(kMinSchedulingDelayMs));

  cast_environment_->PostDelayedTask(CastEnvironment::MAIN, FROM_HERE,
      base::Bind(&VideoReceiver::SendNextRtcpReport,
                weak_factory_.GetWeakPtr()), time_to_next);
}

void VideoReceiver::SendNextRtcpReport() {
  rtcp_->SendRtcpReport(incoming_ssrc_);
  ScheduleNextRtcpReport();
}

}  // namespace cast
}  // namespace media
