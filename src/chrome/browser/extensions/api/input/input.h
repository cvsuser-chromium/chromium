// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_
#define CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_

#include "base/compiler_specific.h"
#include "chrome/browser/extensions/api/profile_keyed_api_factory.h"
#include "chrome/browser/extensions/extension_function.h"

class Profile;

namespace extensions {

class VirtualKeyboardPrivateInsertTextFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("virtualKeyboardPrivate.insertText",
                             VIRTUALKEYBOARDPRIVATE_INSERTTEXT);

 protected:
  virtual ~VirtualKeyboardPrivateInsertTextFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class VirtualKeyboardPrivateMoveCursorFunction : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("virtualKeyboardPrivate.moveCursor",
                             VIRTUALKEYBOARDPRIVATE_MOVECURSOR);

 protected:
  virtual ~VirtualKeyboardPrivateMoveCursorFunction() {}

  // ExtensionFunction.
  virtual bool RunImpl() OVERRIDE;
};

class VirtualKeyboardPrivateSendKeyEventFunction
    : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "virtualKeyboardPrivate.sendKeyEvent",
      VIRTUALKEYBOARDPRIVATE_SENDKEYEVENT);

 protected:
  virtual ~VirtualKeyboardPrivateSendKeyEventFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class VirtualKeyboardPrivateHideKeyboardFunction
    : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "virtualKeyboardPrivate.hideKeyboard",
      VIRTUALKEYBOARDPRIVATE_HIDEKEYBOARD);

 protected:
  virtual ~VirtualKeyboardPrivateHideKeyboardFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class VirtualKeyboardPrivateKeyboardLoadedFunction
    : public SyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION(
      "virtualKeyboardPrivate.keyboardLoaded",
      VIRTUALKEYBOARDPRIVATE_KEYBOARDLOADED);

 protected:
  virtual ~VirtualKeyboardPrivateKeyboardLoadedFunction() {}

  // ExtensionFunction:
  virtual bool RunImpl() OVERRIDE;
};

class InputAPI : public ProfileKeyedAPI {
 public:
  explicit InputAPI(Profile* profile);
  virtual ~InputAPI();

  // ProfileKeyedAPI implementation.
  static ProfileKeyedAPIFactory<InputAPI>* GetFactoryInstance();

 private:
  friend class ProfileKeyedAPIFactory<InputAPI>;

  // ProfileKeyedAPI implementation.
  static const char* service_name() {
    return "InputAPI";
  }
  static const bool kServiceIsNULLWhileTesting = true;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_INPUT_INPUT_H_
