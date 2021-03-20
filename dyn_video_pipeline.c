//
// Created by Milos Pesic on 13.03.21.
//

#include <gst/gst.h>
#include <glib.h>

#include "twitch_broadcaster.h"

static Config config;

static GOptionEntry entries[] =
{
        { "filesink", 'f', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &(config.file_sink), "Optional file sink location, if present overwrites rtmp", "" },
        { "source1", '1', 0, G_OPTION_ARG_STRING, &(config.source1), "Source 1 location", "" },
        { "source2", '2', 0, G_OPTION_ARG_STRING, &(config.source2), "Source 2 location", "" },
        { "source3", '3', 0, G_OPTION_ARG_STRING, &(config.source3), "Source 3 location", "" },
        { "rtmp_address", 'r', 0, G_OPTION_ARG_STRING, &(config.rtmp_address), "rtmp ddress", "" },
};

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
    gst_init(&argc, &argv);

    twitch_broadcaster *broadcaster = twitch_broadcaster_new();

    twitch_broadcaster_init(broadcaster, &config);

    twitch_broadcaster_run(broadcaster);

    twitch_broadcaster_destroy(broadcaster);
}