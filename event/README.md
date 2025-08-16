# Event API Samples

This folder contains ACAP examples that demonstrate how to **send and subscribe to Axis events** using the Axis Event API.  
Events are a core mechanism in the ACAP framework to communicate state changes, data, or signals between applications and services on the device.  

The samples here cover different **event types** (`data`, `pulse`, `pulse with dropdown`, `state`) and one **subscription example** that consumes published event data.  

---

## Folder Content

### Event Senders

These samples show how to publish different event types from an ACAP application:

- **send-data**  
  Sends a **data event** with arbitrary key/value payloads (e.g. `"temperature": "42"`).  
  Useful when you want to propagate sensor values or structured data to other apps or clients.

- **send-pulse**  
  Sends a **pulse event**, representing a short-lived trigger.  
  Often used to signal a single occurrence (e.g. motion detected).

- **send-pulse-dropdown**  
  A variation of `send-pulse` that includes a **dropdown option**, allowing the event to carry one of several predefined values.  
  Example: trigger an event that specifies the "cause" or "source" from a predefined list.

- **send-state**  
  Sends a **state event**, which maintains its last known value.  
  Useful for representing conditions such as `"door=OPEN"` / `"door=CLOSED"` or `"system=ACTIVE"`.

---

### Event Subscriber

- **subscribe-to-event-data**  
  A companion example that **listens for `send-data` events** and prints/acts on the received payload.  
  Demonstrates how to integrate event subscriptions inside an ACAP application.

---

## Objectives

By working through these labs, you will learn:

- How to define and send different types of Axis events in an ACAP app.  
- The differences between **data**, **pulse**, and **state** event types.  
- How to add additional metadata (dropdowns, payload values) to events.  
- How to subscribe to and process events published by other ACAP applications.  

---

## Next Steps

1. Start with **send-data** to understand basic event publishing.  
2. Experiment with **send-pulse** and **send-pulse-dropdown** to see different trigger semantics.  
3. Use **send-state** for persistent condition tracking.  
4. Run **subscribe-to-event-data** in parallel with **send-data** to observe the event flow.

---

For more details, see the [Axis Event API documentation](https://developer.axis.com/acap).  
