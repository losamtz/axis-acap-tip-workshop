/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * - send_event.c -
 *
 * This example illustrates how to send a stateful ONVIF event, which is
 * changing the value every 10th second.
 *
 * Error handling has been omitted for the sake of brevity.
 */
#include <axsdk/axevent.h>
#include <glib-object.h>
#include <glib.h>
#include <string.h>
#include <syslog.h>

typedef struct {
   AXEventHandler *event_handler;
   guint event_id;
   guint timer;
   gdouble confidence;
   guint x_coord;
   guint y_coord;
   gboolean moving;
 } AppData;

char *quadrant = NULL;
char *confidence_range = NULL;
char *moving_state = NULL;

static AppData* app_data = NULL;

void set_app_data(AppData *app_data) {
  // Generate a random confidence value
  int random_denominator = rand() % 100 + 1;
  app_data->confidence = 1.0 / random_denominator;

  // Set moving to true (1) or false (0)
  app_data->moving = rand() % 2;

  // Generate random x and y coordinates
  app_data->x_coord = rand() % 1920;
  app_data->y_coord = rand() % 1080;
}

/**
 * brief Send event.
 *
 * Send the previously declared event.
 *
 * param app_data Application data containing e.g. the event declaration id.
 * return TRUE
 */
static gboolean send_event(AppData* app_data) {
    AXEventKeyValueSet* key_value_set = NULL;
    AXEvent* event                    = NULL;

    key_value_set = ax_event_key_value_set_new();

    set_app_data(app_data);

    if(app_data->x_coord < 960) {
        if(app_data->y_coord < 540) {
            quadrant = "TOP_LEFT";
        } else {
            quadrant = "BOTTOM_LEFT";
        }
    } else {
        if(app_data->y_coord < 540) {
            quadrant = "TOP_RIGHT";
        } else {
            quadrant = "BOTTOM_RIGHT";
        }
    }

    if(app_data->confidence < 0.3) {
        confidence_range = "LOW";
    } else if(app_data->confidence < 0.7) {
        confidence_range = "MEDIUM";
    } else {
        confidence_range = "HIGH";
    }
    
    if(app_data->moving) {
        moving_state = "TRUE";
    } else {
        moving_state = "FALSE";
    }

    if(!ax_event_key_value_set_add_key_value(key_value_set, "Confidence", NULL,
                                         confidence_range,
                                         AX_VALUE_TYPE_DOUBLE, NULL)) {
        syslog(LOG_WARNING, "Could not add Confidence key/value pair");
    }
    if(!ax_event_key_value_set_add_key_value(key_value_set, "Area", NULL,
                                       quadrant, AX_VALUE_TYPE_INT,
                                       NULL)) {
        syslog(LOG_WARNING, "Could not add Area key/value pair");
    }
    // if(!ax_event_key_value_set_add_key_value(key_value_set, "YCoord", NULL,
    //                                    moving_state, AX_VALUE_TYPE_INT,
    //                                    NULL)) {
    //     syslog(LOG_WARNING, "Could not add YCoord key/value pair");
    // }
    if(!ax_event_key_value_set_add_key_value(key_value_set, "Moving", NULL,
                                       moving_state, AX_VALUE_TYPE_BOOL,
                                       NULL)) {
        syslog(LOG_WARNING, "Could not add moving key/value pair");
    }
    // Create the event
    // Use ax_event_new2 since ax_event_new is deprecated from 3.2
    event = ax_event_new2(key_value_set, NULL);

    // The key/value set is no longer needed
    ax_event_key_value_set_free(key_value_set);

    // Send the event
    if(!ax_event_handler_send_event(app_data->event_handler, app_data->event_id,
                                    event, NULL)) {
        syslog(LOG_WARNING, "Could not send event using ax_event_handler_send_event");
    } else {
        syslog(LOG_INFO, "Sent stateless event");
    }

    ax_event_free(event);


    // Returning TRUE keeps the timer going
    return TRUE;
}

/**
 * brief Callback function which is called when event declaration is completed.
 *
 * This callback will be called when the declaration has been registered
 * with the event system. The event declaration can now be used to send
 * events.
 *
 * param declaration Event declaration id.
 * param value Start value of the event.
 */
static void declaration_complete(guint declaration, gdouble* value) {
    syslog(LOG_INFO, "Declaration complete for: %d", declaration);

    app_data->confidence = *value;
    app_data->x_coord = 0;
    app_data->y_coord = 0;
    app_data->moving = FALSE;

    // Set up a timer to be called every 5 second
    app_data->timer = g_timeout_add_seconds(5, (GSourceFunc)send_event, app_data);
}

