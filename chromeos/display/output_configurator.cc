// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/display/output_configurator.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XInput2.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "base/time/time.h"
#include "chromeos/display/output_util.h"
#include "chromeos/display/real_output_configurator_delegate.h"

namespace chromeos {

namespace {

// The delay to perform configuration after RRNotify.  See the comment
// in |Dispatch()|.
const int64 kConfigureDelayMs = 500;

// Returns a string describing |state|.
std::string DisplayPowerStateToString(DisplayPowerState state) {
  switch (state) {
    case DISPLAY_POWER_ALL_ON:
      return "ALL_ON";
    case DISPLAY_POWER_ALL_OFF:
      return "ALL_OFF";
    case DISPLAY_POWER_INTERNAL_OFF_EXTERNAL_ON:
      return "INTERNAL_OFF_EXTERNAL_ON";
    case DISPLAY_POWER_INTERNAL_ON_EXTERNAL_OFF:
      return "INTERNAL_ON_EXTERNAL_OFF";
    default:
      return "unknown (" + base::IntToString(state) + ")";
  }
}

// Returns a string describing |state|.
std::string OutputStateToString(OutputState state) {
  switch (state) {
    case STATE_INVALID:
      return "INVALID";
    case STATE_HEADLESS:
      return "HEADLESS";
    case STATE_SINGLE:
      return "SINGLE";
    case STATE_DUAL_MIRROR:
      return "DUAL_MIRROR";
    case STATE_DUAL_EXTENDED:
      return "DUAL_EXTENDED";
  }
  NOTREACHED() << "Unknown state " << state;
  return "INVALID";
}

// Returns a string representation of OutputSnapshot.
std::string OutputSnapshotToString(
    const OutputConfigurator::OutputSnapshot* output) {
  return base::StringPrintf(
      "[type=%d, output=%ld, crtc=%ld, mode=%ld, dim=%dx%d]",
      output->type,
      output->output,
      output->crtc,
      output->current_mode,
      static_cast<int>(output->width_mm),
      static_cast<int>(output->height_mm));
}

// Returns a string representation of ModeInfo.
std::string ModeInfoToString(const OutputConfigurator::ModeInfo* mode) {
  return base::StringPrintf("[%dx%d %srate=%f]",
                            mode->width,
                            mode->height,
                            mode->interlaced ? "interlaced " : "",
                            mode->refresh_rate);

}

// Returns the number of outputs in |outputs| that should be turned on, per
// |state|.  If |output_power| is non-NULL, it is updated to contain the
// on/off state of each corresponding entry in |outputs|.
int GetOutputPower(
    const std::vector<OutputConfigurator::OutputSnapshot>& outputs,
    DisplayPowerState state,
    std::vector<bool>* output_power) {
  int num_on_outputs = 0;
  if (output_power)
    output_power->resize(outputs.size());

  for (size_t i = 0; i < outputs.size(); ++i) {
    bool internal = outputs[i].is_internal;
    bool on = state == DISPLAY_POWER_ALL_ON ||
        (state == DISPLAY_POWER_INTERNAL_OFF_EXTERNAL_ON && !internal) ||
        (state == DISPLAY_POWER_INTERNAL_ON_EXTERNAL_OFF && internal);
    if (output_power)
      (*output_power)[i] = on;
    if (on)
      num_on_outputs++;
  }
  return num_on_outputs;
}

// Determine if there is an "internal" output and how many outputs are
// connected.
bool IsProjecting(
    const std::vector<OutputConfigurator::OutputSnapshot>& outputs) {
  bool has_internal_output = false;
  int connected_output_count = outputs.size();
  for (size_t i = 0; i < outputs.size(); ++i)
    has_internal_output |= outputs[i].is_internal;

  // "Projecting" is defined as having more than 1 output connected while at
  // least one of them is an internal output.
  return has_internal_output && (connected_output_count > 1);
}

}  // namespace

OutputConfigurator::ModeInfo::ModeInfo()
    : width(0),
      height(0),
      interlaced(false),
      refresh_rate(0.0) {}

OutputConfigurator::ModeInfo::ModeInfo(int width,
                                       int height,
                                       bool interlaced,
                                       float refresh_rate)
    : width(width),
      height(height),
      interlaced(interlaced),
      refresh_rate(refresh_rate) {}

OutputConfigurator::CoordinateTransformation::CoordinateTransformation()
    : x_scale(1.0),
      x_offset(0.0),
      y_scale(1.0),
      y_offset(0.0) {}

OutputConfigurator::OutputSnapshot::OutputSnapshot()
    : output(None),
      crtc(None),
      current_mode(None),
      native_mode(None),
      mirror_mode(None),
      selected_mode(None),
      x(0),
      y(0),
      width_mm(0),
      height_mm(0),
      is_internal(false),
      is_aspect_preserving_scaling(false),
      type(OUTPUT_TYPE_UNKNOWN),
      touch_device_id(0),
      display_id(0),
      has_display_id(false),
      index(0) {}

OutputConfigurator::OutputSnapshot::~OutputSnapshot() {}

void OutputConfigurator::TestApi::SendScreenChangeEvent() {
  XRRScreenChangeNotifyEvent event = {0};
  event.type = xrandr_event_base_ + RRScreenChangeNotify;
  configurator_->Dispatch(reinterpret_cast<const base::NativeEvent>(&event));
}

void OutputConfigurator::TestApi::SendOutputChangeEvent(RROutput output,
                                                        RRCrtc crtc,
                                                        RRMode mode,
                                                        bool connected) {
  XRROutputChangeNotifyEvent event = {0};
  event.type = xrandr_event_base_ + RRNotify;
  event.subtype = RRNotify_OutputChange;
  event.output = output;
  event.crtc = crtc;
  event.mode = mode;
  event.connection = connected ? RR_Connected : RR_Disconnected;
  configurator_->Dispatch(reinterpret_cast<const base::NativeEvent>(&event));
}

bool OutputConfigurator::TestApi::TriggerConfigureTimeout() {
  if (configurator_->configure_timer_.get() &&
      configurator_->configure_timer_->IsRunning()) {
    configurator_->configure_timer_.reset();
    configurator_->ConfigureOutputs();
    return true;
  } else {
    return false;
  }
}

// static
const OutputConfigurator::ModeInfo* OutputConfigurator::GetModeInfo(
    const OutputSnapshot& output,
    RRMode mode) {
  if (mode == None)
    return NULL;

  ModeInfoMap::const_iterator it = output.mode_infos.find(mode);
  if (it == output.mode_infos.end()) {
    LOG(WARNING) << "Unable to find info about mode " << mode
                 << " for output " << output.output;
    return NULL;
  }
  return &it->second;
}

// static
RRMode OutputConfigurator::FindOutputModeMatchingSize(
    const OutputSnapshot& output,
    int width,
    int height) {
  RRMode found = None;
  float best_rate = 0;
  bool non_interlaced_found = false;
  for (ModeInfoMap::const_iterator it = output.mode_infos.begin();
       it != output.mode_infos.end(); ++it) {
    RRMode mode = it->first;
    const ModeInfo& info = it->second;

    if (info.width == width && info.height == height) {
      if (info.interlaced) {
        if (non_interlaced_found)
          continue;
      } else {
        // Reset the best rate if the non interlaced is
        // found the first time.
        if (!non_interlaced_found)
          best_rate = info.refresh_rate;
        non_interlaced_found = true;
      }
      if (info.refresh_rate < best_rate)
        continue;

      found = mode;
      best_rate = info.refresh_rate;
    }
  }
  return found;
}

OutputConfigurator::OutputConfigurator()
    : state_controller_(NULL),
      mirroring_controller_(NULL),
      is_panel_fitting_enabled_(false),
      configure_display_(base::SysInfo::IsRunningOnChromeOS()),
      xrandr_event_base_(0),
      output_state_(STATE_INVALID),
      power_state_(DISPLAY_POWER_ALL_ON),
      next_output_protection_client_id_(1) {
}

OutputConfigurator::~OutputConfigurator() {}

void OutputConfigurator::SetDelegateForTesting(scoped_ptr<Delegate> delegate) {
  delegate_ = delegate.Pass();
  configure_display_ = true;
}

void OutputConfigurator::SetInitialDisplayPower(DisplayPowerState power_state) {
  DCHECK_EQ(output_state_, STATE_INVALID);
  power_state_ = power_state;
}

void OutputConfigurator::Init(bool is_panel_fitting_enabled) {
  is_panel_fitting_enabled_ = is_panel_fitting_enabled;
  if (!configure_display_)
    return;

  if (!delegate_)
    delegate_.reset(new RealOutputConfiguratorDelegate());
}

void OutputConfigurator::Start(uint32 background_color_argb) {
  if (!configure_display_)
    return;

  delegate_->GrabServer();
  delegate_->InitXRandRExtension(&xrandr_event_base_);

  UpdateCachedOutputs();
  if (cached_outputs_.size() > 1 && background_color_argb)
    delegate_->SetBackgroundColor(background_color_argb);
  const OutputState new_state = ChooseOutputState(power_state_);
  const bool success = EnterStateOrFallBackToSoftwareMirroring(
      new_state, power_state_);

  // Force the DPMS on chrome startup as the driver doesn't always detect
  // that all displays are on when signing out.
  delegate_->ForceDPMSOn();
  delegate_->UngrabServer();
  delegate_->SendProjectingStateToPowerManager(IsProjecting(cached_outputs_));
  NotifyObservers(success, new_state);
}

bool OutputConfigurator::ApplyProtections(const DisplayProtections& requests) {
  for (std::vector<OutputSnapshot>::const_iterator it = cached_outputs_.begin();
       it != cached_outputs_.end(); ++it) {
    RROutput this_id = it->output;
    uint32_t all_desired = 0;
    DisplayProtections::const_iterator request_it = requests.find(
        it->display_id);
    if (request_it != requests.end())
      all_desired = request_it->second;
    switch (it->type) {
      case OUTPUT_TYPE_UNKNOWN:
        return false;
      // DisplayPort, DVI, and HDMI all support HDCP.
      case OUTPUT_TYPE_DISPLAYPORT:
      case OUTPUT_TYPE_DVI:
      case OUTPUT_TYPE_HDMI: {
        HDCPState new_desired_state =
            (all_desired & OUTPUT_PROTECTION_METHOD_HDCP) ?
            HDCP_STATE_DESIRED : HDCP_STATE_UNDESIRED;
        if (!delegate_->SetHDCPState(this_id, new_desired_state))
          return false;
        break;
      }
      case OUTPUT_TYPE_INTERNAL:
      case OUTPUT_TYPE_VGA:
      case OUTPUT_TYPE_NETWORK:
        // No protections for these types. Do nothing.
        break;
      case OUTPUT_TYPE_NONE:
        NOTREACHED();
        break;
    }
  }

  return true;
}

OutputConfigurator::OutputProtectionClientId
OutputConfigurator::RegisterOutputProtectionClient() {
  if (!configure_display_)
    return kInvalidClientId;

  return next_output_protection_client_id_++;
}

void OutputConfigurator::UnregisterOutputProtectionClient(
    OutputProtectionClientId client_id) {
  client_protection_requests_.erase(client_id);

  DisplayProtections protections;
  for (ProtectionRequests::const_iterator it =
           client_protection_requests_.begin();
       it != client_protection_requests_.end();
       ++it) {
    for (DisplayProtections::const_iterator it2 = it->second.begin();
       it2 != it->second.end(); ++it2) {
      protections[it2->first] |= it2->second;
    }
  }

  ApplyProtections(protections);
}

bool OutputConfigurator::QueryOutputProtectionStatus(
    OutputProtectionClientId client_id,
    int64 display_id,
    uint32_t* link_mask,
    uint32_t* protection_mask) {
  if (!configure_display_)
    return false;

  uint32_t enabled = 0;
  uint32_t unfulfilled = 0;
  *link_mask = 0;
  for (std::vector<OutputSnapshot>::const_iterator it = cached_outputs_.begin();
       it != cached_outputs_.end(); ++it) {
    RROutput this_id = it->output;
    if (it->display_id != display_id)
      continue;
    *link_mask |= it->type;
    switch (it->type) {
      case OUTPUT_TYPE_UNKNOWN:
        return false;
      // DisplayPort, DVI, and HDMI all support HDCP.
      case OUTPUT_TYPE_DISPLAYPORT:
      case OUTPUT_TYPE_DVI:
      case OUTPUT_TYPE_HDMI: {
        HDCPState state;
        if (!delegate_->GetHDCPState(this_id, &state))
          return false;
        if (state == HDCP_STATE_ENABLED)
          enabled |= OUTPUT_PROTECTION_METHOD_HDCP;
        else
          unfulfilled |= OUTPUT_PROTECTION_METHOD_HDCP;
        break;
      }
      case OUTPUT_TYPE_INTERNAL:
      case OUTPUT_TYPE_VGA:
      case OUTPUT_TYPE_NETWORK:
        // No protections for these types. Do nothing.
        break;
      case OUTPUT_TYPE_NONE:
        NOTREACHED();
        break;
    }
  }

  // Don't reveal protections requested by other clients.
  ProtectionRequests::iterator it = client_protection_requests_.find(client_id);
  if (it != client_protection_requests_.end()) {
    uint32_t requested_mask = 0;
    if (it->second.find(display_id) != it->second.end())
      requested_mask = it->second[display_id];
    *protection_mask = enabled & ~unfulfilled & requested_mask;
  } else {
    *protection_mask = 0;
  }
  return true;
}

bool OutputConfigurator::EnableOutputProtection(
    OutputProtectionClientId client_id,
    int64 display_id,
    uint32_t desired_method_mask) {
  if (!configure_display_)
    return false;

  DisplayProtections protections;
  for (ProtectionRequests::const_iterator it =
           client_protection_requests_.begin();
       it != client_protection_requests_.end();
       ++it) {
    for (DisplayProtections::const_iterator it2 = it->second.begin();
       it2 != it->second.end(); ++it2) {
      if (it->first == client_id && it2->first == display_id)
        continue;
      protections[it2->first] |= it2->second;
    }
  }
  protections[display_id] |= desired_method_mask;

  if (!ApplyProtections(protections))
    return false;

  if (desired_method_mask == OUTPUT_PROTECTION_METHOD_NONE) {
    if (client_protection_requests_.find(client_id) !=
        client_protection_requests_.end()) {
      client_protection_requests_[client_id].erase(display_id);
      if (client_protection_requests_[client_id].size() == 0)
        client_protection_requests_.erase(client_id);
    }
  } else {
    client_protection_requests_[client_id][display_id] = desired_method_mask;
  }

  return true;
}

void OutputConfigurator::Stop() {
  configure_display_ = false;
}

bool OutputConfigurator::SetDisplayPower(DisplayPowerState power_state,
                                         int flags) {
  if (!configure_display_)
    return false;

  VLOG(1) << "SetDisplayPower: power_state="
          << DisplayPowerStateToString(power_state) << " flags=" << flags
          << ", configure timer="
          << ((configure_timer_.get() && configure_timer_->IsRunning()) ?
              "Running" : "Stopped");
  if (power_state == power_state_ && !(flags & kSetDisplayPowerForceProbe))
    return true;

  delegate_->GrabServer();
  UpdateCachedOutputs();

  const OutputState new_state = ChooseOutputState(power_state);
  bool attempted_change = false;
  bool success = false;

  bool only_if_single_internal_display =
      flags & kSetDisplayPowerOnlyIfSingleInternalDisplay;
  bool single_internal_display =
      cached_outputs_.size() == 1 && cached_outputs_[0].is_internal;
  if (single_internal_display || !only_if_single_internal_display) {
    success = EnterStateOrFallBackToSoftwareMirroring(new_state, power_state);
    attempted_change = true;

    // Force the DPMS on since the driver doesn't always detect that it
    // should turn on. This is needed when coming back from idle suspend.
    if (success && power_state != DISPLAY_POWER_ALL_OFF)
      delegate_->ForceDPMSOn();
  }

  delegate_->UngrabServer();
  if (attempted_change)
    NotifyObservers(success, new_state);
  return true;
}

bool OutputConfigurator::SetDisplayMode(OutputState new_state) {
  if (!configure_display_)
    return false;

  VLOG(1) << "SetDisplayMode: state=" << OutputStateToString(new_state);
  if (output_state_ == new_state) {
    // Cancel software mirroring if the state is moving from
    // STATE_DUAL_EXTENDED to STATE_DUAL_EXTENDED.
    if (mirroring_controller_ && new_state == STATE_DUAL_EXTENDED)
      mirroring_controller_->SetSoftwareMirroring(false);
    NotifyObservers(true, new_state);
    return true;
  }

  delegate_->GrabServer();
  UpdateCachedOutputs();
  const bool success = EnterStateOrFallBackToSoftwareMirroring(
      new_state, power_state_);
  delegate_->UngrabServer();

  NotifyObservers(success, new_state);
  return success;
}

bool OutputConfigurator::Dispatch(const base::NativeEvent& event) {
  if (!configure_display_)
    return true;

  if (event->type - xrandr_event_base_ == RRScreenChangeNotify) {
    VLOG(1) << "Received RRScreenChangeNotify event";
    delegate_->UpdateXRandRConfiguration(event);
    return true;
  }

  // Bail out early for everything except RRNotify_OutputChange events
  // about an output getting connected or disconnected.
  if (event->type - xrandr_event_base_ != RRNotify)
    return true;
  const XRRNotifyEvent* notify_event = reinterpret_cast<XRRNotifyEvent*>(event);
  if (notify_event->subtype != RRNotify_OutputChange)
    return true;
  const XRROutputChangeNotifyEvent* output_change_event =
      reinterpret_cast<XRROutputChangeNotifyEvent*>(event);
  const int action = output_change_event->connection;
  if (action != RR_Connected && action != RR_Disconnected)
    return true;

  const bool connected = (action == RR_Connected);
  VLOG(1) << "Received RRNotify_OutputChange event:"
          << " output=" << output_change_event->output
          << " crtc=" << output_change_event->crtc
          << " mode=" << output_change_event->mode
          << " action=" << (connected ? "connected" : "disconnected");

  bool found_changed_output = false;
  for (std::vector<OutputSnapshot>::const_iterator it = cached_outputs_.begin();
       it != cached_outputs_.end(); ++it) {
    if (it->output == output_change_event->output) {
      if (connected && it->crtc == output_change_event->crtc &&
          it->current_mode == output_change_event->mode) {
        VLOG(1) << "Ignoring event describing already-cached state";
        return true;
      }
      found_changed_output = true;
      break;
    }
  }

  if (!connected && !found_changed_output) {
    VLOG(1) << "Ignoring event describing already-disconnected output";
    return true;
  }

  // Connecting/disconnecting a display may generate multiple events. Defer
  // configuring outputs to avoid grabbing X and configuring displays
  // multiple times.
  ScheduleConfigureOutputs();
  return true;
}

base::EventStatus OutputConfigurator::WillProcessEvent(
    const base::NativeEvent& event) {
  // XI_HierarchyChanged events are special. There is no window associated with
  // these events. So process them directly from here.
  if (configure_display_ && event->type == GenericEvent &&
      event->xgeneric.evtype == XI_HierarchyChanged) {
    VLOG(1) << "Received XI_HierarchyChanged event";
    // Defer configuring outputs to not stall event processing.
    // This also takes care of same event being received twice.
    ScheduleConfigureOutputs();
  }

  return base::EVENT_CONTINUE;
}

void OutputConfigurator::DidProcessEvent(const base::NativeEvent& event) {
}

void OutputConfigurator::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void OutputConfigurator::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void OutputConfigurator::SuspendDisplays() {
  // If the display is off due to user inactivity and there's only a single
  // internal display connected, switch to the all-on state before
  // suspending.  This shouldn't be very noticeable to the user since the
  // backlight is off at this point, and doing this lets us resume directly
  // into the "on" state, which greatly reduces resume times.
  if (power_state_ == DISPLAY_POWER_ALL_OFF) {
    SetDisplayPower(DISPLAY_POWER_ALL_ON,
                    kSetDisplayPowerOnlyIfSingleInternalDisplay);

    // We need to make sure that the monitor configuration we just did actually
    // completes before we return, because otherwise the X message could be
    // racing with the HandleSuspendReadiness message.
    delegate_->SyncWithServer();
  }
}

void OutputConfigurator::ResumeDisplays() {
  // Force probing to ensure that we pick up any changes that were made
  // while the system was suspended.
  SetDisplayPower(power_state_, kSetDisplayPowerForceProbe);
}

void OutputConfigurator::ScheduleConfigureOutputs() {
  if (configure_timer_.get()) {
    configure_timer_->Reset();
  } else {
    configure_timer_.reset(new base::OneShotTimer<OutputConfigurator>());
    configure_timer_->Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kConfigureDelayMs),
        this,
        &OutputConfigurator::ConfigureOutputs);
  }
}

