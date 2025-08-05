#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "send_data"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "SendData"
#define TOPIC1_NAME "Send Data"
#define EVENT_TAG   "SendDataEvent"
#define EVENT_NAME  "Send Data Event"

static guint count = 0;  // Counter for the number of events sent

typedef struct {
  AXEventHandler *event_handler;
  guint event_id;
  guint timer;
  char *user_data;
} AppData;

static AppData* app_data = NULL;


static gboolean send_data(AppData *send_data) {
  AXEventKeyValueSet *key_value_set = NULL;
  AXEvent *event = NULL;

  key_value_set = ax_event_key_value_set_new();

  ax_event_key_value_set_add_key_value(key_value_set, 
                                          "data", 
                                          NULL, 
                                          &send_data->user_data, 
                                          AX_VALUE_TYPE_STRING, 
                                          NULL);

  // Create the event
  event = ax_event_new2(key_value_set, NULL);

  // The key/value set is no longer needed
  ax_event_key_value_set_free(key_value_set);

  if (!ax_event_handler_send_event(send_data->event_handler, 
                                        send_data->event_id, 
                                        event, 
                                        NULL)) {
    LOG_ERROR("Could not fire event\n");
  }
  ax_event_free(event);

  count += count;
  send_data->user_data = "Count %d: Updated user data";  // Update user data for next event
  
  LOG("Event sent with user data: %s", send_data->user_data);

  // Returning TRUE keeps the timer going
  return TRUE;
}

static void declaration_complete(guint declaration, char *value) {

  Log("Declaration complete for: %d with value: %s", declaration, value);
  app_data->user_data = *value;  // Initialize user data
  app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_data, app_data);
}

static guint setup_declaration(AXEventHandler *event_handler, char *start_value) {

  AXEventKeyValueSet *key_value_set = NULL;
  guint declaration = 0;
  GError *error = NULL;
  
  key_value_set = ax_event_key_value_set_new();

//Note that the namespace is "tnsaxis:".  It is not recommended to create own name spaces or use the
//the ONVIF namespace "tns1:"
  
  //TOPIC LEVEL 0 
  ax_event_key_value_set_add_key_value( key_value_set,"topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING, NULL);
  //ax_event_key_value_set_add_nice_names( dataSet,"topic0", "tnsaxis", TOPIC0_NAME, TOPIC0_NAME ,NULL);
  //As we are using the standard CameraApplicationPlatform there is no need to set nice name  

  //TOPIC LEVEL 1
  ax_event_key_value_set_add_key_value( key_value_set,"topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING, NULL);
  ax_event_key_value_set_add_nice_names( key_value_set,"topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

  //TOPIC LEVEL 2
  ax_event_key_value_set_add_key_value(  key_value_set, "topic2", "tnsaxis", EVENT_TAG , AX_VALUE_TYPE_STRING, NULL);
  ax_event_key_value_set_add_nice_names( key_value_set, "topic2", "tnsaxis", EVENT_TAG, EVENT_NAME, NULL);

  // A data event is typically used for a specific client/application that knows how to process the data.
  // If the event is not intended to trigger general actions it is a good idea to tag the event
  // with "isApplicationData" to requests clients/actionrules not to display the event.
  ax_event_key_value_set_mark_as_user_defined( key_value_set, "topic2", "tnsaxis", "isApplicationData", NULL);

  //EVENT DATA INSTANCE
  // A tag id that holds the user data.  It is recommended to mark event user data with "isApplicationData"
  ax_event_key_value_set_add_key_value(key_value_set, "data", NULL, "" , AX_VALUE_TYPE_STRING, NULL);
  ax_event_key_value_set_mark_as_data(key_value_set, "data", NULL, NULL);
  ax_event_key_value_set_mark_as_user_defined(key_value_set, "data", NULL, "isApplicationData", NULL);
  
  //Note that the 3:rd parameter defines if the event is stateful or stateless.  1 = stateless, 0 = stateful
  if( !ax_event_handler_declare(event_handler, 
                                    key_value_set, 
                                    1, 
                                    &declaration, 
                                    (AXDeclarationCompleteCallback)declaration_complete, 
                                    NULL, 
                                    NULL))
    LOG_ERROR("Could not declare event\n");

  ax_event_key_value_set_free(key_value_set);

  return declaration;
}


int main(void) {
  GMainLoop* main_loop  = NULL;
  char *start_value     = "Initial user data";

  openlog(SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
  main_loop = g_main_loop_new( NULL, FALSE);

  //Initialize the event handler
  app_data                = calloc(1, sizeof(AppData));
  app_data->event_handler = ax_event_handler_new();
  app_data->event_id      = setup_declaration(app_data->event_handler, &start_value);
  
  g_main_loop_run(main_loop);

  /// Cleanup event handler
  ax_event_handler_undeclare(app_data->event_handler, app_data->event_id, NULL);
  ax_event_handler_free(app_data->event_handler);
  free(app_data);

  // Free g_main_loop
  g_main_loop_unref(main_loop);

  closelog();


  return 0;
}
