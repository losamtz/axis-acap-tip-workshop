## How it works

MDB path: mdb_connection_create → mdb_subscriber_config_create → mdb_subscriber_create_async.
When a message arrives, on_message fires with the ADF payload and a monotonic timestamp.

Bridge: The callback wraps each payload into a small JSON envelope { topic, source, timestamp, frame } and calls broadcast_text().

WebSocket: CivetWeb keeps a list of all connected /ws clients. Each received message is pushed via mg_websocket_write(..., TEXT, body, len).

UI: index.html connects to the same origin (ws://<host>:2001/ws), shows basic metadata, and pretty-prints the frame/adf content (or adf_raw if parsing failed).


## 

```bash

docker build --tag websocket-mdb --build-arg ARCH=aarch64 .
```

```bash

docker cp $(docker create websocket-mdb):/opt/app ./build

```