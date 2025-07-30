#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "mymultieventbad"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "mymultieventbad"
#define TOPIC1_NAME "My Bad Multi Event Service"
#define EVENT_TAG    "badMutliEvent"
#define EVENT_NAME   "A bad event"

//Global variables
AXEventHandler *event_handler = 0;
guint event_id = 0;

guint
declare_event( ) {
  AXEventKeyValueSet *dataSet = NULL;
  guint declaration_ID = 0;
  int   videosource = 2;
  int   area = 2;
  int   active = 0;
  
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

  //SOURCE INSTANCE
  // Note that the value of videosource (in this case 2) will be included in the event declaration.
  // It is not possible to declare a list of selectable video sources in the ACAP SDK context.
  ax_event_key_value_set_add_key_value( dataSet, "channel", NULL, &videosource, AX_VALUE_TYPE_INT,NULL);
  ax_event_key_value_set_mark_as_source( dataSet, "channel", NULL, NULL);
  
  //DATA INSTANCE
  ax_event_key_value_set_add_key_value( dataSet, "area", NULL, &area, AX_VALUE_TYPE_INT, NULL);
  ax_event_key_value_set_mark_as_data( dataSet, "area", NULL, NULL);
  ax_event_key_value_set_add_key_value( dataSet,"active", NULL, &active, AX_VALUE_TYPE_BOOL, NULL);
  ax_event_key_value_set_mark_as_data( dataSet, "active", NULL, NULL);
  
  //Note that the 3:rd parameter defines if he event is stateful or stateless.  1 = stateless, 0 = stateful
  if( !ax_event_handler_declare( event_handler, dataSet, 0, &declaration_ID, NULL, NULL, NULL) )
    LOG_ERROR("Could not declare event\n");
  ax_event_key_value_set_free( dataSet );
  return declaration_ID;
}

void
event_fire( guint theEventID, int theState, int theChannel, int theArea ) {
  AXEventKeyValueSet *dataSet = NULL;
  AXEvent            *event = NULL;
  GTimeVal            time_stamp;
  
  dataSet = ax_event_key_value_set_new();
  ax_event_key_value_set_add_key_value( dataSet, "active", NULL, &theState, AX_VALUE_TYPE_BOOL,NULL);
  ax_event_key_value_set_add_key_value( dataSet, "channel", NULL, &theChannel, AX_VALUE_TYPE_INT,NULL);
  ax_event_key_value_set_add_key_value( dataSet, "area", NULL, &theArea, AX_VALUE_TYPE_INT,NULL);
  
  g_get_current_time(&time_stamp);
  event = ax_event_new( dataSet, &time_stamp);
  ax_event_key_value_set_free( dataSet );

  if( !ax_event_handler_send_event( event_handler, event_id, event, NULL) )
    LOG_ERROR("Could not fire event\n");
  ax_event_free( event );
}

int togglingState = 0;
int channel = 1;
int area = 1;

static gboolean
timerCallback() {
  togglingState = !togglingState;

  channel++;
  if( channel == 2 ) {
    channel = 0;
    area++;
    if( area == 2 )
      area = 0;
  }
  event_fire( event_id, togglingState, channel, area );
  
  return TRUE; //Returning true will make timer continue repeating callback
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
