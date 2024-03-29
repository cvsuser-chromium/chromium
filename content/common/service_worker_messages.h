// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Message definition file, included multiple times, hence no include guard.

#include "base/strings/string16.h"
#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_param_traits.h"
#include "third_party/WebKit/public/platform/WebServiceWorkerError.h"
#include "url/gurl.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START ServiceWorkerMsgStart

IPC_ENUM_TRAITS(blink::WebServiceWorkerError::ErrorType)

// Messages sent from the child process to the browser.

IPC_MESSAGE_CONTROL4(ServiceWorkerHostMsg_RegisterServiceWorker,
                     int32 /* thread_id */,
                     int32 /* request_id */,
                     GURL /* scope */,
                     GURL /* script_url */)

IPC_MESSAGE_CONTROL3(ServiceWorkerHostMsg_UnregisterServiceWorker,
                     int32 /* thread_id */,
                     int32 /* request_id */,
                     GURL /* scope (url pattern) */)

// Messages sent from the browser to the child process.

// Response to ServiceWorkerMsg_RegisterServiceWorker
IPC_MESSAGE_CONTROL3(ServiceWorkerMsg_ServiceWorkerRegistered,
                     int32 /* thread_id */,
                     int32 /* request_id */,
                     int64 /* service_worker_id */)

// Response to ServiceWorkerMsg_UnregisterServiceWorker
IPC_MESSAGE_CONTROL2(ServiceWorkerMsg_ServiceWorkerUnregistered,
                     int32 /* thread_id */,
                     int32 /* request_id */)

// Sent when any kind of registration error occurs during a
// RegisterServiceWorker / UnregisterServiceWorker handler above.
IPC_MESSAGE_CONTROL4(ServiceWorkerMsg_ServiceWorkerRegistrationError,
                     int32 /* thread_id */,
                     int32 /* request_id */,
                     blink::WebServiceWorkerError::ErrorType /* code */,
                     string16 /* message */)
