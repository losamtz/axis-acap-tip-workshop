# Events

The event system make use of event bus via gdbus where applications using axevent library, produce or consumes events.

**Event Producer**
A process (e.g., com.axis.Event) **emits/sends** a signal on the D-Bus system bus.
Example: Motion detection, port input change, analytics events.

**D-Bus Bus (system)**
The dbus-daemon handles message routing between clients.

**Event Consumer**
Your application connects to the system bus (via gdbus in GLib), **subscribes** to a bus name, object path, and interface, and receives events in a callback.

```

[Producer 1] \
              \
[Producer 2] ---->  [ D-Bus Bus ] ----> [Consumer 1]
              /
[Producer 3] /

```

You can consume the events through rtsp which carries metadata, by mqtt connecting a client (subscriber) to a message broker and through Event VAPIX API subscription.


#### Event Bus Flow (GDBus)

+-------------------+              +-------------------+
|   Event Producer  |              |   Event Consumer  |
| (Axis service,    |              |  (Your ACAP app)  |
|  ONVIF, Declared  |              |                   |
|event in your ACAP)|              |                   |
+--------+----------+              +---------+---------+
         |                                     ^
         | emits D-Bus signal                  |
         v                                     |
+--------+-------------------------------------+---------+
|                   D-Bus Bus                             |
| (system bus via gdbus / dbus-daemon)                     |
| - Handles connections                                    |
| - Routes signals by bus name & object path               |
| - Delivers method calls and signal broadcasts            |
+----------------------------------------------------------+
         |                                     ^
         |                                     |
         v                                     |
+--------+----------+              +-----------+---------+
|   GDBusProxy /    |              |  g_signal_connect() |
|   GDBusConnection |-------------->  Signal handler     |
|   (GLib binding)  |              |  parses args, etc.  |
+-------------------+              +---------------------+

### Event Topics

Different services and applications in the camera can trigger events, therefore a topological structure of the topic is used to differentiate between the source of the event. Events are declared with a topology of names and namespaces. There are two namespaces, tnsaxis which is Axis specific and tns1 which is defined in the open ONVIF standard.

As is mentioned in the documentation for the example application send_event, tnsaxis-events are visible in the device webpage. When declaring Axis events in an ACAP, the topic0 value should be set to CameraApplicationPlatform.

#### Namespaces

It is mandatory to set a namespace when declaring events. Even though it is possible to create new
namespaces, an ACAP service should always use the Axis namespace “tnsaxis”. You can also use ONVIF name space “tns1”.

#### Special Attributes

There are special attributes a declaration tag may have.

**isPropertyState=”true”** tells applications that the event is stateful. The event has a bool property in
DataInstance that defines the active/inactive state.

**isApplicationData=”true”** tells applications that the event or its data is intended for a specific
application/service. The event is not intended to trigger generic action rules and the data is not intended
to be used as an action rule filter. A well-behaved client should not display these events or data to the
user.

**isDeprecated=”true”** tells applications that the event (although still functional) may be removed in
future versions. Existing action rules will continue to work, but it is not a good idea to add new action
rules. The service may offer alternative events that should be used instead. A well-behaved client should
not display the event to users.


### Data Contained in an Event

A camera event can be used to indicate the happening of a specific event, but it can also contain extra information within the event structure. Depending on how the event has been declared, this information can be used in different ways.

### Event types
There are three types of event, and it is important to understand their purposes.

- **Stateless** (aka Pulse) – An event that indicates that something has occurred. Typically used to
trigger action rules.
- **Stateful** (aka State) – An event with an active and inactive state. Typically used in action rules
such as “record while active”.
- **Data** (aka Application Data) – An event that includes application specific data that needs to be
processed by the consuming application. A data event is normally not used to trigger generic
action rules.


#### Stateless Vs. Stateful Events

There are two types of events: stateless and stateful. Both types can contain data items of type boolean, double, integer or string.

