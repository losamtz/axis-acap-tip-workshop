#pragma once
#include <axsdk/axevent.h>
#include <stdbool.h>

typedef struct {
    AXEventHandler* handler;
    guint declaration;
    bool ready;
    bool sent;
    bool active;
    bool available;
} AlarmEvent;

bool alarm_event_init(AlarmEvent* event);
bool alarm_event_update(AlarmEvent* event, bool active, bool available);
void alarm_event_destroy(AlarmEvent* event);
