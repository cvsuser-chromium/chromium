// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/plugin/pepper_network_manager.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/net_address.h"
#include "ppapi/cpp/network_list.h"
#include "remoting/client/plugin/pepper_util.h"
#include "third_party/libjingle/source/talk/base/socketaddress.h"

namespace remoting {

PepperNetworkManager::PepperNetworkManager(const pp::InstanceHandle& instance)
    : monitor_(instance),
      start_count_(0),
      network_list_received_(false),
      callback_factory_(this),
      weak_factory_(this) {
  pp::CompletionCallbackWithOutput<pp::NetworkList> callback =
      callback_factory_.NewCallbackWithOutput(
          &PepperNetworkManager::OnNetworkList);
  monitor_.UpdateNetworkList(callback);
}

PepperNetworkManager::~PepperNetworkManager() {
  DCHECK(!start_count_);
}

void PepperNetworkManager::StartUpdating() {
  if (network_list_received_) {
    // Post a task to avoid reentrancy.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&PepperNetworkManager::SendNetworksChangedSignal,
                              weak_factory_.GetWeakPtr()));
  }
  ++start_count_;
}

void PepperNetworkManager::StopUpdating() {
  DCHECK_GT(start_count_, 0);
  --start_count_;
}

void PepperNetworkManager::OnNetworkList(int32_t result,
                                         const pp::NetworkList& list) {
  if (result != PP_OK) {
    SignalError();
    return;
  }
  DCHECK(!list.is_null());

  network_list_received_ = true;

  // Request for the next update.
  pp::CompletionCallbackWithOutput<pp::NetworkList> callback =
      callback_factory_.NewCallbackWithOutput(
          &PepperNetworkManager::OnNetworkList);
  monitor_.UpdateNetworkList(callback);

  // Convert the networks to talk_base::Network.
  std::vector<talk_base::Network*> networks;
  size_t count = list.GetCount();
  for (size_t i = 0; i < count; i++) {
    std::vector<pp::NetAddress> addresses;
    list.GetIpAddresses(i, &addresses);

    if (addresses.size() == 0)
      continue;

    for (size_t i = 0; i < addresses.size(); ++i) {
      talk_base::SocketAddress address;
      PpNetAddressToSocketAddress(addresses[i], &address);
      talk_base::Network* network = new talk_base::Network(
          list.GetName(i), list.GetDisplayName(i), address.ipaddr(), 0);
      network->AddIP(address.ipaddr());
      networks.push_back(network);
    }
  }

  bool changed = false;
  MergeNetworkList(networks, &changed);
  if (changed)
    SignalNetworksChanged();
}

void PepperNetworkManager::SendNetworksChangedSignal() {
  SignalNetworksChanged();
}

}  // namespace remoting
