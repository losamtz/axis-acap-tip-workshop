# Event API – Send State (statefull)

**Property-state (stateful) event generator for ACAP**

This sample declares and periodically toggles a stateful event (a property state), where the boolean key active represents the current state (0/1). Because the event is stateful (declare with stateful=0), the system keeps track of the last value and subscribers receive the current state immediately upon subscription, then on each change.

## What this app does

- Declares one stateful event with topic:

```
topic0 = tnsaxis:CameraApplicationPlatform
topic1 = tnsaxis:SendStateWithData
topic2 = tnsaxis:SendStateWithDataEvent
```
- Adds one data key:

    - active (BOOL) — the property’s current state

- After declaration completes, starts a GLib timer that every 5 seconds toggles active (0→1→0→…)

- Sends the event with the updated active value on each tick

## Code walkthrough

**Types & globals**

```c
typedef struct {
    AXEventHandler* event_handler;
    guint event_id;
    guint timer;
    guint state_value; // 0/1
} AppData;

static AppData* app_data = NULL;
```

**Declaration (stateful)**

```c
ax_event_key_value_set_add_key_value(kv, "active", NULL, &start_value, AX_VALUE_TYPE_BOOL, NULL);
ax_event_key_value_set_mark_as_data(kv, "active", NULL, NULL);

// Declare as STATEFUL: the third arg to declare is 0 (stateful), 1 would be stateless
ax_event_handler_declare(handler, kv, /*stateful=*/0, &declaration,
                         declaration_complete, &start_value, NULL);

```

- Stateful means the system keeps the current active value.
- Consumers/subscribers get an immediate callback with the current state when they subscribe.

**Callback after declaration**

```c
static void declaration_complete(guint declaration, int* value) {
    app_data->state_value = *value;
    app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_event, app_data);
}
```

- Save initial state and start the periodic toggle sender.

**Sending the state updates**

```c
static gboolean send_event(AppData* d) {
    AXEventKeyValueSet* kv = ax_event_key_value_set_new();
    AXEvent* ev = NULL;

    ax_event_key_value_set_add_key_value(kv, "active", NULL, &d->state_value, AX_VALUE_TYPE_BOOL, NULL);
    ev = ax_event_new2(kv, NULL);
    ax_event_key_value_set_free(kv);

    if (!ax_event_handler_send_event(d->event_handler, d->event_id, ev, NULL))
        LOG_ERROR("Could not fire event\n");
    ax_event_free(ev);

    d->state_value = !d->state_value;   // toggle 0/1
    return TRUE;                        // keep timer running
}
```


## LAB: 

1. Build your app. Instruction under **build**
2. Install the produced ACAP package on the camera (Web UI → Apps → Upload & Install).
3. Start the app from the camera Apps page.


**Action Rule (UI)**

4. Create a rule:

- When: Event → CameraApplicationPlatform / SendState / SendStateEvent
- Filter: active (by default)
- Then: choose an action text overlay while rule is active

![Event config](./event_state_overlay.png)

5. Check paylad:

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendStateWithData/axis:SendStateWithDataEvent" ! fdsink

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

![Triggered state](./alarm_overlay.png)

## How this differs from a stateless pulse

- Stateful (this sample): there is always a current value. Subscribers receive the current state immediately and on each change.

- Stateless (pulse): has no memory — callbacks only fire when you send a pulse.

Pick **stateful** for things like “while rule is active”.


## Notes

- The event key active is marked as data because it represents a state payload.
- For source-style dropdowns, you would declare distinct source values, but that’s not the property-state pattern demonstrated here.

## Build

```bash
docker build --build-arg ARCH=aarch64 --tag send-state-data .
```

```bash
docker cp $(docker create send-state-data):/opt/app ./build
```
