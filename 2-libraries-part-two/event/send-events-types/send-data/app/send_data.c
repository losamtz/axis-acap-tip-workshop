#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "send-data"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "SendData"
#define TOPIC1_NAME "Send Data"
#define EVENT_TAG   "SendDataEvent"
#define EVENT_NAME  "Send Data Event"

#define TOTAL_MEMORY 500 // Simulate total memory (e.g. 500MB)

typedef struct {
  AXEventHandler *event_handler;
  guint event_id;
  guint timer;
  gdouble temperature;
  gdouble load;
  guint free_memory;
  guint used_memory;
} AppData;

static AppData* app_data = NULL;

static void generate_random_data(AppData *app_data) {

  // Simulate temperature between 20.0 and 80.0 degrees Celsius
  app_data->temperature = 20.0 + (rand() % 6000) / 100.0;  // 20.0 to 80.0

  // Simulate system load as a float between 0.0 and 4.0 (e.g. CPU load avg)
  app_data->load = (rand() % 400) / 100.0;

  

  // Simulate used memory between 0 and total
  app_data->used_memory = rand() % TOTAL_MEMORY;
  LOG("used memory: %d")
  // Free memory is the rest
  app_data->free_memory = TOTAL_MEMORY - app_data->used_memory;
}

static gboolean send_data(AppData *send_data) {

  AXEventKeyValueSet *key_value_set = NULL;
  AXEvent *event = NULL;

  generate_random_data(send_data);
  key_value_set = ax_event_key_value_set_new();

  // Add the variable elements of the event to the set
    
    if(!ax_event_key_value_set_add_key_value(key_value_set, "Temperature", NULL,
                                         &app_data->temperature,
                                         AX_VALUE_TYPE_DOUBLE, NULL)) {
        syslog(LOG_WARNING, "Could not add temperature key/value pair");
    }                                        
    if(!ax_event_key_value_set_add_key_value(key_value_set, "Load", NULL,
                                       &app_data->load, AX_VALUE_TYPE_DOUBLE,
                                       NULL)) {
        syslog(LOG_WARNING, "Could not add load key/value pair");
    }
  
    if(!ax_event_key_value_set_add_key_value(key_value_set, "UsedMemory", NULL,
                                        &app_data->used_memory, AX_VALUE_TYPE_INT,
                                        NULL)) {
        syslog(LOG_WARNING, "Could not add used memory key/value pair");
    }

    if(!ax_event_key_value_set_add_key_value(key_value_set, "FreeMemory", NULL,
                                        &app_data->free_memory, AX_VALUE_TYPE_INT,
                                        NULL)) {
        syslog(LOG_WARNING, "Could not add free memory key/value pair");
    }

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
  else
    LOG("sent data event");

  ax_event_free(event);

  // Returning TRUE keeps the timer going
  return TRUE;
}

static void declaration_complete(guint declaration, gdouble *value) {

    LOG("Declaration complete for: %d\n", declaration);
    
    app_data->temperature = *value;
    app_data->load = 0.0;
    app_data->used_memory = 0;
    app_data->free_memory = 0;

    // timer to be called every 3th second
    app_data->timer = g_timeout_add_seconds(3, (GSourceFunc)send_data, app_data);

}

static guint setup_declaration(AXEventHandler *event_handler) {

      AXEventKeyValueSet *key_value_set = NULL;
      guint declaration                 = 0;
      
      gdouble start_value               = 0.0;

      gdouble temperature               = 0.5;
      gdouble load                      = 0.0;
      guint used_memory                 = 0;
      gboolean free_memory              = 0;
      //GError *error                     = NULL;

      
      key_value_set = ax_event_key_value_set_new();

      // OMITING ERROR HANDLING!

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
      // A tag id that holds the user data Temperature.  It is recommended to mark event user data with "isApplicationData"
      ax_event_key_value_set_add_key_value(key_value_set, "Temperature", NULL, &temperature , AX_VALUE_TYPE_DOUBLE, NULL);
      ax_event_key_value_set_mark_as_data(key_value_set, "Temperature", NULL, NULL);
      ax_event_key_value_set_mark_as_user_defined(key_value_set, "Temperature", NULL, "isApplicationData", NULL);

      // Data Load
      ax_event_key_value_set_add_key_value(key_value_set, "Load", NULL, &load, AX_VALUE_TYPE_DOUBLE, NULL);
      ax_event_key_value_set_mark_as_data(key_value_set, "Load", NULL, NULL);
      ax_event_key_value_set_mark_as_user_defined(key_value_set, "Load", NULL, "isApplicationData", NULL);

      // Data Used Memory

      ax_event_key_value_set_add_key_value(key_value_set, "UsedMemory", NULL, &used_memory, AX_VALUE_TYPE_INT, NULL);
      ax_event_key_value_set_add_nice_names( key_value_set, "UsedMemory", NULL, "UsedMemory", "Used Memory (MB)", NULL);
      ax_event_key_value_set_mark_as_data(key_value_set, "UsedMemory", NULL, NULL);
      ax_event_key_value_set_mark_as_user_defined(key_value_set, "UsedMemory", NULL, "isApplicationData", NULL);

      // Data Free Memory

      ax_event_key_value_set_add_key_value(key_value_set, "FreeMemory", NULL, &free_memory, AX_VALUE_TYPE_INT, NULL);
      ax_event_key_value_set_add_nice_names( key_value_set, "FreeMemory", NULL, "FreeMemory", "Free Memory (MB)", NULL);
      ax_event_key_value_set_mark_as_data(key_value_set, "FreeMemory", NULL, NULL);
      ax_event_key_value_set_mark_as_user_defined(key_value_set, "FreeMemory", NULL, "isApplicationData", NULL);
      
      //Note that the 3:rd parameter defines if the event is stateful or stateless.  1 = stateless, 0 = stateful
      if( !ax_event_handler_declare(event_handler, 
                                        key_value_set, 
                                        1, 
                                        &declaration, 
                                        (AXDeclarationCompleteCallback)declaration_complete, 
                                        &start_value, 
                                        NULL))
        LOG_ERROR("Could not declare event\n");

      ax_event_key_value_set_free(key_value_set);

      return declaration;
}


int main(void) {

      GMainLoop* main_loop  = NULL;
      

      openlog(SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
      main_loop = g_main_loop_new( NULL, FALSE);

      //Initialize the event handler
      app_data                = calloc(1, sizeof(AppData));
      app_data->event_handler = ax_event_handler_new();
      app_data->event_id      = setup_declaration(app_data->event_handler);
      
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