void OutputConfigurator::UpdateCachedOutputs() {
  cached_outputs_ = delegate_->GetOutputs();

  // Set |selected_mode| fields.
  for (size_t i = 0; i < cached_outputs_.size(); ++i) {
    OutputSnapshot* output = &cached_outputs_[i];
    if (output->has_display_id) {
      int width = 0, height = 0;
      if (state_controller_ &&
          state_controller_->GetResolutionForDisplayId(
              output->display_id, &width, &height)) {
        output->selected_mode =
            FindOutputModeMatchingSize(*output, width, height);
      }
    }
    // Fall back to native mode.
    if (output->selected_mode == None)
      output->selected_mode = output->native_mode;
  }

  // Set |mirror_mode| fields.
  if (cached_outputs_.size() == 2) {
    bool one_is_internal = cached_outputs_[0].is_internal;
    bool two_is_internal = cached_outputs_[1].is_internal;
    int internal_outputs = (one_is_internal ? 1 : 0) +
        (two_is_internal ? 1 : 0);
    DCHECK_LT(internal_outputs, 2);
    LOG_IF(WARNING, internal_outputs == 2)
        << "Two internal outputs detected.";

    bool can_mirror = false;
    for (int attempt = 0; !can_mirror && attempt < 2; ++attempt) {
      // Try preserving external output's aspect ratio on the first attempt.
      // If that fails, fall back to the highest matching resolution.
      bool preserve_aspect = attempt == 0;

      if (internal_outputs == 1) {
        if (one_is_internal) {
          can_mirror = FindMirrorMode(&cached_outputs_[0], &cached_outputs_[1],
              is_panel_fitting_enabled_, preserve_aspect);
        } else {
          DCHECK(two_is_internal);
          can_mirror = FindMirrorMode(&cached_outputs_[1], &cached_outputs_[0],
              is_panel_fitting_enabled_, preserve_aspect);
        }
      } else {  // if (internal_outputs == 0)
        // No panel fitting for external outputs, so fall back to exact match.
        can_mirror = FindMirrorMode(&cached_outputs_[0], &cached_outputs_[1],
                                    false, preserve_aspect);
        if (!can_mirror && preserve_aspect) {
          // FindMirrorMode() will try to preserve aspect ratio of what it
          // thinks is external display, so if it didn't succeed with one, maybe
          // it will succeed with the other.  This way we will have the correct
          // aspect ratio on at least one of them.
          can_mirror = FindMirrorMode(&cached_outputs_[1], &cached_outputs_[0],
                                      false, preserve_aspect);
        }
      }
    }
  }
}

