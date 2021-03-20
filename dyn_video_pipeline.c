//
// Created by Milos Pesic on 13.03.21.
//

#include <gst/gst.h>
#include <glib.h>


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


//Configuration
typedef struct {
    gchar *file_sink;
    gchar *source1;
    gchar *source2;
    gchar *source3;
} Config;

static Config config;

static GOptionEntry entries[] =
{
        { "filesink", 'f', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &(config.file_sink), "Optional file sink location", "" },
        { "source1", '1', 0, G_OPTION_ARG_STRING, &(config.source1), "Source 1 location", "" },
        { "source2", '2', 0, G_OPTION_ARG_STRING, &(config.source2), "Source 2 location", "" },
        { "source3", '3', 0, G_OPTION_ARG_STRING, &(config.source3), "Source 3 location", "" },
};

typedef struct {
    GstElement *pipeline;
    //Sources
    GstElement *source;
    GstElement *source1;
    GstElement *source2;

    // mixers
    GstElement *video_mixer;
    GstElement *audio_mixer;

    // video caps
    GstElement *video_capsfilter;

    // encoders
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
    GMutex lock;
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[]) {

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("- test tree model performance");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error))
    {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }


    CustomData data = {.linked_audio_pads = 0, .linked_pads = 0};
    GstBus *bus = NULL;
    GstMessage *msg = NULL;
    GstCaps *caps = NULL;
    gboolean terminate = FALSE;

    gst_init(&argc, &argv);

    g_mutex_init(&data.lock);
    data.source = gst_element_factory_make("uridecodebin", "source");
    data.source1 = gst_element_factory_make("uridecodebin", "source1");
    data.source2 = gst_element_factory_make("uridecodebin", "source2");

    data.video_mixer = gst_element_factory_make("compositor", "video_mixer");
    data.audio_mixer = gst_element_factory_make("audiomixer", "audio_mixer");

    data.video_capsfilter = gst_element_factory_make("capsfilter","video_mixer_capsfilter");
    caps = gst_caps_from_string(VIDEO_CAPS);
    g_object_set (data.video_capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);


    data.x264_encoder = gst_element_factory_make("x264enc", "video-enc-1");
    data.aac_encoder = gst_element_factory_make("avenc_aac", "aac_encoder");

    data.flv_muxer = gst_element_factory_make("flvmux", "muxer");
    data.queue = gst_element_factory_make("queue", "buffer");

    if (config.file_sink) {
        data.sink = gst_element_factory_make("filesink", "file-sink");
        g_object_set(data.sink, "location", config.file_sink, NULL);
    } else {
        data.sink = gst_element_factory_make("rtmpsink", "rtmp-sink");
        g_object_set(data.sink, "location", "rtmp://127.0.0.1/live live=true", NULL);
    }


    data.pipeline = gst_pipeline_new("test-pipeline");
    if (!data.pipeline || !data.source || !data.source1 || !data.source2 || !data.video_mixer
        || !data.video_capsfilter || !data.x264_encoder || !data.flv_muxer
        || !data.queue || !data.sink || !data.aac_encoder || !data.audio_mixer) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    }

    gst_bin_add_many(GST_BIN(data.pipeline),
            data.source, data.source1, data.source2,
            data.video_mixer,
            data.video_capsfilter,
            data.audio_mixer,
            data.x264_encoder,
            data.aac_encoder,
            data.flv_muxer,
            data.queue,
            data.sink, NULL);

    // Link video chain
    if (!gst_element_link_many(
            data.video_mixer,
            data.video_capsfilter,
            data.x264_encoder,
            data.flv_muxer,
            data.queue,
            data.sink, NULL)) {
        g_printerr("Video elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    // Link audio chain
    if (!gst_element_link_many(data.audio_mixer, data.aac_encoder, data.flv_muxer, NULL)) {
        g_printerr("Failed to link audio branch");
        gst_object_unref(data.pipeline);
        return -1;
    }


//    g_object_set(data.source, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm",
//                 NULL);
    g_object_set(data.source, "uri", config.source1, NULL);
    g_object_set(data.source1, "uri", config.source2, NULL);
    g_object_set(data.source2, "uri", config.source3, NULL);
    //g_object_set(data.source1, "uri", "https://dl8.webmfiles.org/big-buck-bunny_trailer.webm",
    //             NULL);

    //Black background
    g_object_set (data.video_mixer, "background", 1, NULL);


    g_object_set(data.queue, "max-size-time", 5 * GST_SECOND, NULL);

    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);
    g_signal_connect(data.source1, "pad-added", G_CALLBACK(pad_added_handler), &data);
    g_signal_connect(data.source2, "pad-added", G_CALLBACK(pad_added_handler), &data);


    GstStateChangeReturn ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        gst_object_unref (data.pipeline);
        return -1;
    }

    bus = gst_element_get_bus(data.pipeline);

    do {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (msg) {
            GError *error = NULL;
            gchar *debug_info = NULL;

            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &error, &debug_info);
                    g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), error->message);
                    g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
                    g_clear_error (&error);
                    g_free (debug_info);
                    terminate = TRUE;
                    break;
                case GST_MESSAGE_EOS:
                    g_print ("End-Of-Stream reached.\n");
                    terminate = TRUE;
                    break;
                case GST_MESSAGE_STATE_CHANGED:
                    if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
                        GstState old_state, new_state, pending_state;
                        gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
                        g_print ("Pipeline state changed from %s to %s:\n",
                                 gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
                        gst_debug_bin_to_dot_file(GST_BIN(data.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline");
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
    gst_element_set_state (data.pipeline, GST_STATE_NULL);
    gst_object_unref (data.pipeline);
    return 0;
}

static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
    GstPadTemplate *mixer_sink_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->video_mixer), "sink_%u");
    GstPadTemplate *audio_mixer_sink_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(data->audio_mixer), "sink_%u");

    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    GstPad *mixer_sink_pad = NULL;
    GstPad *audio_mixer_sink_pad = NULL;
    const gchar *new_pad_type = NULL;
    GstPadLinkReturn ret;

    GstElement *video_capsfilter = NULL;
    GstCaps *size_caps = NULL;

    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);

    g_mutex_lock(&data->lock);

    if (g_str_has_prefix (new_pad_type, "video/x-raw")) {
        GstElement *video_scaler = NULL;
        if (data->linked_pads == MAX_SOURCES) {
            // assume sources have only one video track for simplicity.
            g_print ("We are already linked. Ignoring.\n");
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
        g_object_set(video_scaler, "add-borders", TRUE, NULL);

        size_caps = gst_caps_from_string(SOURCE_SIZE_CAPS);
        g_object_set (video_capsfilter, "caps", size_caps, NULL);
        gst_caps_unref(size_caps);

        gst_bin_add_many(GST_BIN(data->pipeline), video_scaler, video_capsfilter, NULL);

        if (!gst_element_sync_state_with_parent(video_scaler) || !gst_element_sync_state_with_parent(video_capsfilter)) {
            g_printerr("Failed to sync state of a video scaler or capsfilter with the pipeline's state\n");
            goto exit;
        }

        // Requesting new sink pad from mixer
        mixer_sink_pad = gst_element_request_pad (data->video_mixer, mixer_sink_pad_template, NULL, NULL);
        if (!mixer_sink_pad) {
            g_printerr("failed to get mixer sink pad\n");
            goto exit;
        }
        g_object_set(mixer_sink_pad, "xpos", data->linked_pads * 640, NULL);
        g_object_set(mixer_sink_pad, "ypos", 0, NULL);
        g_object_set(mixer_sink_pad, "width", 640, NULL);
        g_object_set(mixer_sink_pad, "height", 1080, NULL);


        GstPad *scaler_sink_pad = gst_element_get_static_pad(video_scaler, "sink");
        GstPad *scaler_src_pad = gst_element_get_static_pad(video_scaler, "src");

        // linking decodebin and scaler
        ret = gst_pad_link(new_pad, scaler_sink_pad);
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
        data->linked_pads++;

    } else if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        //TODO: remove limit
        if (data->linked_audio_pads == MAX_SOURCES) {
            // assume sources have only one audio track for simplicity.
            g_print ("We are already linked. Ignoring.\n");
            goto exit;
        }
        // dyn. part
        GstCaps *caps_to_use;
        GstElement *audio_convert, *audio_resample, *audio_capsfilter;

        caps_to_use = gst_caps_from_string(AUDIO_CAPS);
        audio_mixer_sink_pad = gst_element_request_pad (data->audio_mixer, audio_mixer_sink_pad_template, NULL, NULL);
        if (!audio_mixer_sink_pad) {
            g_printerr("failed to get mixer sink pad");
            goto exit;
        }

        if (!gst_caps_is_equal(caps_to_use, new_pad_caps)) {
            // convert audio
            audio_resample = gst_element_factory_make("audioresample", NULL);
            audio_convert = gst_element_factory_make("audioconvert", NULL);
            audio_capsfilter = gst_element_factory_make("capsfilter", NULL);
            g_object_set(audio_capsfilter, "caps", caps_to_use, NULL);
            gst_bin_add_many(GST_BIN(data->pipeline), audio_resample, audio_convert, audio_capsfilter, NULL);

            if (!gst_element_link_many(audio_resample, audio_convert, audio_capsfilter, NULL)) {
                g_printerr("Failed to link audio chain");
                goto exit;
            }
            if (!gst_element_sync_state_with_parent(audio_resample) ||
                !gst_element_sync_state_with_parent(audio_convert) ||
                !gst_element_sync_state_with_parent(audio_capsfilter)) {
                g_printerr("Failed to sync new audio el. state with parent");
                goto exit;
            }
            // now link decodebin -> resample && capsfilter -> mixer
            GstPad *resample_sink_pad = gst_element_get_static_pad (audio_resample, "sink");
            ret = gst_pad_link(new_pad, resample_sink_pad);
            gst_object_unref(resample_sink_pad);
            if (GST_PAD_LINK_FAILED (ret)) {
                g_printerr("Failed to link decodebin and resampler");
                goto exit;
            }
            GstPad *caps_filter_src_pad = gst_element_get_static_pad (audio_capsfilter, "src");
            ret = gst_pad_link(caps_filter_src_pad, audio_mixer_sink_pad);
            gst_object_unref(caps_filter_src_pad);
            if (GST_PAD_LINK_FAILED (ret)) {
                g_printerr ("Failed to link capsfilter and mixer");
            } else {
                g_print ("Link succeeded (type '%s').\n", new_pad_type);
                data->linked_audio_pads++;
            }
        } else {
            // pass through link directly to mixer
            ret = gst_pad_link(new_pad, audio_mixer_sink_pad);
            if (GST_PAD_LINK_FAILED (ret)) {
                g_print ("Type is '%s' but link failed.\n", new_pad_type);
            } else {
                g_print ("Link succeeded (type '%s').\n", new_pad_type);
                data->linked_audio_pads++;
            }
        }
    }

    exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    /* Unreference the sink pad */
    if (mixer_sink_pad) {
        gst_object_unref(mixer_sink_pad);
    }
    if (audio_mixer_sink_pad) {
        gst_object_unref(audio_mixer_sink_pad);
    }
    g_mutex_unlock(&data->lock);
}