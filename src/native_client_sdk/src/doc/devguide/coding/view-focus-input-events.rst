.. _view_focus_input_events:

####################################
View Change, Focus, and Input Events
####################################

.. contents::
  :local:
  :backlinks: none
  :depth: 2

This chapter describes view change, focus, and input event handling for a
Native Client module. The chapter assumes you are familiar with the
material presented in the :doc:`Technical Overview <../../overview>`.

There are two examples used in this chapter to illustrate basic
programming techniques. The ``input_events`` example is used to
illustrate how your module can react to keyboard and mouse input
event.  The ``mouse_lock`` example is used to illustrate how your module
can react to view change events. You can find these examples in the
``/examples/api/input_events`` and ``/examples/api/mouse_lock``
directories in the Native Client SDK.  There is also the
ppapi_simple library that can be used to to implement most of the
boiler plate.  The ``pi_generator`` example in
``/examples/demo/pi_generator`` uses ppapi_simple to manage view
change events and 2D graphics.


Overview
========

When a user interacts with the web page using a keyboard, mouse or
some other input device, the browser generates input events.
In a traditional web application, these input events are
passed to and handled in JavaScript, typically through event listeners
and event handlers. In a Native Client application, user interaction
with an instance of a module (e.g., clicking inside the rectangle
managed by a module) also generates input events, which are passed to
the module. The browser also passes view change and focus events that
affect a module's instance to the module. Native Client modules can
override certain functions in the `pp::Instance
<https://developers.google.com/native-client/peppercpp/classpp_1_1_instance>`_
class to handle input and browser events. These functions are listed in
the table below:


======================  ===============================  ====================
Function                  Event                            Use
======================  ===============================  ====================
``DidChangeView``       Called when the position,        An implementation
                        size, or clip rectangle          of this function
                        of the module's instance in      might check the size
                        the browser has changed.         of the module
                        This event also occurs           instance's rectangle
                        when browser window is           has changed and
                        resized or mouse wheel           reallocate the
                        is scrolled.                     graphics context
                                                         when a different
                                                         size is received.

``DidChangeFocus``      Called when the module's         An implementation
                        instance in the browser          of this function
                        has gone in or out of            might start or stop
                        focus (usually by                an animation or a
                        clicking inside or               blinking cursor.
                        outside the module
                        instance). Having focus
                        means that keyboard
                        events will be sent to
                        the module instance.
                        An instance's default
                        condition is that it
                        does not have focus.

``HandleDocumentLoad``  Called after                     This API is only
                        ``pp::Instance::Init()``         applicable when you
                        for a full-frame module          are writing an
                        instance that was                extension to enhance
                        instantiated based on            the abilities of
                        the MIME type of a               the Chrome web
                        DOMWindow navigation.            browser. For
                        This situation only              example, a PDF
                        applies to modules that          viewer might
                        are pre-registered to            implement this
                        handle certain MIME              function to download
                        types. If you haven't            and display a PDF
                        specifically registered          file.
                        to handle a MIME type or
                        aren't positive this
                        applies to you, your
                        implementation of this
                        function can just return
                        false.

``HandleInputEvent``    Called when a user               An implementation of
                        interacts with the               this function
                        module's instance in the         examines the input
                        browser using an input           event type and
                        device such as a mouse           branches accordingly.
                        or keyboard. You must
                        register your module to
                        accept input events
                        using
                        ``RequestInputEvents()``
                        for mouse events and
                        ``RequestFilteringInputEvents``
                        for keyboard events
                        prior to overriding this
                        function.
======================  ===============================  ====================


These interfaces are found in the `pp::Instance class
<https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_instance>`_.
The sections below provide examples of how to handle these events.


Handling browser events
=======================

DidChangeView()
---------------

In the ``mouse_lock`` example, ``DidChangeView()`` checks the previous size
of instance's rectangle versus the new size.  It also compares
other state such as whether or not the app is running in full screen mode.
If none of the state has actually changed, no action is needed.
However, if the size of the view or other state has changed, it frees the
old graphics context and allocates a new one.

