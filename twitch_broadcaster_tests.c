#include "twitch_broadcaster.h"
#include <glib.h>
#include <gst/gst.h>

gboolean test_init_with_incomplete_config_returns_error() {
    twitch_broadcaster *broadcaster = twitch_broadcaster_new();
    Config *config = g_malloc0(sizeof(Config));
    gboolean ret = TRUE;

    if (twitch_broadcaster_init(broadcaster, config) != -1) {
        g_printerr("test_init_with_incomplete_config_returns_error FAILED\n");
        ret = FALSE;
    }
    twitch_broadcaster_destroy(broadcaster);
    return ret;
}

gboolean test_run_without_init_returns_error() {
    twitch_broadcaster *broadcaster = twitch_broadcaster_new();
    gboolean ret = TRUE;
    if (twitch_broadcaster_run(broadcaster) != -1) {
        g_printerr("test_run_without_init_returns_error FAILED\n");
        ret = FALSE;
    }
    twitch_broadcaster_destroy(broadcaster);
    return ret;
}

gboolean test_mixing_3_sources_creates_correct_file() {
    twitch_broadcaster *broadcaster = twitch_broadcaster_new();
    Config *config = g_malloc0(sizeof(Config));
    gboolean ret = TRUE;
    config->source1 = "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm";
    config->source2 = "https://dl8.webmfiles.org/big-buck-bunny_trailer.webm";
    config->source3 = "https://dl8.webmfiles.org/elephants-dream.webm";
    config->file_sink = "mixed.flv";

    if(twitch_broadcaster_init(broadcaster, config) != 0) {
        ret = FALSE;
        goto exit;
    }
    if (twitch_broadcaster_run(broadcaster) != 0) {
        ret = FALSE;
        goto exit;
    }
    if (!g_file_test(config->file_sink, G_FILE_TEST_EXISTS)) {
        ret = FALSE;
        goto exit;
    }
    // TODO: more checks on produced file
    // eg. using gst_discoverer* API
    exit:
    g_free(config);
    twitch_broadcaster_destroy(broadcaster);
    return ret;
};

int main(int argc, char *argv[]) {
    g_print("RUNNING ALL TESTS!");
    gst_init(&argc, &argv);
    gboolean res = test_init_with_incomplete_config_returns_error();
    res = res && test_run_without_init_returns_error();
    res = res && test_mixing_3_sources_creates_correct_file();

    if (!res) {
        g_printerr("Some tests failed!\n");
    } else {
        g_print("ALL GOOD!\n");
    }
}