# Event API – Send Pulse Dropdown (stateless)

Stateless source-based event generator for ACAP.
This sample declares multiple event sources (values 0,10,20,…,100) under the same topic. Because the value is declared as a source, the Axis Action Rule UI shows a dropdown so you can filter on a specific value without typing free-text.

## What this app does

- Declares 11 stateless events that only differ by the source key value ∈ {0,10,…,100}:

```
topic0 = tnsaxis:CameraApplicationPlatform
topic1 = tnsaxis:SendPulseDropDown
topic2 = tnsaxis:SendPulseDropDownEvent

value (INT) is marked as source (appears as dropdown in the UI)
```

- After all declarations, starts a GLib timer that every 5 seconds sends an event using the next declared source (0→100→wrap).

- For observability, each send also puts the same integer as a data key named value inside the event payload (optional but handy for logs/MQTT/RTSP metadata).

In this sample value is marked both as source and data at declaration time. That’s fine for demos. In production, pick one semantic (source or data) to avoid confusion.

## Code Walkthrough

- Precompute the values

```c
for (int i = 0; i < MAX_DECLARATIONS; ++i) {
  app_data->values[i] = i * 10;   // 0..100
}

```
- setup_declaration() - Declare one event per value (source)

```c
ax_event_key_value_set_add_key_value(kv, "value", NULL, value, AX_VALUE_TYPE_INT, NULL);
ax_event_key_value_set_mark_as_source(kv, "value", NULL, NULL);
ax_event_key_value_set_mark_as_data  (kv, "value", NULL, NULL); // optional demo choice
ax_event_handler_declare(handler, kv, /*stateless=*/1, &declId,
                         declaration_complete, /*user data*/ value, NULL);
```

- Start the periodic sender

```c
static void declaration_complete(guint decl, guint* value) {
  if (*value == 0) { // start once after the 0-source is registered
    app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_event, app_data);
  }
}

```
- Send using the matching declaration id

```c
static gboolean send_event(AppData* d) {
  guint i = d->value_index;
  guint value = d->values[i];
  guint event_id = d->event_ids[i];

  AXEventKeyValueSet* kv = ax_event_key_value_set_new();
  ax_event_key_value_set_add_key_value(kv, "value", NULL, &value, AX_VALUE_TYPE_INT, NULL); // data (optional)
  AXEvent* ev = ax_event_new2(kv, NULL);
  ax_event_key_value_set_free(kv);

  ax_event_handler_send_event(d->event_handler, event_id, ev, NULL);
  ax_event_free(ev);

  d->value_index = (i + 1) % MAX_DECLARATIONS; // cycle 0..10
  return TRUE; // keep timer
}
```
Sending with event_id ties the event to the declared source value for routing and rule dropdowns. Adding a data value mirrors the same number inside the payload for telemetry.

- main()
Initializes syslog, creates the event handler, registers declaration, and runs the GLib main loop.

## Read Data stream

1. Build the ACAP application and install it on your camera.
2. Start the application
3. Use **Gstreamer/Axis Metadata Monitor/MQTT** to subscribe to the event

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendPulseDropDown/axis:SendPulseDropDownEvent" ! fdsink

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
                <wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendPulseDropDown/SendPulseDropDownEvent<wsnt:Topic>
                <wsnt:ProducerReference>
                    <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference<wsa5:Address>
                </wsnt:ProducerReference>
                <wsnt:Message>
                    <tt:Message UtcTime="2025-08-16T21:41:50.674465Z">
                        <tt:Source>
                            <tt:SimpleItem Name="value" Value="50"/>
                        </tt:Source>
                        <tt:Data></tt:Data>
                    </tt:Message>
                </wsnt:Message>
            </wsnt:NotificationMessage>
        </tt:Event>
    </tt:MetadataStream>


```


6. Check if application send pulse is listed when creating an event

![Event settings](./pulse_drop_down.png)

7. Check the available actions. You will see that "while is active" actions are greyed out due to require state.

![Actions listing](./action_stateless.png)

8. Action Rule (UI)

Create a rule:

- When: Event → CameraApplicationPlatform / SendPulseDropDown / SendPulseDropDownEvent
- Filter: Dropdown Value → pick e.g. 50

![Actions listing](./dropdown.png)

- Then: check Log, MQTT publish, create a text overlay, etc...

You’ll see the rule trigger only when the app’s cycle reaches that value.

## Build

```bash
docker build --build-arg ARCH=aarch64 --tag pulse-send-stless .
```

```bash
docker cp $(docker create pulse-send-stless):/opt/app ./build
```

