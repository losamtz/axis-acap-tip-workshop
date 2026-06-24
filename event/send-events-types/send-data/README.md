# Send Data Event

This example publishes a stateless event that carries data values. It is the best first Event API example because the event has no lasting state: every message is a new observation.

## What this example teaches

- How an ACAP application declares an event topic.
- How event fields are split into topic keys and data keys.
- How to publish a repeated event from a GLib timer.
- How subscribers receive structured values instead of parsing text.

## Code Flow

```mermaid
sequenceDiagram
    participant App as send_data
    participant Event as AXEventHandler
    participant Rule as Event System / Rules

    App->>Event: ax_event_handler_new()
    App->>App: Create key/value set
    App->>Event: ax_event_handler_declare()
    Event-->>App: event_id
    loop every 3 seconds
        App->>App: Read simulated values
        App->>Event: ax_event_handler_send_event()
        Event->>Rule: Publish event with data payload
    end
```

The application creates one event declaration and then reuses that declaration when sending data.

```c
ax_event_key_value_set_add_key_value(key_value_set, "topic0", "tnsaxis",
                                     TOPIC0_TAG, AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(key_value_set, "topic1", "tnsaxis",
                                     TOPIC1_TAG, AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(key_value_set, "topic2", "tnsaxis",
                                     EVENT_TAG, AX_VALUE_TYPE_STRING, NULL);
```

The topic fields identify the event. Data fields are values attached to each event instance.

```c
ax_event_key_value_set_add_key_value(key_value_set, "Temperature", NULL, &temperature,
                                     AX_VALUE_TYPE_DOUBLE, NULL);
ax_event_key_value_set_mark_as_data(key_value_set, "Temperature", NULL, NULL);
```

After declaration, the timer callback sends a new event instance.

```c
g_timeout_add_seconds(3, send_data, app_data);
```

Check payload:

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendData/axis:SendDataEvent" ! fdsink

```

It should look like this:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
    <tt:Event>
        <wsnt:NotificationMessage xmlns:tns1="http://www.onvif.org/ver10/topics" xmlns:tnsaxis="http://www.axis.com/2009/event/topics" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:wsa5="http://www.w3.org/2005/08/addressing"><wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendData/SendDataEvent</wsnt:Topic>
        <wsnt:ProducerReference>
            <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
        </wsnt:ProducerReference>
            <wsnt:Message>
                <tt:Message UtcTime="2025-08-17T05:07:54.678151Z" PropertyOperation="Changed">
                    <tt:Source></tt:Source>
                    <tt:Key></tt:Key>
                    <tt:Data>
                        <tt:SimpleItem Name="FreeMemory" Value="138"/><tt:SimpleItem Name="Load" Value="3.300000"/><tt:SimpleItem Name="Temperature" Value="57.820000"/><tt:SimpleItem Name="UsedMemory" Value="362"/>
                    </tt:Data>
                </tt:Message>
            </wsnt:Message>
        </wsnt:NotificationMessage>
    </tt:Event>
</tt:MetadataStream>
```

You’ll see it trigger every time the state flips to the filtered value.

## Build

From this directory:

```sh
docker build --tag send-data --build-arg ARCH=aarch64 .
docker cp $(docker create send-data):/opt/app ./build
```

Install the generated `.eap` file on the camera and start the application from the web UI or VAPIX.

## Classroom Exercises

1. Add one more data field, such as `"FrameRate"`.
2. Change the timer interval and observe the event rate.
3. Pair this example with `subcribe-event-data` and verify that the subscriber receives the new field.
