#include "inspection.h"
#include "status_server.h"
#include <axsdk/axparameter.h>
#include <glib-unix.h>
#include <jansson.h>
#include <vdo-stream.h>
#include <vdo-error.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#define APP "vdo_inspection_zone"
static const char* names[]={"Enabled","Zone","PixelDelta","OccupiedPercent","DwellSeconds"};
static Config config;
static Inspection inspection;
static VdoStream* stream;
static guint width,height,pitch;
static int stream_fd=-1;
static gint64 last_frame,retry_at,rate_start,changed_at;
static guint frames;
static double fps;
static gchar* stream_error;
static guint calibration_id;
static gchar* calibration_message;

static void close_stream(void) {
    if (stream) { vdo_stream_stop(stream); g_clear_object(&stream); }
    g_clear_pointer(&calibration_message,g_free);
    stream_fd=-1; last_frame=0; fps=0; frames=0; rate_start=0;
    inspection_reset(&inspection);
}
static void fail_stream(const char* message) {
    g_free(stream_error); stream_error=g_strdup(message);
    syslog(LOG_WARNING,"VDO: %s",message);
    close_stream(); retry_at=g_get_monotonic_time()+5*G_USEC_PER_SEC;
}
static gboolean open_stream(void) {
    GError* error=NULL;
    g_autoptr(VdoMap) settings=vdo_map_new();
    vdo_map_set_double(settings,"framerate",5.0);
    vdo_map_set_uint32(settings,"buffer.count",2);
    stream=vdo_stream_nv12_new(settings,1,(VdoResolution){.width=640,.height=360},&error);
    if (!stream) goto failed;
    stream_fd=vdo_stream_get_fd(stream,&error);
    if (stream_fd<0 || !vdo_stream_start(stream,&error)) goto failed;
    VdoMap* info=vdo_stream_get_info(stream,&error);
    if (!info) goto failed;
    width=vdo_map_get_uint32(info,"width",0);
    height=vdo_map_get_uint32(info,"height",0);
    pitch=vdo_map_get_uint32(info,"pitch",0);
    guint format=vdo_map_get_uint32(info,"format",0);
    g_object_unref(info);
    if (!width || !height || width>4096 || height>4096 || pitch<width || format!=VDO_FORMAT_YUV) {
        fail_stream("Unsupported NV12 stream layout"); return FALSE;
    }
    g_clear_pointer(&stream_error,g_free);
    rate_start=g_get_monotonic_time();
    syslog(LOG_INFO,"VDO NV12 %ux%u, pitch %u; analysis preview %dx%d",width,height,pitch,WIDTH,HEIGHT);
    return TRUE;
failed:
    fail_stream(error ? error->message : "Cannot open stream");
    g_clear_error(&error); return FALSE;
}
static void parameter_changed(const gchar* name,const gchar* value,gpointer unused) {
    (void)unused;
    if (config_apply(&config,name,value)) {
        /* Parameter callbacks and capture tick share the main context. Invalidate
         * the reference conservatively on any setting change. */
        inspection_reset(&inspection); g_clear_pointer(&calibration_message,g_free); changed_at=g_get_monotonic_time();
    } else syslog(LOG_WARNING,"Rejected parameter %s",name);
}
static gboolean capture(gpointer unused) {
    (void)unused;
    gint64 now=g_get_monotonic_time();
    if (!stream && now>=retry_at) open_stream();
    if (status_server_take_calibration()) {
        calibration_id++;
        g_free(calibration_message);
        if (stream && last_frame && now-last_frame<3*G_USEC_PER_SEC && config.enabled && now-changed_at>G_USEC_PER_SEC) {
            inspection_calibrate(&inspection); calibration_message=g_strdup("Capturing ten empty-zone frames");
        } else calibration_message=g_strdup("Calibration rejected: enable monitoring, apply settings, and wait for fresh frames");
    }
    if (!stream) return G_SOURCE_CONTINUE;
    struct pollfd p={.fd=stream_fd,.events=POLLIN};
    int result=poll(&p,1,0);
    if (result<0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL))) { fail_stream("Stream disconnected"); return G_SOURCE_CONTINUE; }
    if (p.revents&POLLIN) {
        GError* error=NULL;
        VdoBuffer* buffer=vdo_stream_get_buffer(stream,&error);
        if (!buffer && g_error_matches(error,VDO_ERROR,VDO_ERROR_NO_DATA)) { g_clear_error(&error); }
        else if (!buffer) { fail_stream(error ? error->message : "Cannot fetch frame"); g_clear_error(&error); return G_SOURCE_CONTINUE; }
        else {
            now=g_get_monotonic_time();
            VdoFrame* frame=vdo_buffer_get_frame(buffer);
            const guint8* data=vdo_buffer_get_data(buffer);
            gboolean valid=frame && vdo_frame_get_timestamp(frame) <= (guint64)now &&
                (guint64)now-vdo_frame_get_timestamp(frame)<3*G_USEC_PER_SEC && sample_luma(data,MIN(vdo_frame_get_size(frame),vdo_buffer_get_capacity(buffer)),width,height,pitch,inspection.image);
            /* No VDO-owned pointer survives this return. Analysis uses our copy. */
            gboolean returned=vdo_stream_buffer_unref(stream,&buffer,&error);
            if (!valid || !returned) {
                g_clear_object(&buffer);
                fail_stream(error ? error->message : "Invalid or truncated luma frame"); g_clear_error(&error); return G_SOURCE_CONTINUE;
            }
            last_frame=now; frames++;
            if (now-rate_start>=G_USEC_PER_SEC) { fps=frames*1e6/(now-rate_start); frames=0; rate_start=now; }
            inspection_process(&inspection,&config,now);
        }
    }
    if (now-(last_frame ? last_frame : rate_start)>3*G_USEC_PER_SEC) fail_stream("No camera frames for three seconds; recalibrate after reconnection");
    return G_SOURCE_CONTINUE;
}
static gboolean publish(gpointer unused) {
    (void)unused;
    gint64 now=g_get_monotonic_time();
    gboolean fresh=stream && last_frame && now-last_frame<3*G_USEC_PER_SEC;
    gchar* image=fresh ? g_base64_encode(inspection.image,PIXELS) : g_strdup("");
    gchar* zone=g_strdup_printf("%u,%u,%u,%u",config.x,config.y,config.w,config.h);
    json_t* root=json_object();
#define SETS(k,v) json_object_set_new(root,k,json_string(v))
#define SETI(k,v) json_object_set_new(root,k,json_integer(v))
#define SETB(k,v) json_object_set_new(root,k,json_boolean(v))
    SETS("state",inspection_state(&inspection,&config,fresh,now));
    SETS("error",stream_error ? stream_error : ""); SETS("image",image);
    SETS("zone",zone); SETB("enabled",config.enabled); SETB("fresh",fresh);
    SETB("calibrated",inspection.calibrated); SETB("calibrating",inspection.calibrating);
    SETI("calibrationFrames",inspection.calibration); SETI("calibrationId",calibration_id);
    SETS("calibrationMessage",calibration_message ? calibration_message : "");
    SETI("pixelDelta",config.delta); SETI("occupiedPercent",config.percent); SETI("dwellSeconds",config.dwell);
    SETI("width",width); SETI("height",height); SETI("pitch",pitch);
    SETI("sampleWidth",WIDTH); SETI("sampleHeight",HEIGHT);
    json_object_set_new(root,"score",json_real(inspection.score));
    json_object_set_new(root,"fps",json_real(fresh ? fps : 0));
    SETI("occupiedSeconds",fresh && inspection.occupied ? (now-inspection.occupied_since)/G_USEC_PER_SEC : 0);
    char* text=json_dumps(root,JSON_COMPACT);
    status_server_publish(text); free(text); json_decref(root); g_free(image); g_free(zone);
    return G_SOURCE_CONTINUE;
}
static gboolean stop(gpointer loop) { g_main_loop_quit(loop); return G_SOURCE_CONTINUE; }
int main(void) {
    openlog(APP,LOG_PID,LOG_USER); signal(SIGPIPE,SIG_IGN);
    GError* error=NULL;
    AXParameter* parameters=ax_parameter_new(APP,&error);
    GMainLoop* loop=g_main_loop_new(NULL,FALSE);
    guint registered=0; int result=EXIT_FAILURE;
    if (!parameters) goto cleanup;
    for (guint i=0;i<G_N_ELEMENTS(names);i++) {
        gchar* value=NULL;
        if (!ax_parameter_get(parameters,names[i],&value,&error)) goto cleanup;
        gboolean valid=config_apply(&config,names[i],value); g_free(value);
        if (!valid) { syslog(LOG_ERR,"Invalid saved setting %s",names[i]); goto cleanup; }
        if (!ax_parameter_register_callback(parameters,names[i],parameter_changed,NULL,&error)) goto cleanup;
        registered++;
    }
    if (!status_server_start()) { syslog(LOG_ERR,"Cannot start status service"); goto cleanup; }
    publish(NULL);
    guint tick=g_timeout_add(50,capture,NULL), status=g_timeout_add(500,publish,NULL);
    guint term=g_unix_signal_add(SIGTERM,stop,loop), interrupt=g_unix_signal_add(SIGINT,stop,loop);
    g_main_loop_run(loop);
    g_source_remove(tick); g_source_remove(status); g_source_remove(term); g_source_remove(interrupt);
    result=EXIT_SUCCESS;
cleanup:
    if (error) { syslog(LOG_ERR,"%s",error->message); g_clear_error(&error); }
    close_stream(); status_server_stop();
    if (parameters) {
        for (guint i=0;i<registered;i++) ax_parameter_unregister_callback(parameters,names[i]);
        ax_parameter_free(parameters);
    }
    g_main_loop_unref(loop); g_free(stream_error); g_free(calibration_message); closelog(); return result;
}