.. naclcode::

  void MouseLockInstance::DidChangeView(const pp::View& view) {
    // DidChangeView can get called for many reasons, so we only want to
    // rebuild the device context if we really need to.
    if ((size_ == view.GetRect().size()) &&
        (was_fullscreen_ == view.IsFullscreen()) && is_context_bound_) {
      return;
    }

    // ...

    // Reallocate the graphics context.
    size_ = view.GetRect().size();
    device_context_ = pp::Graphics2D(this, size_, false);
    waiting_for_flush_completion_ = false;

    is_context_bound_ = BindGraphics(device_context_);
    // ...

    // Remember if we are fullscreen or not
    was_fullscreen_ = view.IsFullscreen();
    // ...
  }


For more information about graphics contexts and how to manipulate images, see:

* `pp::ImageData class <https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_image_data>`_
* `pp::Graphics2D class <https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_graphics2_d>`_


DidChangeFocus()
----------------

``DidChangeFocus()`` is called when you click inside or outside of a
module's instance in the web page. When the instance goes out
of focus (click outside of the instance), you might do something
like stop an animation. When the instance regains focus, you can
restart the animation.

.. naclcode::

  void DidChangeFocus(bool focus) {
    // Do something like stopping animation or a blinking cursor in
    // the instance.
  }


Handling input events
=====================

Input events are events that occur when the user interacts with a
module instance using the mouse, keyboard, or other input device
(e.g., touch screen). This section describes how the ``input_events``
example handles input events.


Registering a module to accept input events
-------------------------------------------

Before your module can handle these events, you must register your
module to accept input events using ``RequestInputEvents()`` for mouse
events and ``RequestFilteringInputEvents()`` for keyboard events. For the
``input_events`` example, this is done in the constructor of the
``InputEventInstance`` class:

.. naclcode::

  class InputEventInstance : public pp::Instance {
   public:
    explicit InputEventInstance(PP_Instance instance)
        : pp::Instance(instance), event_thread_(NULL), callback_factory_(this) {
      RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE | PP_INPUTEVENT_CLASS_WHEEL |
                         PP_INPUTEVENT_CLASS_TOUCH);
      RequestFilteringInputEvents(PP_INPUTEVENT_CLASS_KEYBOARD);
    }
    // ...
  };


``RequestInputEvents()`` and ``RequestFilteringInputEvents()`` accept a
combination of flags that identify the class of events that the
instance is requesting to receive. Input event classes are defined in
the `PP_InputEvent_Class
<https://developers.google.com/native-client/dev/pepperc/group___enums.html#gafe68e3c1031daa4a6496845ff47649cd>`_
enumeration in `ppb_input_event.h
<https://developers.google.com/native-client/dev/pepperc/ppb__input__event_8h>`_.


Determining and branching on event types
----------------------------------------

In a typical implementation, the ``HandleInputEvent()`` function
determines the type of each event using the ``GetType()`` function found
in the ``InputEvent`` class. The ``HandleInputEvent()`` function then uses a
switch statement to branch on the type of input event. Input events
are defined in the `PP_InputEvent_Type
<https://developers.google.com/native-client/dev/pepperc/group___enums.html#gaca7296cfec99fcb6646b7144d1d6a0c5>`_
enumeration in `ppb_input_event.h
<https://developers.google.com/native-client/dev/pepperc/ppb__input__event_8h>`_.

