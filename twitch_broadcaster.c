#include <gst/gst.h>
#include <glib.h>

#include "twitch_broadcaster.h"

/* Video and audio caps outputted by the mixers */
#define AUDIO_CAPS "audio/x-raw, format=(string)S16LE, " \
"layout=(string)interleaved, rate=(int)44100, channels=(int)2, " \
"channel-mask=(bitmask)0x03"

#define VIDEO_CAPS "video/x-raw, width=(int)1920 \
, height=(int)1080, framerate=(fraction)30/1, " \
"format=I420, pixel-aspect-ratio=(fraction)1/1"

#define SOURCE_SIZE_CAPS "video/x-raw, width=(int)640 \
, height=(int)1080,pixel-aspect-ratio=(fraction)1/1"

#define MAX_SOURCES 3


typedef struct broadcaster_impl {
    GstElement *pipeline;
    // Sources
    GstElement *source;
    GstElement *source1;
    GstElement *source2;

    // mixers
    GstElement *video_mixer;
    GstElement *audio_mixer;

    // Video caps for setting mixer out caps
    GstElement *video_capsfilter;

    // Encoders
    GstElement *x264_encoder;
    GstElement *aac_encoder;

    // muxers
    GstElement *flv_muxer;

    // Queue to decouple media processing from IO
    GstElement *queue;

    //Sink
    GstElement *sink;

    // private data
    int linked_pads;
    int linked_audio_pads;
    gboolean initialized;
    GMutex lock;

} broadcaster_impl;


//----------------------------------------------------------------------------------------
// Callbacks
static void pad_added_handler(GstElement *src, GstPad *new_pad, broadcaster_impl *data);

//-----------------------------------------------------------------------------------------
// Helper functions (added not as much for re-usability as for making the code more readable

// Creates all pipeline elements and configures them
gboolean twitch_broadcaster_create_elements(broadcaster_impl *self, Config *config);

// Add all created elements to the pipeline and links static part of the pipeline
gboolean twitch_broadcaster_configure_pipeline(broadcaster_impl *self);

// Wires new video track into the pipeline dynamically
gboolean twitch_broadcaster_wire_new_video_track(broadcaster_impl *self, GstPad *new_pad);

// Wires new audio track into the pipeline dynamically
gboolean twitch_broadcaster_wire_new_audio_track(broadcaster_impl *self, GstPad *new_pad);


//----------------------------------------------------------------------------------------
// API implementation

twitch_broadcaster* twitch_broadcaster_new() {
    twitch_broadcaster *instance = g_malloc0(sizeof(twitch_broadcaster));
    instance->impl = g_malloc0(sizeof(broadcaster_impl));
    return instance;
}

int twitch_broadcaster_init(twitch_broadcaster *self, Config *config) {
    g_mutex_init(&self->impl->lock);
    g_return_val_if_fail(self && config->source1 && config->source2 && config->source3, -1);
    if (!twitch_broadcaster_create_elements(self->impl, config)) {
        return -1;
    }
    if (!twitch_broadcaster_configure_pipeline(self->impl)) {
        return -1;
    }
    self->impl->initialized = TRUE;
    return 0;
}

int twitch_broadcaster_run(twitch_broadcaster *self) {
    GstBus *bus = NULL;
    GstMessage *msg = NULL;
    gboolean terminate = FALSE;
    int res = 0;

    g_return_val_if_fail(self && self->impl->initialized, -1);
    GstStateChangeReturn ret = gst_element_set_state(self->impl->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        gst_object_unref (self->impl->pipeline);
        return -1;
    }

    // pop and react on messages appearing on the pipeline's bus
    bus = gst_element_get_bus(self->impl->pipeline);
    do {
        msg = gst_bus_timed_pop_filtered(bus,
                GST_CLOCK_TIME_NONE,
                GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED);
        if (msg) {
            GError *error = NULL;
            gchar *debug_info = NULL;

            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &error, &debug_info);
                    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
                    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                    g_clear_error(&error);
                    g_free(debug_info);
                    terminate = TRUE;
                    res = -1;
                    break;
                case GST_MESSAGE_EOS:
                    g_print("End-Of-Stream reached.\n");
                    terminate = TRUE;
                    break;
                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->impl->pipeline)) {
                        GstState old_state, new_state, pending_state;
                        gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                        g_print("Pipeline state changed from %s to %s:\n",
                                 gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
                        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(self->impl->pipeline),
                                GST_DEBUG_GRAPH_SHOW_ALL,
                                "pipeline");
                    }
                    break;
                default:
                    /* We should not reach here */
                    g_printerr ("Unexpected message received.\n");
                    break;
            }
            gst_message_unref (msg);
        }
    } while(!terminate);

    /* Free resources */
    gst_object_unref (bus);
    gst_element_set_state (self->impl->pipeline, GST_STATE_NULL);
    gst_object_unref (self->impl->pipeline);
    return res;
}

