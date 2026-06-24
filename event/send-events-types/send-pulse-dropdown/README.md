# Send Pulse Dropdown Event

This example extends the pulse event by declaring a source value. In the camera rule interface, that source can be shown as a selectable option, which is why this example is useful after the basic pulse example.

## Concept

```mermaid
flowchart TD
    App[Application] --> Declare0[Declare event for option 0]
    App --> Declare1[Declare event for option 1]
    App --> DeclareN[Declare event for more options]
    User[Rule configuration] --> Select[Select source value]
    App --> Send[Send matching event id]
    Send --> Rule[Rule matches selected source]
```

The source value is part of the event identity. It is not just extra payload.

## Key Code

The example creates several event declarations, one per selectable value.

```c
ax_event_key_value_set_add_key_value(key_value_set, "value", NULL, value,
                                     AX_VALUE_TYPE_INT, NULL);
ax_event_key_value_set_mark_as_source(key_value_set, "value", NULL, NULL);
```

Marking a key as source tells the event system that this value participates in selection and filtering.

```c
ax_event_handler_declare(event_handler, key_value_set, 1, &declaration,
                         (AXDeclarationCompleteCallback)declaration_complete,
                         value, NULL);
```

At runtime, the application sends the event id that corresponds to the current option.

```c
ax_event_handler_send_event(event_handler, declaration, key_value_set, NULL);
```
## Test

Check payload:

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendPulseDropDown/axis:SendPulseDropDownEvent" ! fdsink

```

It should look like this:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
    <tt:Event>
        <wsnt:NotificationMessage xmlns:tns1="http://www.onvif.org/ver10/topics" xmlns:tnsaxis="http://www.axis.com/2009/event/topics" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:wsa5="http://www.w3.org/2005/08/addressing"><wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendPulseDropDown/SendPulseDropDownEvent</wsnt:Topic>
        <wsnt:ProducerReference>
            <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
        </wsnt:ProducerReference>
            <wsnt:Message>
                <tt:Message UtcTime="2025-08-17T05:07:54.678151Z" PropertyOperation="Changed">
                    <tt:Source></tt:Source>
                    <tt:Key></tt:Key>
                    <tt:Data>
                        <tt:SimpleItem Name="value" Value="80"/>
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
docker build --tag send-pulse-dropdown --build-arg ARCH=aarch64 .
docker cp $(docker create send-pulse-dropdown):/opt/app ./build
```

## Classroom Exercises

1. Reduce the number of options and inspect the rule UI.
2. Rename the source key from `"value"` to something domain specific.
3. Compare this example with `send-data` and discuss source keys versus data keys.
