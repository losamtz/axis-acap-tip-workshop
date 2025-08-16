# Stateless Event - Send Pulse

Stateless ONVIF/Axis event “pulse” generator for ACAP.
This sample declares a stateless event under the CameraApplicationPlatform topic and periodically sends it with a numeric value that steps 0→100 by 10.

## What this app does

- Declares a stateless event:

```
    topic0 = tnsaxis:CameraApplicationPlatform
    topic1 = tnsaxis:SendPulse
    topic2 = tnsaxis:SendPulseEvent
    Data key: value (INT)
```

- Starts a GLib timer that fires every 5 seconds.

- Each tick:

    - Builds an AXEvent with the current value
    - Sends it with ax_event_handler_send_event
    - Increments value by 10 (wraps at 100)

This is ideal for testing action rules, event forwarding (MQTT), or another ACAP app that subscribes to these events.


- **Stateless events**
This sample declares the event as stateless (1), meaning each fired event is independent and does not represent an ongoing state.

- **Event loop**
A GLib main loop schedules periodic event generation every 5 seconds using g_timeout_add_seconds.


## Code Walkthrough

- setup_declaration()
Declares the event and defines the topic hierarchy and data fields.

- declaration_complete()
Callback when declaration succeeds. Sets initial values and starts the periodic event timer.

- send_event()
Creates an event, adds key/value pairs, and sends it through ax_event_handler_send_event. sends it with a numeric value that steps 0→100 by 10.

- main()
Initializes syslog, seeds RNG, creates the event handler, registers declaration, and runs the GLib main loop.

## Read Data stream

1. Build the ACAP application and install it on your camera.
2. Start the application
3. Use **Gstreamer/Axis Metadata Monitor/MQTT** to subscribe to the event

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendPulse/axis:SendPulseEvent" ! fdsink

```
4. Observer events with data payload:

```xml

    <?xml version="1.0" encoding="UTF-8"?>
    <tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
        <tt:Event>
            <wsnt:NotificationMessage xmlns:tns1="http://www.onvif.org/ver10/topics" 
                                    xmlns:tnsaxis="http://www.axis.com/2009/event/topics" 
                                    xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" 
                                    xmlns:wsa5="http://www.w3.org/2005/08/addressing">
                <wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendPulse/SendPulseEvent</wsnt:Topic>
                <wsnt:ProducerReference>
                    <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
                </wsnt:ProducerReference>
                <wsnt:Message>
                    <tt:Message UtcTime="2025-08-16T18:26:08.676764Z">
                        <tt:Source></tt:Source>
                        <tt:Data>
                            <tt:SimpleItem Name="value" Value="10"/>
                        </tt:Data>
                    </tt:Message>
                </wsnt:Message>
            </wsnt:NotificationMessage>
        </tt:Event>
    </tt:MetadataStream>

```


6. Check if application send pulse is listed when creating an event

![Event settings](./pulse_event_listed.png)

7. Check the available actions. You will see that while is active actions are greyed out due to require state.

![Actions listing](./event_stateless_actions.png)

## Build

```bash
docker build --build-arg ARCH=aarch64 --tag pulse-send-stless .
```

```bash
docker cp $(docker create pulse-send-stless):/opt/app ./build
```

## Read Data stream

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:PulseSendStless/axis:PulseID" ! fdsink

```