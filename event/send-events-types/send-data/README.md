# Event API – Send Data Sample

This sample demonstrates how to **declare and send custom data events** using the Axis **Event API**.

It periodically simulates system metrics such as CPU temperature, load, used memory, and free memory, then sends them as an event through the ACAP Event framework.
Other applications or external clients (e.g. MQTT subscribers, VAPIX event stream, or Axis Event Log) can subscribe to these events.

## Theory

- **Event declaration**
Events must first be declared with a topic hierarchy and data fields. In this sample:

  ```
    tnsaxis:topic0 = CameraApplicationPlatform
    tnsaxis:topic1 = SendData
    tnsaxis:topic2 = SendDataEvent

  ```
Each event carries user-defined key/value pairs (Temperature, Load, UsedMemory, FreeMemory).

- **Stateless events**
This sample declares the event as stateless (1), meaning each fired event is independent and does not represent an ongoing state.

- **User-defined data**
Keys are marked with isApplicationData so that they are treated as application-specific metrics. They will not appear in the event rule.

- **Event loop**
A GLib main loop schedules periodic event generation every 3 seconds using g_timeout_add_seconds.

## Code Walkthrough

- setup_declaration()
Declares the event and defines the topic hierarchy and data fields.

- declaration_complete()
Callback when declaration succeeds. Sets initial values and starts the periodic event timer.

- generate_random_data()
Simulates random metrics:

    - Temperature: 20.0–80.0 °C
    - Load: 0.0–4.0
    - Memory: used + free, total 500 MB

- send_data()
Creates an event, adds key/value pairs, and sends it through ax_event_handler_send_event.

- main()
Initializes syslog, seeds RNG, creates the event handler, registers declaration, and runs the GLib main loop.

## Read Data stream

1. Build the ACAP application and install it on your camera.
2. Start the application
3. Use **Gstreamer/Axis Metadata Monitor/MQTT** to subscribe to the event

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendData/axis:SendDataEvent" ! fdsink

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
                <wsnt:Topic Dialect="http://docs.oasis-open.org/wsn/t-1/TopicExpression/Simple">tnsaxis:CameraApplicationPlatform/SendData/SendDataEvent</wsnt:Topic>
                <wsnt:ProducerReference>
                <wsa5:Address>uri://834f16ae-0f06-437c-8d04-2ad363dfc88d/ProducerReference</wsa5:Address>
                </wsnt:ProducerReference>
                    <wsnt:Message>
                        <tt:Message UtcTime="2025-08-16T15:54:57.179581Z">
                            <tt:Source>
                            </tt:Source>
                            <tt:Data>
                                <tt:SimpleItem Name="FreeMemory" Value="333"/>
                                <tt:SimpleItem Name="UsedMemory" Value="167"/>
                                <tt:SimpleItem Name="Temperature" Value="70.580000"/>
                                <tt:SimpleItem Name="Load" Value="2.690000"/>
                            </tt:Data>
                        </tt:Message>
                    </wsnt:Message>
            </wsnt:NotificationMessage>
        </tt:Event>
    </tt:MetadataStream>

```
5. Build and compile sample subscribe-event-data to subscribe to SendDataEvent

![Log from running subcription app](./log_subscribe_send_data.png)

6. Check if application send data is listed when creating an event

![Event settings](./send_data_not_listed.png)


## Build

```bash
docker build --build-arg ARCH=aarch64 --tag send-data .
```

```bash
docker cp $(docker create send-data):/opt/app ./build
```