void twitch_broadcaster_destroy(twitch_broadcaster *self) {
    if (self && self->impl) {
        if (self->impl->initialized) {
            g_mutex_clear(&(self->impl->lock));
        }
        g_free(self->impl);
        self->impl = NULL;
    }
    g_free(self);
}

//----------------------------------------------------------------------------------------
// Helpers && callbacks impl

static void pad_added_handler (GstElement *src, GstPad *new_pad, broadcaster_impl *data) {
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);

    g_mutex_lock(&data->lock);
    if (g_str_has_prefix (new_pad_type, "video/x-raw")) {
        twitch_broadcaster_wire_new_video_track(data, new_pad);

    } else if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        twitch_broadcaster_wire_new_audio_track(data, new_pad);
    }
    if (new_pad_caps != NULL) {
        gst_caps_unref(new_pad_caps);
    }
    g_mutex_unlock(&data->lock);
}

// Creates all static pipeline elements based on the provided config
gboolean twitch_broadcaster_create_elements(broadcaster_impl *self, Config *config) {
    GstCaps *caps = NULL;
    g_return_val_if_fail(self && config->source1 && config->source2 && config->source3, FALSE);
    self->source = gst_element_factory_make("uridecodebin", "source");
    self->source1 = gst_element_factory_make("uridecodebin", "source1");
    self->source2 = gst_element_factory_make("uridecodebin", "source2");

    self->video_mixer = gst_element_factory_make("compositor", "video_mixer");
    self->audio_mixer = gst_element_factory_make("audiomixer", "audio_mixer");
    self->video_capsfilter = gst_element_factory_make("capsfilter","video_mixer_capsfilter");
    caps = gst_caps_from_string(VIDEO_CAPS);
    g_object_set (self->video_capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    self->x264_encoder = gst_element_factory_make("x264enc", "video-enc-1");
    self->aac_encoder = gst_element_factory_make("avenc_aac", "aac_encoder");

    self->flv_muxer = gst_element_factory_make("flvmux", "muxer");
    self->queue = gst_element_factory_make("queue", "buffer");

    if (config->file_sink) {
        self->sink = gst_element_factory_make("filesink", "file-sink");
        g_object_set(self->sink, "location", config->file_sink, NULL);
    } else {
        self->sink = gst_element_factory_make("rtmpsink", "rtmp-sink");
        g_object_set(self->sink, "location", config->rtmp_address, NULL);
    }

    self->pipeline = gst_pipeline_new("test-pipeline");
    if (!self->pipeline || !self->source || !self->source1 || !self->source2 || !self->video_mixer
        || !self->video_capsfilter || !self->x264_encoder || !self->flv_muxer
        || !self->queue || !self->sink || !self->aac_encoder || !self->audio_mixer) {
        g_printerr ("Not all elements could be created.\n");
        return FALSE;
    }
    g_object_set(self->source, "uri", config->source1, NULL);
    g_object_set(self->source1, "uri", config->source2, NULL);
    g_object_set(self->source2, "uri", config->source3, NULL);
    //Black background
    g_object_set (self->video_mixer, "background", 1, NULL);
    // Decouple rtmpsink from the rest of the pipeline
    g_object_set(self->queue, "max-size-time", 5 * GST_SECOND, NULL);
    return TRUE;
}

// Adds created elements to the pipeline and links them. To be called once after
// twitch_broadcaster_create_elements
gboolean twitch_broadcaster_configure_pipeline(broadcaster_impl *self) {
    g_return_val_if_fail(self, FALSE);
    // Add all elements to the pipeline
    gst_bin_add_many(GST_BIN(self->pipeline),
                     self->source, self->source1, self->source2,
                     self->video_mixer,
                     self->video_capsfilter,
                     self->audio_mixer,
                     self->x264_encoder,
                     self->aac_encoder,
                     self->flv_muxer,
                     self->queue,
                     self->sink, NULL);

    // Link video chain
    if (!gst_element_link_many(
            self->video_mixer,
            self->video_capsfilter,
            self->x264_encoder,
            self->flv_muxer,
            self->queue,
            self->sink, NULL)) {
        g_printerr("Video elements could not be linked.\n");
        gst_object_unref(self->pipeline);
        return FALSE;
    }
    // Link audio chain
    if (!gst_element_link_many(
            self->audio_mixer,
            self->aac_encoder,
            self->flv_muxer,
            NULL)) {
        g_printerr("Failed to link audio branch");
        gst_object_unref(self->pipeline);
        return FALSE;
    }
    // Installing pad added handler to be able to reconfigure pipeline dynamically
    g_signal_connect(self->source, "pad-added", G_CALLBACK(pad_added_handler), self);
    g_signal_connect(self->source1, "pad-added", G_CALLBACK(pad_added_handler), self);
    g_signal_connect(self->source2, "pad-added", G_CALLBACK(pad_added_handler), self);
    return TRUE;
}

gboolean twitch_broadcaster_wire_new_video_track(broadcaster_impl *self, GstPad *new_pad) {
    g_return_val_if_fail(self && new_pad, FALSE);
    // Scaler to resize the signal but keep aspect ratio
    GstElement *video_scaler = NULL;
    GstElement *video_capsfilter = NULL;

    if (self->linked_pads == MAX_SOURCES) {
        // assume sources have only one video track for simplicity.
        g_print("We are already linked. Ignoring.\n");
        goto exit;
    }
    // time to link pads decodebin -> scaler (keeping aspect ratio) -> capsfilter -> video mixer
    video_scaler = gst_element_factory_make("videoscale", NULL);
    if (!video_scaler) {
        g_printerr("Failed to create scaler.\n");
        goto exit;
    }
    video_capsfilter = gst_element_factory_make("capsfilter", NULL);
    if (!video_capsfilter) {
        g_printerr("Failed to create capsfilter in front of mixer.\n");
        gst_object_unref(video_scaler);
        goto exit;
    }
    // keep aspect ratio
    g_object_set(video_scaler, "add-borders", TRUE, NULL);

    // make sure each source takes 1/3 of the output on the horizontal line
    GstCaps *size_caps = gst_caps_from_string(SOURCE_SIZE_CAPS);
    g_object_set (video_capsfilter, "caps", size_caps, NULL);
    gst_caps_unref(size_caps);

    gst_bin_add_many(GST_BIN(self->pipeline), video_scaler, video_capsfilter, NULL);

    if (!gst_element_sync_state_with_parent(video_scaler) ||
        !gst_element_sync_state_with_parent(video_capsfilter)) {
        g_printerr("Failed to sync state of a video scaler or capsfilter with the pipeline's state\n");
        gst_object_unref(video_scaler);
        gst_object_unref(video_capsfilter);
        goto exit;
    }

    // Requesting new sink pad from mixer
    GstPadTemplate *mixer_sink_pad_template = gst_element_class_get_pad_template(
            GST_ELEMENT_GET_CLASS(self->video_mixer),
            "sink_%u");
    GstPad *mixer_sink_pad =gst_element_request_pad(
            self->video_mixer,
            mixer_sink_pad_template,
            NULL, NULL);
    if (!mixer_sink_pad) {
        g_printerr("failed to get mixer sink pad\n");
        goto exit;
    }
    // Configure position and size in the output stream
    g_object_set(mixer_sink_pad, "xpos", self->linked_pads * 640, NULL);
    g_object_set(mixer_sink_pad, "ypos", 0, NULL);
    g_object_set(mixer_sink_pad, "width", 640, NULL);
    g_object_set(mixer_sink_pad, "height", 1080, NULL);


    GstPad *scaler_sink_pad = gst_element_get_static_pad(video_scaler, "sink");
    GstPad *scaler_src_pad = gst_element_get_static_pad(video_scaler, "src");

    // linking decodebin and scaler
    GstPadLinkReturn ret = gst_pad_link(new_pad, scaler_sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
        gst_object_unref(scaler_src_pad);
        gst_object_unref(scaler_sink_pad);
        g_printerr("Failed to link decodebin and scaler\n");
        goto exit;
    }

    // linking scaler and capsfilter
    if (!gst_element_link(video_scaler, video_capsfilter)) {
        gst_object_unref(scaler_src_pad);
        gst_object_unref(scaler_sink_pad);
        g_printerr("Failed to link scaler and capsfilter\n");
        goto exit;
    }

    // linking capsfilter and mixer
    GstPad *capsfilter_src_pad = gst_element_get_static_pad(video_capsfilter, "src");
    ret = gst_pad_link(capsfilter_src_pad, mixer_sink_pad);

    gst_object_unref(capsfilter_src_pad);
    gst_object_unref(scaler_sink_pad);

    if (GST_PAD_LINK_FAILED (ret)) {
        g_printerr("Faild to link capsfilter and mixer\n");
        goto exit;
    }
    self->linked_pads++;
    exit:
    /* Unreference the sink pad */
    if (mixer_sink_pad) {
        gst_object_unref(mixer_sink_pad);
    }
    return TRUE;
}

gboolean twitch_broadcaster_wire_new_audio_track(broadcaster_impl *self, GstPad *new_pad) {
    g_return_val_if_fail(self && new_pad, FALSE);
    GstPadTemplate *audio_mixer_sink_pad_template = gst_element_class_get_pad_template (
            GST_ELEMENT_GET_CLASS(self->audio_mixer),
            "sink_%u");
    GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
    //TODO: remove limit - assume one track per source for simplicity
    if (self->linked_audio_pads == MAX_SOURCES) {
        // assume sources have only one audio track for simplicity.
        g_print("We are already linked. Ignoring.\n");
        goto exit;
    }
    // dyn. part
    GstCaps *caps_to_use = NULL;
    GstElement *audio_convert, *audio_resample, *audio_capsfilter;

    caps_to_use = gst_caps_from_string(AUDIO_CAPS);
    GstPad *audio_mixer_sink_pad = gst_element_request_pad(
            self->audio_mixer, audio_mixer_sink_pad_template, NULL, NULL);
    if (!audio_mixer_sink_pad) {
        g_printerr("failed to get mixer sink pad!\n");
        goto exit;
    }

    if (!gst_caps_is_equal(caps_to_use, new_pad_caps)) {
        // convert audio
        audio_resample = gst_element_factory_make("audioresample", NULL);
        audio_convert = gst_element_factory_make("audioconvert", NULL);
        audio_capsfilter = gst_element_factory_make("capsfilter", NULL);
        g_object_set(audio_capsfilter, "caps", caps_to_use, NULL);
        gst_bin_add_many(GST_BIN(self->pipeline),
                audio_resample,
                audio_convert,
                audio_capsfilter, NULL);

        if (!gst_element_link_many(audio_resample, audio_convert, audio_capsfilter, NULL)) {
            g_printerr("Failed to link audio chain!\n");
            goto exit;
        }
        if (!gst_element_sync_state_with_parent(audio_resample) ||
            !gst_element_sync_state_with_parent(audio_convert) ||
            !gst_element_sync_state_with_parent(audio_capsfilter)) {
            g_printerr("Failed to sync new audio el. state with parent!\n");
            goto exit;
        }
        // now link decodebin -> resample && capsfilter -> mixer
        GstPad *resample_sink_pad = gst_element_get_static_pad (audio_resample, "sink");
        GstPadLinkReturn ret = gst_pad_link(new_pad, resample_sink_pad);
        gst_object_unref(resample_sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_printerr("Failed to link decodebin and resampler\n");
            goto exit;
        }
        GstPad *caps_filter_src_pad = gst_element_get_static_pad (audio_capsfilter, "src");
        ret = gst_pad_link(caps_filter_src_pad, audio_mixer_sink_pad);
        gst_object_unref(caps_filter_src_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_printerr("Failed to link capsfilter and mixer\n");
            goto exit;
        }
    } else {
        // pass through link directly to mixer
        GstPadLinkReturn ret = gst_pad_link(new_pad, audio_mixer_sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_print("Failed to link decodebin to audio mixer");
            goto exit;
        }
    }
    self->linked_audio_pads++;
    exit:
    if (new_pad_caps) {
        gst_caps_unref(new_pad_caps);
    }
    if (audio_mixer_sink_pad) {
        gst_object_unref(audio_mixer_sink_pad);
    }
    return TRUE;
}