/**
 * brief Setup a declaration of an event.
 *
 * Declare a stateful ONVIF event that looks like this,
 * which is using ONVIF namespace "tns1".
 *
 * Topic: tns1:Monitoring/ProcessorUsage
 * <tt:MessageDescription IsProperty="true">
 *  <tt:Source>
 *   <tt:SimpleItemDescription Name=”Token” Type=”tt:ReferenceToken”/>
 *  </tt:Source>
 *  <tt:Data>
 *   <tt:SimpleItemDescription Name="Value" Type="xs:float"/>
 *  </tt:Data>
 * </tt:MessageDescription>
 *
 * Value = 0 <-- The initial value will be set to 0.0
 *
 * param event_handler Event handler.
 * return declaration id as integer.
 */
static guint setup_declaration(AXEventHandler* event_handler) {
    AXEventKeyValueSet *key_value_set = NULL;
    guint declaration = 0;
    gdouble start_value = 0;
    char *confidence = "NO_DETECTION";
    char *area = "NO_DETECTION";
    char *moving = "NO_DETECTION";
    GError *error = NULL;


    // Create keys, namespaces and nice names for the event
    key_value_set = ax_event_key_value_set_new();
    if (!ax_event_key_value_set_add_key_value(key_value_set, "topic0", "tnsaxis",
                                          "CameraApplicationPlatform",
                                          AX_VALUE_TYPE_STRING, &error)) {
        syslog(LOG_WARNING, "Could not add key/value pair: %s", error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
  
    if (!ax_event_key_value_set_add_key_value(key_value_set, "topic1", "tnsaxis",
                                          "Event",
                                          AX_VALUE_TYPE_STRING, &error)) {
            syslog(LOG_WARNING, "Could not add key/value pair: %s", error->message);
            g_error_free(error);
            ax_event_key_value_set_free(key_value_set);
            return declaration;
    }
    if (!ax_event_key_value_set_add_key_value(
            key_value_set, "Confidence", NULL /* namespace */,
            confidence, AX_VALUE_TYPE_STRING, &error)) {
        syslog(LOG_WARNING, "Could not add key/value pair: %s", error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
    if (!ax_event_key_value_set_mark_as_data(key_value_set, "Confidence",
                                         NULL /* namespace */,
                                         &error)) {
        syslog(LOG_WARNING, "Could not mark key/value pair as data: %s",
                error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
    //XCoord
    if (!ax_event_key_value_set_add_key_value(
            key_value_set, "Area", NULL /* namespace */,
            area, AX_VALUE_TYPE_STRING, &error)) {
        syslog(LOG_WARNING, "Could not add key/value pair: %s", error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
    if (!ax_event_key_value_set_mark_as_data(key_value_set, "Area",
                                         NULL /* namespace */,
                                         &error)) {
        syslog(LOG_WARNING, "Could not mark key/value pair as data: %s",
                error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
    
    
    //Moving
    if (!ax_event_key_value_set_add_key_value(
            key_value_set, "Moving", NULL /* namespace */,
            &ycoord, AX_VALUE_TYPE_STRING, &error)) {
        syslog(LOG_WARNING, "Could not add key/value pair: %s", error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }
    if (!ax_event_key_value_set_mark_as_data(key_value_set, "Moving",
                                         NULL /* namespace */,
                                         &error)) {
        syslog(LOG_WARNING, "Could not mark key/value pair as data: %s",
                error->message);
        g_error_free(error);
        ax_event_key_value_set_free(key_value_set);
        return declaration;
    }

    
    // Declare event
    if (!ax_event_handler_declare(event_handler,
                                  key_value_set,
                                  TRUE, // Indicate a stateless event
                                  &declaration,
                                  (AXDeclarationCompleteCallback)declaration_complete,
                                  &start_value,
                                  &error)) {
        syslog(LOG_WARNING, "Could not declare: %s", error->message);
        g_error_free(error);
    }

    // The key/value set is no longer needed
    ax_event_key_value_set_free(key_value_set);
    return declaration;
}

/**
 * brief Main function which sends an event.
 */
gint main(void) {
    GMainLoop* main_loop = NULL;

    // Set up the user logging to syslog
    openlog(NULL, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Started logging from send event application");

    // Event handler
    app_data                = calloc(1, sizeof(AppData));
    app_data->event_handler = ax_event_handler_new();
    app_data->event_id      = setup_declaration(app_data->event_handler);

    // Main loop
    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);

    // Cleanup event handler
    ax_event_handler_undeclare(app_data->event_handler, app_data->event_id, NULL);
    ax_event_handler_free(app_data->event_handler);
    free(app_data);

    // Free g_main_loop
    g_main_loop_unref(main_loop);

    closelog();

    return 0;
}
