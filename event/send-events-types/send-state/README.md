# Send State Event

This example publishes a stateful event. A state event represents a condition that can be active or inactive.

## Pulse Versus State

```mermaid
flowchart LR
    Pulse[Pulse event] --> PulseMeaning[An occurrence happened]
    State[State event] --> StateMeaning[A condition is currently true or false]
```

Use state events for conditions such as "motion active", "object present", or "temperature above threshold".

## Code Flow

```mermaid
sequenceDiagram
    participant App as send_state
    participant Event as AXEventHandler
    participant Rule as Rule Engine

    App->>Event: Declare state event
    loop timer
        App->>App: Toggle active value
        App->>Event: Send active=true or active=false
        Event->>Rule: Update condition state
    end
```

## Key Code

The state key is normally named `active`.

```c
gboolean active = FALSE;
ax_event_key_value_set_add_key_value(key_value_set, "active", NULL, &start_value,
                                     AX_VALUE_TYPE_BOOL, NULL);
```

The event is declared as stateful by the declaration arguments used by the example.

```c
ax_event_handler_declare(event_handler, key_value_set, 0, &declaration,
                         (AXDeclarationCompleteCallback)declaration_complete,
                         &start_value, NULL);
```

The timer toggles and publishes the current state.

```c
start_value = !start_value;
ax_event_handler_send_event(event_handler, declaration, key_value_set, NULL);
```


Check payload:

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendState/axis:SendStateEvent" ! fdsink

```

It should look like this:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
    <tt:Event>
        <wsnt:NotificationMessage xmlns:tns1="http://www.onvif.org/ver10/topics" xmlns:tnsaxis="http://www.axis.com/2009/event/topics" xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" xmlns:wsa5="http://www.w3.org/2005/08/addressing"><wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendState/SendStateEvent</wsnt:Topic>
        <wsnt:ProducerReference>
            <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
        </wsnt:ProducerReference>
            <wsnt:Message>
                <tt:Message UtcTime="2025-08-17T05:07:54.678151Z" PropertyOperation="Changed">
                    <tt:Source></tt:Source>
                    <tt:Key></tt:Key>
                    <tt:Data>
                        <tt:SimpleItem Name="active" Value="1"/>
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
docker build --tag send-state --build-arg ARCH=aarch64 .
docker cp $(docker create send-state):/opt/app ./build
```



## Classroom Exercises

1. Change the timing so the active state lasts longer than the inactive state.
2. Add logging that prints every state transition.
3. Discuss why a state event is easier for rules than a repeated pulse when representing a condition.
