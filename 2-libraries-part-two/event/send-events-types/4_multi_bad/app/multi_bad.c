#include <stdio.h>
#include <syslog.h>
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "multi_choise_bad"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "MultiChoiseBad"
#define TOPIC1_NAME "Multi Choise Bad"
#define EVENT_TAG    "MultiChoiseBadEvent"
#define EVENT_NAME   "Multi Choise Bad Event"

typedef struct {
   AXEventHandler *event_handler;
   guint event_id;
   guint timer;
   guint channel;
   guint area;
   gboolean state;
 } AppData;

static AppData* app_data = NULL;

static void set_app_data(AppData *app_data) {
  
  app_data->channel   = 2;
  app_data->area      = 2;
  app_data->state     = 0;
 
}
static void send_event(AppData *app_data) {

  AXEventKeyValueSet *key_value_set = NULL;
  AXEvent            *event         = NULL;
  
  
  key_value_set = ax_event_key_value_set_new();
  ax_event_key_value_set_add_key_value(key_value_set, "active", NULL, &app_data->state, AX_VALUE_TYPE_BOOL, NULL);
  ax_event_key_value_set_add_key_value(key_value_set, "channel", NULL, &app_data->channel, AX_VALUE_TYPE_INT, NULL);
  ax_event_key_value_set_add_key_value(key_value_set, "area", NULL, &app_data->area, AX_VALUE_TYPE_INT, NULL);
  
  event = ax_event_new(key_value_set, NULL);
  ax_event_key_value_set_free(key_value_set);

  if( !ax_event_handler_send_event(app_data->event_handler, app_data->event_id, event, NULL) )
    LOG_ERROR("Could not fire event\n");
  ax_event_free(event);
}
static gboolean declaration_complete() {


}

static guint setup_declaration(AXEventHandler *event_handler, char *start_value) {
  AXEventKeyValueSet *key_value_set = NULL;
  guint declaration = 0;
  int   videosource = 2;
  int   area = 2;
  int   active = 0;
  
  key_value_set = ax_event_key_value_set_new();

//Note that the namespace is "tnsaxis:".  It is not recommended to create own name spaces or use the
//the ONVIF namespace "tns1:"
  
  //TOPIC LEVEL 0 
  ax_event_key_value_set_add_key_value( key_value_set, "topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING,NULL);
  //ax_event_key_value_set_add_nice_names( dataSet,"topic0", "tnsaxis", TOPIC0_NAME, TOPIC0_NAME ,NULL);
  //As we are using the standard CameraApplicationPlatform there is no need to set nice name  

  //TOPIC LEVEL 1
  ax_event_key_value_set_add_key_value(key_value_set, "topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING, NULL);
  ax_event_key_value_set_add_nice_names(key_value_set,"topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

  //TOPIC LEVEL 2
  ax_event_key_value_set_add_key_value(key_value_set, "topic2", "tnsaxis", EVENT_TAG , AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names(key_value_set, "topic2", "tnsaxis", EVENT_TAG, EVENT_NAME, NULL);

  //SOURCE INSTANCE
  // Note that the value of videosource (in this case 2) will be included in the event declaration.
  // It is not possible to declare a list of selectable video sources in the ACAP SDK context.
  ax_event_key_value_set_add_key_value( key_value_set, "channel", NULL, &videosource, AX_VALUE_TYPE_INT,NULL);
  ax_event_key_value_set_mark_as_source( key_value_set, "channel", NULL, NULL);
  
  //DATA INSTANCE
  ax_event_key_value_set_add_key_value( key_value_set, "area", NULL, &area, AX_VALUE_TYPE_INT, NULL);
  ax_event_key_value_set_mark_as_data( key_value_set, "area", NULL, NULL);

  ax_event_key_value_set_add_key_value( key_value_set,"active", NULL, &active, AX_VALUE_TYPE_BOOL, NULL);
  ax_event_key_value_set_mark_as_data( key_value_set, "active", NULL, NULL);
  
  //Note that the 3:rd parameter defines if he event is stateful or stateless.  1 = stateless, 0 = stateful
  if( !ax_event_handler_declare( event_handler, 
                                      key_value_set, 
                                      0, 
                                      &declaration, 
                                      (AXDeclarationCompleteCallback)declaration_complete, 
                                      &start_value, 
                                      NULL))
    LOG_ERROR("Could not declare event\n");

  ax_event_key_value_set_free(key_value_set);

  return declaration;
}





int main(void) {
  GMainLoop *main_loop  = NULL;
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
