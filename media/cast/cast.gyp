# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'include_tests%': 1,
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'cast_config',
      'type': 'static_library',
      'include_dirs': [
        '<(DEPTH)/',
      ],
      'sources': [
        'cast_config.cc',
        'cast_config.h',
        'cast_environment.cc',
        'cast_environment.h',
      ], # source
    },
  ],  # targets,
  'conditions': [
    ['include_tests==1', {
      'targets': [
        {
          'target_name': 'cast_unittests',
          'type': '<(gtest_target_type)',
          'dependencies': [
            'cast_config',
            'cast_receiver.gyp:cast_receiver',
            'cast_sender.gyp:cast_sender',
            'logging/logging.gyp:cast_logging',
            'test/utility/utility.gyp:cast_test_utility',
            '<(DEPTH)/base/base.gyp:run_all_unittests',
            '<(DEPTH)/net/net.gyp:net',
            '<(DEPTH)/testing/gmock.gyp:gmock',
            '<(DEPTH)/testing/gtest.gyp:gtest',
          ],
          'include_dirs': [
            '<(DEPTH)/',
            '<(DEPTH)/third_party/',
            '<(DEPTH)/third_party/webrtc/',
          ],
          'sources': [
            'audio_receiver/audio_decoder_unittest.cc',
            'audio_receiver/audio_receiver_unittest.cc',
            'audio_sender/audio_encoder_unittest.cc',
            'audio_sender/audio_sender_unittest.cc',
            'congestion_control/congestion_control_unittest.cc',
            'framer/cast_message_builder_unittest.cc',
            'framer/frame_buffer_unittest.cc',
            'framer/framer_unittest.cc',
            'pacing/mock_paced_packet_sender.cc',
            'pacing/mock_paced_packet_sender.h',
            'pacing/paced_sender_unittest.cc',
            'rtcp/mock_rtcp_receiver_feedback.cc',
            'rtcp/mock_rtcp_receiver_feedback.h',
            'rtcp/mock_rtcp_sender_feedback.cc',
            'rtcp/mock_rtcp_sender_feedback.h',
            'rtcp/rtcp_receiver_unittest.cc',
            'rtcp/rtcp_sender_unittest.cc',
            'rtcp/rtcp_unittest.cc',
            'rtp_common/mock_rtp_payload_feedback.cc',
            'rtp_common/mock_rtp_payload_feedback.h',
            'rtp_receiver/receiver_stats_unittest.cc',
            'rtp_receiver/rtp_parser/test/rtp_packet_builder.cc',
            'rtp_receiver/rtp_parser/rtp_parser_unittest.cc',
            'rtp_sender/packet_storage/packet_storage_unittest.cc',
            'rtp_sender/rtp_packetizer/rtp_packetizer_unittest.cc',
            'rtp_sender/rtp_packetizer/test/rtp_header_parser.cc',
            'rtp_sender/rtp_packetizer/test/rtp_header_parser.h',
            'test/encode_decode_test.cc',
            'test/end2end_unittest.cc',
            'video_receiver/video_decoder_unittest.cc',
            'video_receiver/video_receiver_unittest.cc',
            'video_sender/mock_video_encoder_controller.cc',
            'video_sender/mock_video_encoder_controller.h',
            'pacing/paced_sender_unittest.cc',
            'rtcp/rtcp_receiver_unittest.cc',
            'rtcp/rtcp_sender_unittest.cc',
            'rtcp/rtcp_unittest.cc',
            'video_sender/video_encoder_unittest.cc',
            'video_sender/video_sender_unittest.cc',
          ], # source
        },
        {
          'target_name': 'cast_sender_app',
          'type': 'executable',
          'include_dirs': [
            '<(DEPTH)/',
          ],
          'dependencies': [
            '<(DEPTH)/net/net.gyp:net_test_support',
            'cast_config',
            '<(DEPTH)/media/cast/cast_sender.gyp:*',
            '<(DEPTH)/testing/gtest.gyp:gtest',
            '<(DEPTH)/third_party/opus/opus.gyp:opus',
            '<(DEPTH)/media/cast/test/transport/transport.gyp:cast_transport',
            '<(DEPTH)/media/cast/test/utility/utility.gyp:cast_test_utility',
          ],
          'sources': [
            '<(DEPTH)/media/cast/test/sender.cc',
          ],
        },
        {
          'target_name': 'cast_receiver_app',
          'type': 'executable',
          'include_dirs': [
            '<(DEPTH)/',
          ],
          'dependencies': [
            '<(DEPTH)/net/net.gyp:net_test_support',
            'cast_config',
            '<(DEPTH)/media/cast/cast_receiver.gyp:*',
            '<(DEPTH)/testing/gtest.gyp:gtest',
            '<(DEPTH)/media/cast/test/transport/transport.gyp:cast_transport',
            '<(DEPTH)/media/cast/test/utility/utility.gyp:cast_test_utility',
            '<(DEPTH)/third_party/libyuv/libyuv.gyp:libyuv',
          ],
          'sources': [
            '<(DEPTH)/media/cast/test/receiver.cc',
          ],
          'conditions': [
            ['OS == "linux"', {
              'sources': [
                '<(DEPTH)/media/cast/test/linux_output_window.cc',
                '<(DEPTH)/media/cast/test/linux_output_window.h',
              ],
              'libraries': [
                '-lXext',
                '-lX11',
             ],
          }],
          ],
        },
      ],  # targets
    }], # include_tests
  ],
}
