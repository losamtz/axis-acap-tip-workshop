#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "send-pulse-drop-down"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "SendPulseDropDown"
#define TOPIC1_NAME "Send Pulse Dropdown"
#define EVENT_TAG   "SendPulseDropDownEvent"
#define EVENT_NAME  "Send Pulse Drop Down Event"

#define MAX_DECLARATIONS 11  // 0 to 100 in steps of 10

typedef struct {
    AXEventHandler* event_handler;
    guint event_ids[MAX_DECLARATIONS];
    guint timer;
    guint value_index;
    guint values[MAX_DECLARATIONS];
} AppData;

static AppData* app_data = NULL;
//static guint declarations_done = 0;

static void setup_values(void) {
  for (int i = 0; i < MAX_DECLARATIONS; ++i) {
        guint value = i * 10;
        app_data->values[i] = value;
  }
}
static gboolean send_event(AppData *send_data) {

    AXEventKeyValueSet *key_value_set = NULL;
    AXEvent  *event                   = NULL;
    
    key_value_set = ax_event_key_value_set_new();

    guint value = send_data->values[send_data->value_index];
    guint event_id = send_data->event_ids[send_data->value_index];

    ax_event_key_value_set_add_key_value(key_value_set, "value", NULL, &value, AX_VALUE_TYPE_INT, NULL);

    // Create the event
    event = ax_event_new2(key_value_set, NULL);
    // The key/value set is no longer needed
    ax_event_key_value_set_free(key_value_set);

    if (!ax_event_handler_send_event(send_data->event_handler, event_id, event, NULL)) {
        LOG_ERROR("Could not fire event with value: %u\n", value);
    } else {
        LOG("Event sent with value: %u\n", value);
    }

    ax_event_free(event);

    // Update index
    send_data->value_index = (send_data->value_index + 1) % MAX_DECLARATIONS;

    return TRUE;
}
static void declaration_complete(guint declaration, guint *value) {

    syslog(LOG_INFO, "Declaration complete for: %d with value: %d", declaration, *value);

    
     // Start timer only once (after declaring the "0" value)
    if(*value == 0) {
        // Set up a timer to be called every 5th second
        app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_event, app_data);
        LOG("Timer started.\n");
    }
      
    
    
}

static guint setup_declaration(AXEventHandler* event_handler, guint *value) {

    AXEventKeyValueSet* key_value_set = NULL;
    guint declaration                 = 0;
    GError* error                     = NULL;

    key_value_set = ax_event_key_value_set_new();

    ax_event_key_value_set_add_key_value(key_value_set, "topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING, NULL);

    ax_event_key_value_set_add_key_value(key_value_set, "topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING, NULL);
    ax_event_key_value_set_add_nice_names(key_value_set, "topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

    ax_event_key_value_set_add_key_value(key_value_set, "topic2", "tnsaxis", EVENT_TAG, AX_VALUE_TYPE_STRING, NULL);
    ax_event_key_value_set_add_nice_names(key_value_set, "topic2", "tnsaxis", EVENT_TAG, EVENT_NAME, NULL);

    // Declare value as SOURCE (will appear as dropdown in Axis UI)
    ax_event_key_value_set_add_key_value(key_value_set, "value", NULL, value, AX_VALUE_TYPE_INT, NULL);
    ax_event_key_value_set_mark_as_source(key_value_set, "value", NULL, NULL);
    ax_event_key_value_set_mark_as_data(key_value_set, "value", NULL, NULL);

    if (!ax_event_handler_declare(event_handler,
                                      key_value_set,
                                      1,  // Stateless
                                      &declaration,
                                      (AXDeclarationCompleteCallback)declaration_complete,
                                      value,
                                      NULL)) {
            LOG_ERROR("Could not declare event: %s\n", error->message);
    }

    ax_event_key_value_set_free(key_value_set);

    return declaration;
}


int main(void) {

      GMainLoop* main_loop  = NULL;

      // Set up the user logging to syslog
      openlog(SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
      syslog(LOG_INFO, "Started logging from send event application");

      
      //Initialize the event handler
      app_data = calloc(1, sizeof(AppData));
      setup_values();
      app_data->event_handler = ax_event_handler_new();
      //app_data->value_index = 0;

      for (int i = 0; i < MAX_DECLARATIONS; i++) {
          app_data->event_ids[i] = setup_declaration(app_data->event_handler, &app_data->values[i]);
      }
      

      // main loop
      main_loop = g_main_loop_new( NULL, FALSE);
        

      g_main_loop_run(main_loop);

      // Cleanup event handler
      for (int i = 0; i < MAX_DECLARATIONS; ++i) {
            ax_event_handler_undeclare(app_data->event_handler, app_data->event_ids[i], NULL);
      }

      ax_event_handler_free(app_data->event_handler);
      free(app_data);

      // Free g_main_loop
      g_main_loop_unref(main_loop);
      closelog();
      return 0;
}
