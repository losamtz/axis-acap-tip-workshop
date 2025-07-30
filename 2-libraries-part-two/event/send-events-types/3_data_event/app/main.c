#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "mydataservice"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "mydataservice"
#define TOPIC1_NAME "My Data Service"
#define EVENT_TAG   "theDataEvent"
#define EVENT_NAME  "My Data Event"

//Global variables
AXEventHandler *event_handler = 0;
guint event_id = 0;

guint
declare_event( ) {
  AXEventKeyValueSet *dataSet = NULL;
  guint declaration_ID = 0;
  
  dataSet = ax_event_key_value_set_new();

//Note that the namespace is "tnsaxis:".  It is not recommended to create own name spaces or use the
//the ONVIF namespace "tns1:"
  
  //TOPIC LEVEL 0 
  ax_event_key_value_set_add_key_value( dataSet,"topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING,NULL);
  //ax_event_key_value_set_add_nice_names( dataSet,"topic0", "tnsaxis", TOPIC0_NAME, TOPIC0_NAME ,NULL);
  //As we are using the standard CameraApplicationPlatform there is no need to set nice name  

  //TOPIC LEVEL 1
  ax_event_key_value_set_add_key_value( dataSet,"topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names( dataSet,"topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

  //TOPIC LEVEL 2
  ax_event_key_value_set_add_key_value(  dataSet, "topic2", "tnsaxis", EVENT_TAG , AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names( dataSet, "topic2", "tnsaxis", EVENT_TAG, EVENT_NAME, NULL);
  // A data event is typically used for a specific client/application that knows how to process the data.
  // If the event is not intended to trigger general actions it is a good idea to the tag the event
  // with "isApplicationData" to requests clients/actionrules not to display the event.
  ax_event_key_value_set_mark_as_user_defined( dataSet, "topic2", "tnsaxis", "isApplicationData", NULL);

  //EVENT DATA INSTANCE
  // A tag id that holds the user data.  It is recommended to mark event user data with "isApplicationData"
  ax_event_key_value_set_add_key_value( dataSet, "eventData", NULL, "" , AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_mark_as_data(  dataSet, "eventData", NULL, NULL);
  ax_event_key_value_set_mark_as_user_defined( dataSet, "eventData", NULL, "isApplicationData", NULL);
  
  //Note that the 3:rd parameter defines if the event is stateful or stateless.  1 = stateless, 0 = stateful
  if( !ax_event_handler_declare( event_handler, dataSet, 1, &declaration_ID, NULL, NULL, NULL) )
    LOG_ERROR("Could not declare event\n");
  ax_event_key_value_set_free( dataSet );
  return declaration_ID;
}

void
event_fire( guint theEventID, const char *theUserDataString ) {
  AXEventKeyValueSet *dataSet = NULL;
  AXEvent            *event = NULL;
  GTimeVal           time_stamp;
  
  dataSet = ax_event_key_value_set_new();
  ax_event_key_value_set_add_key_value( dataSet,"eventData",NULL, theUserDataString, AX_VALUE_TYPE_STRING,NULL);
  
  g_get_current_time(&time_stamp);
  event = ax_event_new( dataSet, &time_stamp);
  ax_event_key_value_set_free( dataSet );

  if( !ax_event_handler_send_event( event_handler, theEventID, event, NULL) )
    LOG_ERROR("Could not fire event\n");
  ax_event_free( event );
}

static gboolean
timerCallback() {
  char someDataString[] = "The event user data";
  event_fire( event_id, someDataString );
  //Returning true will make timer continue repeating callback
  return TRUE; 
}

int main(void) {
  GMainLoop *loop;

  openlog( SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
  loop = g_main_loop_new( NULL, FALSE);

  //Initialize the event handler
  event_handler = ax_event_handler_new();
  event_id = declare_event();

  //Repeat sending events
  g_timeout_add_seconds( 5, timerCallback, NULL );  

  g_main_loop_run(loop);

  //No need to clean up as ACAP platform will do this for you
  return 0;
}