bool OutputConfigurator::FindMirrorMode(OutputSnapshot* internal_output,
                                        OutputSnapshot* external_output,
                                        bool try_panel_fitting,
                                        bool preserve_aspect) {
  const ModeInfo* internal_native_info =
      GetModeInfo(*internal_output, internal_output->native_mode);
  const ModeInfo* external_native_info =
      GetModeInfo(*external_output, external_output->native_mode);
  if (!internal_native_info || !external_native_info)
    return false;

  // Check if some external output resolution can be mirrored on internal.
  // Prefer the modes in the order that X sorts them, assuming this is the order
  // in which they look better on the monitor.
  for (ModeInfoMap::const_iterator external_it =
       external_output->mode_infos.begin();
       external_it != external_output->mode_infos.end(); ++external_it) {
    const ModeInfo& external_info = external_it->second;
    bool is_native_aspect_ratio =
        external_native_info->width * external_info.height ==
        external_native_info->height * external_info.width;
    if (preserve_aspect && !is_native_aspect_ratio)
      continue;  // Allow only aspect ratio preserving modes for mirroring.

    // Try finding an exact match.
    for (ModeInfoMap::const_iterator internal_it =
         internal_output->mode_infos.begin();
         internal_it != internal_output->mode_infos.end(); ++internal_it) {
      const ModeInfo& internal_info = internal_it->second;
      if (internal_info.width == external_info.width &&
          internal_info.height == external_info.height &&
          internal_info.interlaced == external_info.interlaced) {
        internal_output->mirror_mode = internal_it->first;
        external_output->mirror_mode = external_it->first;
        return true;  // Mirror mode found.
      }
    }

    // Try to create a matching internal output mode by panel fitting.
    if (try_panel_fitting) {
      // We can downscale by 1.125, and upscale indefinitely. Downscaling looks
      // ugly, so, can fit == can upscale. Also, internal panels don't support
      // fitting interlaced modes.
      bool can_fit =
          internal_native_info->width >= external_info.width &&
          internal_native_info->height >= external_info.height &&
          !external_info.interlaced;
      if (can_fit) {
        RRMode mode = external_it->first;
        delegate_->AddOutputMode(internal_output->output, mode);
        internal_output->mode_infos.insert(std::make_pair(mode, external_info));
        internal_output->mirror_mode = mode;
        external_output->mirror_mode = mode;
        return true;  // Mirror mode created.
      }
    }
  }

  return false;
}