Stateful events must always have a value on each metadata item, i.e. they are initialized with a default value. The triggering of a stateless event is only a notification, while triggering a stateful event marks a change in its value, i.e. the stateful event has a value at all times. Both stateless and stateful events can be used to trigger actions; however, unlike stateless events, stateful ones also provide the option to configure an action to be active while the event itself is active, e.g.: enable an LED light (action) while an audio clip is playing (stateful event). When setting up an action rule, the UI will show a checkbox indicating if the event should be used as a trigger or not, which dictates if the action can be set to be active as long as the event itself is active. This is always checked when using stateless events as shown in the pictures below.

When an application subscribes to an event, if the event is stateful, then the consumer will always get a callback with the current values. For stateless events the first callback is received first when the event is triggered.
When using the axevent library, the type of event is specified when calling the function ax_event_handler_declare. The third input, of boolean type, will determine if the event is stateless (true) or stateful (false).

#### Data Items in the Events

When adding new items to a key-value-store in an event, a data item can be marked as: 

    • Data (ax_event_key_value_set_mark_as_data) 
    • Source (ax_event_key_value_set_mark_as_source)
    • User-specified tags 

Items marked as either data or as a source will show in the XML from the RTSP stream. They will also be visible in the Axis camera's UI when creating an event rule and will be included in the MQTT when forwarding events. 

Data items are shown as a free text field in the rule engine UI since the possible values are not known ahead of time. The user might input a specific value which filters the events or leave it blank (include all). 

Event source values are specified when declaring the events (one event declaration needs to be done for each possible value), this means that all values are known and a drop-down can be used instead of a free-text field. The source field should not be specified when triggering an event, the event will always use the event source it was declared with.

According to the Axis documentation, an event must include exactly one item marked as data and no or one item marked as source:

Please note that although it is possible to mark more than one key as a source, only events with zero or one source keys can be used to trigger actions.

Please note that although it is possible to mark more than one key as data, only events with one and only one data key can be used to trigger actions.

Note: In later versions of AXIS OS is using multiple items marked as data 

```xml

<wsnt:Message>
    <tt:Message UtcTime="2023-04-04T15:10:28.554806Z">
        <tt:Source></tt:Source>
        <tt:Data>
            <tt:SimpleItem Name="data" Value=“[…]”/>
            <tt:SimpleItem Name="type" Value="QRCode"/>
            <tt:SimpleItem Name="time" Value="2023-04-04 17:10:28.231"/>
        </tt:Data>
    </tt:Message>
</wsnt:Message>
```
#### Data Types and Filtering

As previously mentioned, events can contain data fields of type string, integer, double, or boolean. These can be used to include additional information in the event, which can be used, among other things, to filter events when setting up action rules.

An important thing to note about the camera's event rule engine is that the behavior of boolean data types differs in the filtering from the others: a boolean data item needs to have the filter set to either true or false, and will always filter on this. Meanwhile, a string or integer data type will treat an empty field as a wildcard, i.e. no filtering. 

## Event declarations

The XML event declaration includes two sections, called SourceInstance and DataInstance. The idea
behind this structure is that the SourceInstance defines the source of the event such as video channel or
a digital input port. The DataInstance may include additional data that an event consumer may want to
use or filter on. Parsing of the data in the Source- or Data –instance may work well when applications
have hard-coded support for that specific event. However, applications/clients that support Dynamic
Event Integration may be challenged to display this information in a proper and user-friendly way. The
examples described in this document, using the camera’s event/action dialog and Axis Camera Station
examples, highlight some of these challenges.

Topic Tree placement

Xis namespace uses the Topic level 0 “CameraApplicationPlatform”. In ACAP
SDK it is possible to place the events anywhere, even create a new Topic level 0. However, it is highly
recommended to place ACAP service events under “CameraApplicationPlatform”, as some clients will
only query and display events under this topic.

```xml
<wstop:TopicSet>
    . . .
        <tnsaxis:CameraApplicationPlatform>                             Topic level 0
            <service_tag aev:NiceName="My Service Name">                Topic Level 1
                <event_tag topic="true" NiceName="My Service Event">    Topic level 2
                    <aev:SourceInstance/>
                    <aev:DataInstance/>
                </event_tag>
            </service_tag>
        </CameraApplicationPlatform >
    . . .
<wstop:TopicSet>

```

