
## Event API – Subscribe Event Data

**Subscribe to and log data events from another ACAP app**

This sample shows how to receive events using the AXIS Event API. It subscribes to the topic hierarchy:
```
tnsaxis:CameraApplicationPlatform
tnsaxis:SendData
tnsaxis:SendDataEvent
```
…and logs the payload keys it expects from a matching producer (e.g. the send-data sample):

- Temperature (double)
- Load (double)
- UsedMemory (int)
- FreeMemory (int)

Error handling is intentionally minimal to keep the sample focused on the subscription flow.

## What this app does

1. Creates an AXEventHandler.
2. Builds a subscription filter with the topic keys above.
3. Calls ax_event_handler_subscribe(...) and registers subscription_callback.
4. Runs a GMainLoop so callbacks can be delivered.
5. In each callback, it extracts values from the event’s AXEventKeyValueSet and logs them.

## Code highlights

**Subscription filter**

```c
AXEventKeyValueSet* kv = ax_event_key_value_set_new();

ax_event_key_value_set_add_key_value(kv, "topic0", "tnsaxis",
                                     "CameraApplicationPlatform",
                                     AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(kv, "topic1", "tnsaxis",
                                     "SendData",
                                     AX_VALUE_TYPE_STRING, NULL);
ax_event_key_value_set_add_key_value(kv, "topic2", "tnsaxis",
                                     "SendDataEvent",
                                     AX_VALUE_TYPE_STRING, NULL);

ax_event_handler_subscribe(handler, kv, &subscription,
                           (AXSubscriptionCallback)subscription_callback,
                           NULL, NULL);
```

**Callback: extract payload**

```c
static void subscription_callback(guint subscription, AXEvent* event, gpointer* userdata) {
    (void)subscription; (void)userdata;

    const AXEventKeyValueSet* kv = ax_event_get_key_value_set(event);

    gdouble temperature = 0.0, load = 0.0;
    gint used_memory = 0, free_memory = 0;

    ax_event_key_value_set_get_double(kv, "Temperature", NULL, &temperature, NULL);
    ax_event_key_value_set_get_double(kv, "Load", NULL, &load, NULL);
    ax_event_key_value_set_get_integer(kv, "UsedMemory", NULL, &used_memory, NULL);
    ax_event_key_value_set_get_integer(kv, "FreeMemory", NULL, &free_memory, NULL);

    syslog(LOG_INFO, "Temperature: %f C", temperature);
    syslog(LOG_INFO, "Load: %f", load);
    syslog(LOG_INFO, "Used Memory: %d (MB)", used_memory);
    syslog(LOG_INFO, "Free Memory: %d (MB)", free_memory);

    ax_event_free(event); // free the event (do NOT free kv)
}
```

**Main loop**

```c
GMainLoop* loop = g_main_loop_new(NULL, FALSE);
g_main_loop_run(loop);

```
## Lab

1. Install the produced ACAP package on the camera (Web UI → Apps → Upload & Install).
2. Start the app from the camera Apps page.
3. For a live test, also install and run the matching producer (send-data) on the same camera.

Verify it works
4. Watch logs    Apps > acap > app log

You should see lines similar to:
```
subscribe-event-data: Started logging from subscribe event application
subscribe-event-data: Temperature: 72.830000 C
subscribe-event-data: Load: 1.120000
subscribe-event-data: Used Memory: 223 (MB)
subscribe-event-data: Free Memory: 277 (MB)
```

5. Confirm topic match

Make sure the producer uses exactly:

```
topic0 = tnsaxis:CameraApplicationPlatform
topic1 = tnsaxis:SendData
topic2 = tnsaxis:SendDataEvent
```

If these differ, your subscription won’t receive anything.


## Extra


## Lab 1 — Add a new key

- Modify your producer to also send a new key, e.g. Hostname as AX_VALUE_TYPE_STRING.
- In the subscriber callback, call

```c
ax_event_key_value_set_get_string(kv, "Hostname", NULL, &str, NULL)
```
and log it.

- Test: See the hostname printed in subscriber logs.

## Lab 2 — Narrow the subscription

- Add a data filter to your subscription KVS, e.g. subscribe only when UsedMemory > 250.
(Since subscription matching is equality-based, simulate this by sending a dedicated HighMem boolean from the producer and filtering on HighMem=1.)

- Test: Events only show up when the producer sets HighMem=1.

## ab 3 — Multiple subscriptions

- Create two independent subscriptions:
    - One to SendDataEvent
    - One to your other event (e.g., SendPulseEvent)

- Log which subscription id fired in the callback for clarity.
- Test: Trigger both producer apps and see both streams of logs.

## Lab 4 — Robust error handling

- Add checks for the return values of all Event API calls.
- If ax_event_handler_subscribe fails, log the error and exit cleanly.
- Test: Temporarily break a topic string and confirm the app reports a clear error.