void OutputConfigurator::ConfigureOutputs() {
  configure_timer_.reset();

  delegate_->GrabServer();
  UpdateCachedOutputs();
  const OutputState new_state = ChooseOutputState(power_state_);
  const bool success = EnterStateOrFallBackToSoftwareMirroring(
      new_state, power_state_);
  delegate_->UngrabServer();

  NotifyObservers(success, new_state);
  delegate_->SendProjectingStateToPowerManager(IsProjecting(cached_outputs_));
}

void OutputConfigurator::NotifyObservers(bool success,
                                         OutputState attempted_state) {
  if (success) {
    FOR_EACH_OBSERVER(Observer, observers_,
                      OnDisplayModeChanged(cached_outputs_));
  } else {
    FOR_EACH_OBSERVER(Observer, observers_,
                      OnDisplayModeChangeFailed(attempted_state));
  }
}

bool OutputConfigurator::EnterStateOrFallBackToSoftwareMirroring(
    OutputState output_state,
    DisplayPowerState power_state) {
  bool success = EnterState(output_state, power_state);
  if (mirroring_controller_) {
    bool enable_software_mirroring = false;
    if (!success && output_state == STATE_DUAL_MIRROR) {
      if (output_state_ != STATE_DUAL_EXTENDED || power_state_ != power_state)
        EnterState(STATE_DUAL_EXTENDED, power_state);
      enable_software_mirroring = success =
          output_state_ == STATE_DUAL_EXTENDED;
    }
    mirroring_controller_->SetSoftwareMirroring(enable_software_mirroring);
  }
  return success;
}

