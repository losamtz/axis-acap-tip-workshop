# Stateless Event

## Pulse event (Source Code Example #1)
Pulse (a.k.a. stateless) events are typically used to trigger an action, either in the camera or in a VMS.
The example #1 describes the standard way to declare and fire these events.

Example 1 declaration:                  Note: Namespaces and unnecessary tags removed for readability

```xml
<TopicSet>
    <CameraApplicationPlatform>
        <SendData NiceName="Send Data">
            <SendDataEvent topic="true" NiceName="Send Data Evemt">
                <MessageInstance>
                    <DataInstance>
                        <SimpleItemInstance Type="string" Name="value"/>
                    </DataInstance>
                </MessageInstance>
            </SendDataEvent>
        </SendData>
    </CameraApplicationPlatform>
. . .
</TopicSet>
```

## Build

```bash
docker build --build-arg ARCH=aarch64 --tag send-data .
```

```bash
docker cp $(docker create send-data):/opt/app ./build
```

## Read Data stream

```bash
gst-launch-1.0 rtspsrc location="rtsp://root:pass@192.168.0.90/axis-media/media.amp?video=0&audio=0&event=on&eventtopic=axis:CameraApplicationPlatform/axis:SendData/axis:SendDataEvent" ! fdsink

```