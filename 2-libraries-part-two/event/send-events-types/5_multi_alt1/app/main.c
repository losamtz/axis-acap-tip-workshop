#include <stdio.h>
#include <syslog.h>
#include <time.h>
#include <axsdk/axevent.h>

#define LOG(fmt, args...)    { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERROR(fmt, args...)    { syslog(LOG_CRIT, fmt, ## args); printf(fmt, ## args); }

#define SERVICE_ID   "mymultievent1"

#define TOPIC0_TAG  "CameraApplicationPlatform"
#define TOPIC0_NAME "ACAP"
#define TOPIC1_TAG  "mymultievent1"
#define TOPIC1_NAME "My Multi Alt 1"

//Global variables
AXEventHandler *event_handler = 0;

guint
declare_event( const char *channel, const char *channelName, const char *area, const char *areaName ) {
  AXEventKeyValueSet *dataSet = NULL;
  guint declarationId;
  int active = 0;
  
  dataSet = ax_event_key_value_set_new();
  
  //TOPIC LEVEL 0 
  ax_event_key_value_set_add_key_value( dataSet,"topic0", "tnsaxis", TOPIC0_TAG, AX_VALUE_TYPE_STRING,NULL);
  //ax_event_key_value_set_add_nice_names( dataSet,"topic0", "tnsaxis", TOPIC0_NAME, TOPIC0_NAME ,NULL);
  //As we are using the standard CameraApplicationPlatform there is no need to set nice name  

  //TOPIC LEVEL 1
  ax_event_key_value_set_add_key_value( dataSet,"topic1", "tnsaxis", TOPIC1_TAG, AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names( dataSet,"topic1", "tnsaxis", TOPIC1_TAG, TOPIC1_NAME, NULL);

  //TOPIC LEVEL 2
  ax_event_key_value_set_add_key_value(  dataSet, "topic2", "tnsaxis", channel , AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names( dataSet, "topic2", "tnsaxis", channel, channelName, NULL);

  //TOPIC LEVEL 3
  ax_event_key_value_set_add_key_value(  dataSet, "topic3", "tnsaxis", area , AX_VALUE_TYPE_STRING,NULL);
  ax_event_key_value_set_add_nice_names( dataSet, "topic3", "tnsaxis", area, areaName, NULL);
  
  //EVENT DATA INSTANCE
  ax_event_key_value_set_add_key_value( dataSet,"active", NULL, &active, AX_VALUE_TYPE_BOOL,NULL);
  ax_event_key_value_set_mark_as_data( dataSet, "active", NULL, NULL);
  
  //Note that the 3:rd parameter defines if he event is stateful or stateless.  1 = stateless, 0 = stateful
  if( !ax_event_handler_declare( event_handler, dataSet, 0, &declarationId, NULL, NULL, NULL) )
    LOG_ERROR("Could not declare event\n");
  ax_event_key_value_set_free( dataSet );
  return declarationId;
}

void
event_fire( int theEventId, int theState ) {
  AXEventKeyValueSet *dataSet = NULL;
  AXEvent            *event = NULL;
  GTimeVal           time_stamp;
  
  dataSet = ax_event_key_value_set_new();
  ax_event_key_value_set_add_key_value( dataSet, "active", NULL, &theState, AX_VALUE_TYPE_BOOL,NULL);
  
  g_get_current_time(&time_stamp);
  event = ax_event_new( dataSet, &time_stamp);
  ax_event_key_value_set_free( dataSet );

  if( !ax_event_handler_send_event( event_handler, theEventId, event, NULL) )
    LOG_ERROR("Could not fire event\n");
  ax_event_free( event );
}

guint eventIdContainer[2][3];
guint eventStates[2][2];
int channelCounter = 0;
int areaCounter = 0;
int state = 1;

static gboolean
timerCallback() {
  //Random event generation
  areaCounter++;
  if( areaCounter == 2 ) {
    areaCounter = 0;
    channelCounter++;
    if( channelCounter == 2 ) {
      channelCounter = 0;
      state = !state;
    }
  }
  eventStates[channelCounter][areaCounter] = state;

  event_fire( eventIdContainer[channelCounter][areaCounter] , state );
  //Send "Any event" state that combines states of the two areas
  event_fire( eventIdContainer[channelCounter][2] , eventStates[channelCounter][0] | eventStates[channelCounter][1] );
 
  return TRUE; //Returning true will make timer continue repeating callback
}

int main(void) {
  GMainLoop *loop;
  
  openlog(SERVICE_ID, LOG_PID|LOG_CONS, LOG_USER);
  loop = g_main_loop_new( NULL, FALSE);

  //Initialize the event handler
  event_handler = ax_event_handler_new();

  eventIdContainer[0][0] = declare_event( "channel1", "Video 1", "area1", "Window 1" );
  eventIdContainer[0][1] = declare_event( "channel1", "Video 1", "area2", "Window 2" );
  eventIdContainer[0][2] = declare_event( "channel1", "Video 1", "areax", "Any Window " );
  eventIdContainer[1][0] = declare_event( "channel2", "Video 2", "area1", "Window 1" );
  eventIdContainer[1][1] = declare_event( "channel2", "Video 2", "area2", "Window 2" );
  eventIdContainer[1][2] = declare_event( "channel2", "Video 2", "areax", "Any Window " );

  //Change states for a channel/area 
  g_timeout_add_seconds( 3, timerCallback, NULL );  
  g_main_loop_run(loop);

  //No need to clean up as ACAP platform will do this for you
  return 0;
}