bool OutputConfigurator::EnterState(
    OutputState output_state,
    DisplayPowerState power_state) {
  std::vector<bool> output_power;
  int num_on_outputs = GetOutputPower(
      cached_outputs_, power_state, &output_power);
  VLOG(1) << "EnterState: output=" << OutputStateToString(output_state)
          << " power=" << DisplayPowerStateToString(power_state);

  // Framebuffer dimensions.
  int width = 0, height = 0;
  std::vector<OutputSnapshot> updated_outputs = cached_outputs_;

  switch (output_state) {
    case STATE_INVALID:
      NOTREACHED() << "Ignoring request to enter invalid state with "
                   << updated_outputs.size() << " connected output(s)";
      return false;
    case STATE_HEADLESS:
      if (updated_outputs.size() != 0) {
        LOG(WARNING) << "Ignoring request to enter headless mode with "
                     << updated_outputs.size() << " connected output(s)";
        return false;
      }
      break;
    case STATE_SINGLE: {
      // If there are multiple outputs connected, only one should be turned on.
      if (updated_outputs.size() != 1 && num_on_outputs != 1) {
        LOG(WARNING) << "Ignoring request to enter single mode with "
                     << updated_outputs.size() << " connected outputs and "
                     << num_on_outputs << " turned on";
        return false;
      }

      for (size_t i = 0; i < updated_outputs.size(); ++i) {
        OutputSnapshot* output = &updated_outputs[i];
        output->x = 0;
        output->y = 0;
        output->current_mode = output_power[i] ? output->selected_mode : None;

        if (output_power[i] || updated_outputs.size() == 1) {
          const ModeInfo* mode_info =
              GetModeInfo(*output, output->selected_mode);
          if (!mode_info)
            return false;
          if (mode_info->width == 1024 && mode_info->height == 768) {
            VLOG(1) << "Potentially misdetecting display(1024x768):"
                    << " outputs size=" << updated_outputs.size()
                    << ", num_on_outputs=" << num_on_outputs
                    << ", current size:" << width << "x" << height
                    << ", i=" << i
                    << ", output=" << OutputSnapshotToString(output)
                    << ", mode_info=" << ModeInfoToString(mode_info);
          }
          width = mode_info->width;
          height = mode_info->height;
        }
      }
      break;
    }
    case STATE_DUAL_MIRROR: {
      if (updated_outputs.size() != 2 ||
          (num_on_outputs != 0 && num_on_outputs != 2)) {
        LOG(WARNING) << "Ignoring request to enter mirrored mode with "
                     << updated_outputs.size() << " connected output(s) and "
                     << num_on_outputs << " turned on";
        return false;
      }

      if (!updated_outputs[0].mirror_mode)
        return false;
      const ModeInfo* mode_info =
          GetModeInfo(updated_outputs[0], updated_outputs[0].mirror_mode);
      if (!mode_info)
        return false;
      width = mode_info->width;
      height = mode_info->height;

      for (size_t i = 0; i < updated_outputs.size(); ++i) {
        OutputSnapshot* output = &updated_outputs[i];
        output->x = 0;
        output->y = 0;
        output->current_mode = output_power[i] ? output->mirror_mode : None;
        if (output->touch_device_id) {
          // CTM needs to be calculated if aspect preserving scaling is used.
          // Otherwise, assume it is full screen, and use identity CTM.
          if (output->mirror_mode != output->native_mode &&
              output->is_aspect_preserving_scaling) {
            output->transform = GetMirrorModeCTM(*output);
            mirrored_display_area_ratio_map_[output->touch_device_id] =
                GetMirroredDisplayAreaRatio(*output);
          }
        }
      }
      break;
    }
    case STATE_DUAL_EXTENDED: {
      if (updated_outputs.size() != 2 ||
          (num_on_outputs != 0 && num_on_outputs != 2)) {
        LOG(WARNING) << "Ignoring request to enter extended mode with "
                     << updated_outputs.size() << " connected output(s) and "
                     << num_on_outputs << " turned on";
        return false;
      }

      for (size_t i = 0; i < updated_outputs.size(); ++i) {
        OutputSnapshot* output = &updated_outputs[i];
        output->x = 0;
        output->y = height ? height + kVerticalGap : 0;
        output->current_mode = output_power[i] ? output->selected_mode : None;

        // Retain the full screen size even if all outputs are off so the
        // same desktop configuration can be restored when the outputs are
        // turned back on.
        const ModeInfo* mode_info =
            GetModeInfo(updated_outputs[i], updated_outputs[i].selected_mode);
        if (!mode_info)
          return false;
        width = std::max<int>(width, mode_info->width);
        height += (height ? kVerticalGap : 0) + mode_info->height;
      }

      for (size_t i = 0; i < updated_outputs.size(); ++i) {
        OutputSnapshot* output = &updated_outputs[i];
        if (output->touch_device_id) {
          const ModeInfo* mode_info =
              GetModeInfo(*output, output->selected_mode);
          DCHECK(mode_info);
          CoordinateTransformation* ctm = &(output->transform);
          ctm->x_scale = static_cast<float>(mode_info->width) / width;
          ctm->x_offset = static_cast<float>(output->x) / width;
          ctm->y_scale = static_cast<float>(mode_info->height) / height;
          ctm->y_offset = static_cast<float>(output->y) / height;
        }
      }
      break;
    }
  }

  // Finally, apply the desired changes.
  DCHECK_EQ(cached_outputs_.size(), updated_outputs.size());
  if (!updated_outputs.empty()) {
    delegate_->CreateFrameBuffer(width, height, updated_outputs);
    for (size_t i = 0; i < updated_outputs.size(); ++i) {
      const OutputSnapshot& output = updated_outputs[i];
      if (delegate_->ConfigureCrtc(output.crtc, output.current_mode,
                                   output.output, output.x, output.y)) {
        if (output.touch_device_id)
          delegate_->ConfigureCTM(output.touch_device_id, output.transform);
        cached_outputs_[i] = updated_outputs[i];
      } else {
        LOG(WARNING) << "Unable to configure CRTC " << output.crtc << ":"
                     << " mode=" << output.current_mode
                     << " output=" << output.output
                     << " x=" << output.x
                     << " y=" << output.y;
      }
    }
  }

  output_state_ = output_state;
  power_state_ = power_state;
  return true;
}

