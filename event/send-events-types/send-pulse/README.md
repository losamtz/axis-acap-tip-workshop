# Send Pulse Event

This example publishes a stateless pulse event. A pulse is useful for "something happened" signals, such as a detected object, a button press, or a threshold crossing.

## Concept

A pulse event does not represent a current condition. It represents an instant.

```mermaid
flowchart LR
    Timer[Timer fires] --> Build[Build event payload]
    Build --> Send[Send pulse event]
    Send --> Rule[Rule engine sees one occurrence]
```

The example still attaches a small value to make the event easy to inspect, but the important part is that each call to `ax_event_handler_send_event()` is an occurrence.

## Key Code

The topic names place the event under the application namespace:

```c
ax_event_key_value_set_add_key_value(key_value_set, "topic0", "tnsaxis",
                                     TOPIC0_TAG, AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(key_value_set, "topic1", "tnsaxis",
                                     TOPIC1_TAG, AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(key_value_set, "topic2", "tnsaxis",
                                     EVENT_TAG, AX_VALUE_TYPE_STRING, NULL);
```

The timer callback sends the pulse repeatedly:

```c
g_timeout_add_seconds(5, send_pulse, app_data);
```

The event is declared once and sent many times:

```c
ax_event_handler_declare(event_handler, key_value_set, 1, &declaration,
                         (AXDeclarationCompleteCallback)declaration_complete,
                         &start_value, NULL);
```
Check payload:

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendPulse/axis:SendPulseEvent" ! fdsink

```

It should look like this:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
    <tt:Event>
        <wsnt:NotificationMessage xmlns:tns1="http://www.onvif.org/ver10/topics" xmlns:tnsaxis="http://www.axis.com/2009/event/topics" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:wsa5="http://www.w3.org/2005/08/addressing"><wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendPulse/SendPulseEvent</wsnt:Topic>
        <wsnt:ProducerReference>
            <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
        </wsnt:ProducerReference>
            <wsnt:Message>
                <tt:Message UtcTime="2025-08-17T05:07:54.678151Z" PropertyOperation="Changed">
                    <tt:Source></tt:Source>
                    <tt:Key></tt:Key>
                    <tt:Data>
                        <tt:SimpleItem Name="active" Value="10"/>
                    </tt:Data>
                </tt:Message>
            </wsnt:Message>
        </wsnt:NotificationMessage>
    </tt:Event>
</tt:MetadataStream>
```

You’ll see it trigger every time the state flips to the filtered value.

## Build

```sh
docker build --tag send-pulse --build-arg ARCH=aarch64 .
docker cp $(docker create send-pulse):/opt/app ./build
```

## Classroom Exercises

1. Change the event interval from 5 seconds to 1 second.
2. Remove the data value and make the event a pure pulse.
3. Create an event rule on the camera that reacts to this pulse.