.. naclcode::

  virtual bool HandleInputEvent(const pp::InputEvent& event) {
    Event* event_ptr = NULL;
    switch (event.GetType()) {
      case PP_INPUTEVENT_TYPE_UNDEFINED:
        break;
      case PP_INPUTEVENT_TYPE_MOUSEDOWN:
      case PP_INPUTEVENT_TYPE_MOUSEUP:
      case PP_INPUTEVENT_TYPE_MOUSEMOVE:
      case PP_INPUTEVENT_TYPE_MOUSEENTER:
      case PP_INPUTEVENT_TYPE_MOUSELEAVE:
      case PP_INPUTEVENT_TYPE_CONTEXTMENU: {
        pp::MouseInputEvent mouse_event(event);
        PP_InputEvent_MouseButton pp_button = mouse_event.GetButton();
        MouseEvent::MouseButton mouse_button = MouseEvent::kNone;
        switch (pp_button) {
          case PP_INPUTEVENT_MOUSEBUTTON_NONE:
            mouse_button = MouseEvent::kNone;
            break;
          case PP_INPUTEVENT_MOUSEBUTTON_LEFT:
            mouse_button = MouseEvent::kLeft;
            break;
          case PP_INPUTEVENT_MOUSEBUTTON_MIDDLE:
            mouse_button = MouseEvent::kMiddle;
            break;
          case PP_INPUTEVENT_MOUSEBUTTON_RIGHT:
            mouse_button = MouseEvent::kRight;
            break;
        }
        event_ptr =
            new MouseEvent(ConvertEventModifier(mouse_event.GetModifiers()),
                           mouse_button,
                           mouse_event.GetPosition().x(),
                           mouse_event.GetPosition().y(),
                           mouse_event.GetClickCount(),
                           mouse_event.GetTimeStamp(),
                           event.GetType() == PP_INPUTEVENT_TYPE_CONTEXTMENU);
      } break;
      case PP_INPUTEVENT_TYPE_WHEEL: {
        pp::WheelInputEvent wheel_event(event);
        event_ptr =
            new WheelEvent(ConvertEventModifier(wheel_event.GetModifiers()),
                           wheel_event.GetDelta().x(),
                           wheel_event.GetDelta().y(),
                           wheel_event.GetTicks().x(),
                           wheel_event.GetTicks().y(),
                           wheel_event.GetScrollByPage(),
                           wheel_event.GetTimeStamp());
      } break;
      case PP_INPUTEVENT_TYPE_RAWKEYDOWN:
      case PP_INPUTEVENT_TYPE_KEYDOWN:
      case PP_INPUTEVENT_TYPE_KEYUP:
      case PP_INPUTEVENT_TYPE_CHAR: {
        pp::KeyboardInputEvent key_event(event);
        event_ptr = new KeyEvent(ConvertEventModifier(key_event.GetModifiers()),
                                 key_event.GetKeyCode(),
                                 key_event.GetTimeStamp(),
                                 key_event.GetCharacterText().DebugString());
      } break;
      default: {
        // For any unhandled events, send a message to the browser
        // so that the user is aware of these and can investigate.
        std::stringstream oss;
        oss << "Default (unhandled) event, type=" << event.GetType();
        PostMessage(oss.str());
      } break;
    }
    event_queue_.Push(event_ptr);
    return true;
  }


Notice that the generic ``InputEvent`` received by ``HandleInputEvent()`` is
converted into a specific type after the event type is
determined.  The event types handled in the example code are
``MouseInputEvent``, ``WheelInputEvent``, and ``KeyboardInputEvent``.
There are also ``TouchInputEvents``.  For the latest list of event types,
see the `InputEvent documentation
<https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_input_event>`_.
For reference information related to the these event classes, see the
following documentation:

* `pp::MouseInputEvent class <https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_mouse_input_event>`_
* `pp::WheelInputEvent class <https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_wheel_input_event>`_
* `pp::KeyboardInputEvent class <https://developers.google.com/native-client/dev/peppercpp/classpp_1_1_keyboard_input_event>`_


Threading and blocking
----------------------

``HandleInputEvent()`` in this example runs on the main module thread.
However, the bulk of the work happens on a separate worker thread (see
``ProcessEventOnWorkerThread``). ``HandleInputEvent()`` puts events in
the ``event_queue_`` and the worker thread takes events from the
``event_queue_``. This processing happens independently of the main
thread, so as not to slow down the browser.
