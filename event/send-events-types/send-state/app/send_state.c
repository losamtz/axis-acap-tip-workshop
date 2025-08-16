#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "send-state"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "SendState"
#define TOPIC1_NAME "Send State"
#define EVENT_TAG   "SendStateEvent"
#define EVENT_NAME  "Send State Event"

typedef struct {
    AXEventHandler* event_handler;
    guint event_id;
    guint timer;
    guint state_value;
} AppData;

static AppData* app_data = NULL;


static gboolean send_event(AppData *send_data) {

    AXEventKeyValueSet *key_value_set = NULL;
    AXEvent  *event                   = NULL;
    //GDateTime *time_stamp;
    
    key_value_set = ax_event_key_value_set_new();

    ax_event_key_value_set_add_key_value( key_value_set, "active", NULL, &send_data->state_value, AX_VALUE_TYPE_BOOL, NULL);
    
    //time_stamp = g_date_time_new_now_local();

    // Create the event
    // Use ax_event_new2 since ax_event_new is deprecated from 3.2
    event = ax_event_new2(key_value_set, NULL);
    
    // The key/value set is no longer needed
    ax_event_key_value_set_free(key_value_set);

    if(!ax_event_handler_send_event(send_data->event_handler, send_data->event_id, event, NULL))
      LOG_ERROR("Could not fire event\n");
    ax_event_free(event);
    //g_date_time_unref(time_stamp);


    send_data->state_value = !send_data->state_value;
    
    // Returning TRUE keeps the timer going
    return TRUE;
}
static void declaration_complete(guint declaration, int *value) {
  syslog(LOG_INFO, "Declaration complete for: %d", declaration);

    app_data->state_value = *value;

    // Set up a timer to be called every 10th second
    app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_event, app_data);
}

static guint setup_declaration(AXEventHandler* event_handler, guint *start_value) {

    AXEventKeyValueSet* key_value_set = NULL;
    guint declaration                 = 0;
    GError* error                     = NULL;

    
    key_value_set = ax_event_key_value_set_new();
    
  //Note that the name space is "tnsaxis:".  It is not recommended to create own name spaces or use the
  //the ONVIF namespace "tns1:"

    //TOPIC LEVEL 0
    ax_event_key_value_set_add_key_value( key_value_set,"topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING,NULL);
    //ax_event_key_value_set_add_nice_names( dataSet,"topic0", "tnsaxis", TOPIC0_NAME, TOPIC0_NAME ,NULL);
    //As we are using the standard CameraApplicationPlatform there is no need to set nice name  

    //TOPIC LEVEL 1
    ax_event_key_value_set_add_key_value( key_value_set,"topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING,NULL);
    ax_event_key_value_set_add_nice_names( key_value_set,"topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

    //TOPIC LEVEL 2
    ax_event_key_value_set_add_key_value(  key_value_set, "topic2", "tnsaxis", EVENT_TAG , AX_VALUE_TYPE_STRING,NULL);
    ax_event_key_value_set_add_nice_names( key_value_set, "topic2", "tnsaxis", EVENT_TAG, EVENT_NAME, NULL);

    //EVENT DATA INSTANCE
    // A bool data instance called "active" will hold the event state 0 or 1  
    ax_event_key_value_set_add_key_value( key_value_set,"active", NULL, &start_value, AX_VALUE_TYPE_BOOL, NULL);
    ax_event_key_value_set_mark_as_data( key_value_set, "active", NULL, NULL);
    
    //Note that the 3:rd parameter defines if he event is stateful or stateless.  1 = stateless, 0 = stateful
    if( !ax_event_handler_declare(event_handler, 
                                  key_value_set, 
                                  0,              // here defines state (0)
                                  &declaration, 
                                  (AXDeclarationCompleteCallback)declaration_complete, 
                                  &start_value, 
                                  NULL)) {
        syslog(LOG_WARNING, "Could not declare: %s", error->message);
        g_error_free(error);
    }   
    
    // The key/value set is no longer needed
    ax_event_key_value_set_free( key_value_set );
    return declaration;
}


int main(void) {
  GMainLoop* main_loop  = NULL;
  guint start_value     = 0;

  // Set up the user logging to syslog
  openlog(SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
  syslog(LOG_INFO, "Started logging from send event application");

  
  //Initialize the event handler
  app_data                = calloc(1, sizeof(AppData));
  app_data->event_handler = ax_event_handler_new();
  app_data->event_id      = setup_declaration(app_data->event_handler, &start_value);

  // main loop
  main_loop = g_main_loop_new( NULL, FALSE);
    

  g_main_loop_run(main_loop);

  /// Cleanup event handler
  ax_event_handler_undeclare(app_data->event_handler, app_data->event_id, NULL);
  ax_event_handler_free(app_data->event_handler);
  free(app_data);

  // Free g_main_loop
  g_main_loop_unref(main_loop);

  return 0;
}
