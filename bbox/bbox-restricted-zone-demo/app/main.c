#include "zone.h"
#include "status_server.h"
#include <bbox.h>
#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <jansson.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#define APP "bbox_restricted_zone"
static Config config;
static State state;
static bbox_t* boxes;
static bbox_color_t yellow,green,amber,red;
static gint64 started,retry;
static gchar* draw_error;
static const char* names[]={"Enabled","Zone","Rule","DwellSeconds"};
static gboolean rectangle(Rect r) { return bbox_rectangle(boxes,r.x,r.y,r.x+r.w,r.y+r.h); }
static gboolean draw(void) {
    if(!boxes) {
        boxes=bbox_view_new(1);
        if(!boxes) return FALSE;
    }
    if(!bbox_coordinates_frame_normalized(boxes) || !bbox_video_output(boxes,true) || !bbox_clear(boxes)) return FALSE;
    if(config.enabled) {
        if(!bbox_style_outline(boxes) || !bbox_thickness_thin(boxes) || !bbox_color(boxes,yellow) || !rectangle(config.zone)) return FALSE;
        if(!bbox_style_corners(boxes) || !bbox_thickness_thick(boxes) ||
           !bbox_color(boxes,state.alarm?red:state.inside?amber:green) || !rectangle(state.object)) return FALSE;
    }
    return bbox_commit(boxes,0);
}
static void parameter_changed(const gchar* name,const gchar* value,gpointer unused) {
    (void)unused;
    if(config_apply(&config,name,value)) {
        /* A new policy starts a new simulation run and clears its counters. */
        memset(&state,0,sizeof(state)); started=g_get_monotonic_time(); retry=0;
    } else syslog(LOG_WARNING,"Rejected parameter %s",name);
}
static json_t* rect_json(Rect r) { return json_pack("[f,f,f,f]",r.x,r.y,r.w,r.h); }
static gboolean tick(gpointer unused) {
    (void)unused; gint64 now=g_get_monotonic_time();
    zone_step(&state,&config,(now-started)/1e6,now);
    if(now>=retry) {
        if(draw()) g_clear_pointer(&draw_error,g_free);
        else {
            g_free(draw_error); draw_error=g_strdup_printf("BBox drawing failed: %s. Previous boxes may remain visible.",strerror(errno));
            syslog(LOG_WARNING,"%s",draw_error); retry=now+5*G_USEC_PER_SEC;
            if(boxes) { bbox_destroy(boxes); boxes=NULL; }
        }
    }
    json_t* root=json_pack("{s:b,s:b,s:s,s:i,s:b,s:b,s:i,s:i,s:s}",
        "simulated",TRUE,"enabled",config.enabled,"rule",config.overlap?"overlap":"center","dwellSeconds",config.dwell,
        "inside",state.inside,"alarm",state.alarm,"entries",state.entries,
        "insideSeconds",state.inside?(int)((now-state.entered)/G_USEC_PER_SEC):0,
        "drawError",draw_error?draw_error:"");
    json_object_set_new(root,"zone",rect_json(config.zone)); json_object_set_new(root,"object",rect_json(state.object));
    char* text=json_dumps(root,JSON_COMPACT); if(text) status_server_publish(text);
    free(text);json_decref(root);return G_SOURCE_CONTINUE;
}
static gboolean stop(gpointer loop) { g_main_loop_quit(loop); return G_SOURCE_CONTINUE; }
int main(void) {
    openlog(APP,LOG_PID,LOG_USER);signal(SIGPIPE,SIG_IGN);
    GError* error=NULL;AXParameter* parameters=ax_parameter_new(APP,&error);
    GMainLoop* loop=g_main_loop_new(NULL,FALSE);guint registered=0;int result=EXIT_FAILURE;
    if(!parameters) goto cleanup;
    for(guint i=0;i<G_N_ELEMENTS(names);i++) {
        gchar* value=NULL;
        if(!ax_parameter_get(parameters,names[i],&value,&error)) goto cleanup;
        gboolean valid=config_apply(&config,names[i],value);g_free(value);
        if(!valid) { syslog(LOG_ERR,"Invalid saved parameter %s",names[i]);goto cleanup; }
        if(!ax_parameter_register_callback(parameters,names[i],parameter_changed,NULL,&error)) goto cleanup;
        registered++;
    }
    yellow=bbox_color_from_rgb(255,207,50);green=bbox_color_from_rgb(40,190,90);
    amber=bbox_color_from_rgb(255,150,0);red=bbox_color_from_rgb(230,40,40);
    if(!status_server_start()) {syslog(LOG_ERR,"Cannot start status service");goto cleanup;}
    started=g_get_monotonic_time();tick(NULL);
    guint timer=g_timeout_add(100,tick,NULL),term=g_unix_signal_add(SIGTERM,stop,loop),intr=g_unix_signal_add(SIGINT,stop,loop);
    g_main_loop_run(loop);g_source_remove(timer);g_source_remove(term);g_source_remove(intr);result=EXIT_SUCCESS;
cleanup:
    if(error){syslog(LOG_ERR,"%s",error->message);g_clear_error(&error);}
    if(boxes){if(!bbox_clear(boxes) || !bbox_commit(boxes,0)) syslog(LOG_WARNING,"Could not clear boxes");bbox_destroy(boxes);}
    status_server_stop();
    if(parameters){for(guint i=0;i<registered;i++)ax_parameter_unregister_callback(parameters,names[i]);ax_parameter_free(parameters);}
    g_free(draw_error);g_main_loop_unref(loop);closelog();return result;
}