When filtering using data items, specifying a value for multiple fields will create an AND logic, i.e. all conditions need to be met simultaneously to trigger the event.

### Sending Events

When sending events using the axevent library, a GMainLoop should be running; if there is not, the function responsible for sending the event will not fail, but the events will never be sent to the bus.

Note that when triggering the event, the reference to the value must still be valid when the asynchronous worker in the GMainLoop sends the event. This means that using local variables on the stack can lead to bugs.


### Designing the Event for a Use Case

Each use case might call for specific design decisions for the events generated in an application. In this part of the lesson, we will provide tips and explain certain limitations to keep in mind when designing events for different use cases.

The first step is to know how the events will be used. When it comes to this, there are two general situations:

    • The events will be used to trigger actions from the event rule engine. 
    • The events will be consumed and parsed by a custom system. 

Each of these situations presents its own possibilities and limitations that we should design our events around.

#### Designing Events to Trigger Actions

Events generated from an ACAP application can be used in rules to trigger actions in the camera. However, as discussed above, this has its limitations when it comes to filtering. Three important things to keep in mind are:

    • It is only possible to filter by exact data field values.
    • Boolean values can't be filtered by a wildcard, so events that include a boolean data type will always be filtered for true or false.
    • All conditions added form an AND logic, meaning the action will only trigger if all conditions are met simultaneously. Using OR is not possible without declaring multiple rules.

Only being able to filter by exact data values can be a challenge in cases where we want to filter for events with a data field within a range; for example, if an event is including a number between 1 and 100 and we want an action to trigger if the value is above 50, then we would need to create 50 different rules, one for each possible integer value.

A good way to simplify this situation when designing the application that sends events is to create tags for each range we might want to filter by; for example, we might call the range between 1 and 50 "LOW", and the range between 51 and 100 "HIGH". We can implement this logic in our ACAP application and modify our event structure, so that it sends a string data type with the tag included. This would allow us to filter by events where the associated field is "LOW" or "HIGH".

Boolean types are also important to keep in mind. Since they are always filtered on, adding a boolean value to our event could limit our filtering options. Let's say we have an application that sends an event every time a person enters a room. The event has two data fields: a string field including the name of the person, and a boolean that is true if the person is an employee or false if they are not. We might want to trigger an event action when anyone enters, but since the boolean is always filtered on, we would need to add two separate event rules: one for employees and another for non-employees. This could be solved by using a string field called type which contains the string "EMPLOYEE" or "OTHER" instead. This would solve the issue since the wildcard can be used for string fields.

Lastly, when an event has multiple data fields, there is a possibility to filter on multiple values, but this will create an AND logic. If we want to have event rules that trigger on OR logic (e.g.: trigger an action when field A is 1 OR when field B is 0), we would either need to create a custom field to represent this in the event generating ACAP or create separate event rules for each individual condition.

#### Designing Events for Custom Consumers

Using a customized system to consume the events gives us the possibility of choosing how to process the information received from them. This way, we are not limited to using exact values for filtering (like in the camera's event rule engine), meaning that sending exact values might be more beneficial than working with range tags. This allows us to have more precise information and use it for further calculations or logic. This would also allow different consumers to use different threshold levels.

This implementation offers the most flexibility in how the data is handled and minimizes the adaptations needed between our raw data and the fields in the event, but it requires additional logic to trigger actions if that is needed. This can be a good option in use cases that already require an external implementation to handle events or when the events are only used as a means to share information; however, in cases where the primary goal is triggering actions in the camera it might be best to simplify the event data fields and adapt to the filters in the event rule engine.

## Summary

Understanding the intricacies of event management within Axis cameras is crucial for developers in order to build user friendly and flexible ACAPs. The distinction between stateless and stateful events, alongside the structured use of namespaces and data items, plays a significant role in how the ACAP can be integrated into larger systems.
Properly utilized, these event features allow for flexible applications that can easily be used to trigger actions without the need to implement every action in the ACAP itself. Events are also a standardized way to communicate with external systems as there are already a wealth of systems that can handle the events from Axis cameras.