OutputState OutputConfigurator::ChooseOutputState(
    DisplayPowerState power_state) const {
  int num_on_outputs = GetOutputPower(cached_outputs_, power_state, NULL);
  switch (cached_outputs_.size()) {
    case 0:
      return STATE_HEADLESS;
    case 1:
      return STATE_SINGLE;
    case 2: {
      if (num_on_outputs == 1) {
        // If only one output is currently turned on, return the "single"
        // state so that its native mode will be used.
        return STATE_SINGLE;
      } else {
        // With either both outputs on or both outputs off, use one of the
        // dual modes.
        std::vector<int64> display_ids;
        for (size_t i = 0; i < cached_outputs_.size(); ++i) {
          // If display id isn't available, switch to extended mode.
          if (!cached_outputs_[i].has_display_id)
            return STATE_DUAL_EXTENDED;
          display_ids.push_back(cached_outputs_[i].display_id);
        }
        return state_controller_->GetStateForDisplayIds(display_ids);
      }
    }
    default:
      NOTREACHED();
  }
  return STATE_INVALID;
}

OutputConfigurator::CoordinateTransformation
OutputConfigurator::GetMirrorModeCTM(
    const OutputConfigurator::OutputSnapshot& output) {
  CoordinateTransformation ctm;  // Default to identity
  const ModeInfo* native_mode_info = GetModeInfo(output, output.native_mode);
  const ModeInfo* mirror_mode_info = GetModeInfo(output, output.mirror_mode);

  if (!native_mode_info || !mirror_mode_info ||
      native_mode_info->height == 0 || mirror_mode_info->height == 0 ||
      native_mode_info->width == 0 || mirror_mode_info->width == 0)
    return ctm;

  float native_mode_ar = static_cast<float>(native_mode_info->width) /
      static_cast<float>(native_mode_info->height);
  float mirror_mode_ar = static_cast<float>(mirror_mode_info->width) /
      static_cast<float>(mirror_mode_info->height);

  if (mirror_mode_ar > native_mode_ar) {  // Letterboxing
    ctm.x_scale = 1.0;
    ctm.x_offset = 0.0;
    ctm.y_scale = mirror_mode_ar / native_mode_ar;
    ctm.y_offset = (native_mode_ar / mirror_mode_ar - 1.0) * 0.5;
    return ctm;
  }
  if (native_mode_ar > mirror_mode_ar) {  // Pillarboxing
    ctm.y_scale = 1.0;
    ctm.y_offset = 0.0;
    ctm.x_scale = native_mode_ar / mirror_mode_ar;
    ctm.x_offset = (mirror_mode_ar / native_mode_ar - 1.0) * 0.5;
    return ctm;
  }

  return ctm;  // Same aspect ratio - return identity
}

float OutputConfigurator::GetMirroredDisplayAreaRatio(
    const OutputConfigurator::OutputSnapshot& output) {
  float area_ratio = 1.0f;
  const ModeInfo* native_mode_info = GetModeInfo(output, output.native_mode);
  const ModeInfo* mirror_mode_info = GetModeInfo(output, output.mirror_mode);

  if (!native_mode_info || !mirror_mode_info ||
      native_mode_info->height == 0 || mirror_mode_info->height == 0 ||
      native_mode_info->width == 0 || mirror_mode_info->width == 0)
    return area_ratio;

  float width_ratio = static_cast<float>(mirror_mode_info->width) /
      static_cast<float>(native_mode_info->width);
  float height_ratio = static_cast<float>(mirror_mode_info->height) /
      static_cast<float>(native_mode_info->height);

  area_ratio = width_ratio * height_ratio;
  return area_ratio;
}

}  // namespace chromeos
