// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_NFC_RECORD_CLIENT_H_
#define CHROMEOS_DBUS_NFC_RECORD_CLIENT_H_

#include <string>

#include "chromeos/chromeos_export.h"
#include "chromeos/dbus/dbus_client.h"
#include "chromeos/dbus/dbus_client_implementation_type.h"
#include "chromeos/dbus/nfc_property_set.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"

namespace chromeos {

class NfcDeviceClient;
class NfcTagClient;

// NfcRecordClient is used to communicate with objects representing NDEF
// records that are stored in remote NFC tags and devices.
class CHROMEOS_EXPORT NfcRecordClient : public DBusClient {
 public:
  // Structure of properties associated with an NFC record.
  struct Properties : public NfcPropertySet {
    // The NDEF record type.  Possible values are "SmartPoster", "Text", "URI",
    // "HandoverRequest", "HandoverSelect", "HandoverCarrier". Read-only.
    dbus::Property<std::string> type;

    // The character encoding. Possible values are "UTF-8" or "UTF-16".
    // This property is only valid for Text and SmartPoster's title records.
    // Read-only.
    dbus::Property<std::string> encoding;

    // The ISO/IANA language code (For example "en" or "jp"). This property is
    // only valid for Text and SmartPoster's title records.
    dbus::Property<std::string> language;

    // The human readable representation of a text or title record.
    // This property is only valid for Text and SmartPoster's title records.
    // Read-only.
    dbus::Property<std::string> representation;

    // The record URI (for example https://nfc-forum.org). This is the complete
    // URI, including the scheme and the resource. This property is only valid
    // for SmartPoster's URI type records.
    // Read-only.
    dbus::Property<std::string> uri;

    // The URI object MIME type. This is a description of the MIME type of the
    // object the URI points at. This is not a mandatory field and is only
    // valid for SmartPosters carrying a URI record.
    // Read-only.
    dbus::Property<std::string> mime_type;

    // The URI object size. This is the size of the object the URI points at.
    // It should be used by applications to decide if they can afford to fetch
    // the object or not. This is not a mandatory field and is only valid for
    // Smart Posters carrying a URI record.
    // Read-only.
    dbus::Property<std::string> size;

    // The suggested course of action. This one is only valid for Smart Posters
    // and is a suggestion only. It can be ignored, and the possible values are
    // "Do" (for example launch the browser), "Save" (for example save the URI
    // in the bookmarks folder), or "Edit" (for example open the URI in an URI
    // editor for the user to modify it).
    dbus::Property<std::string> action;

    Properties(dbus::ObjectProxy* object_proxy,
               const PropertyChangedCallback& callback);
    virtual ~Properties();
  };

  // Interface for observing changes from a remote NFC NDEF record.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called when a remote NFC record with the object path |object_path| is
    // added to the set of known records.
    virtual void RecordAdded(const dbus::ObjectPath& object_path) {}

    // Called when a remote NFC record with the object path |object_path| is
    // removed from the set of known records.
    virtual void RecordRemoved(const dbus::ObjectPath& object_path) {}

    // Called when the record property with the name |property_name| on record
    // with object path |object_path| has acquired a new value.
    virtual void RecordPropertyChanged(const dbus::ObjectPath& object_path,
                                       const std::string& property_name) {}
  };

  // NDEF records can be created via the Tag and Device interfaces by passing a
  // dictionary of strings containing the record properties and their values to
  // their respective API methods.
  typedef std::map<std::string, std::string> Attributes;

  virtual ~NfcRecordClient();

  // Adds and removes observers for events on all remote NFC records. Check the
  // |object_path| parameter of observer methods to determine which record is
  // issuing the event.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;

  // Obtain the properties for the NFC record with object path |object_path|;
  // any values should be copied if needed.
  virtual Properties* GetProperties(const dbus::ObjectPath& object_path) = 0;

  // Creates the instance.
  static NfcRecordClient* Create(DBusClientImplementationType type,
                                 NfcDeviceClient* device_client,
                                 NfcTagClient* tag_client);

 protected:
  friend class NfcClientTest;

  NfcRecordClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(NfcRecordClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_NFC_RECORD_CLIENT_H_
