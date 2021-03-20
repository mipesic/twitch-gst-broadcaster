#ifndef _TWITCH_BROADCASTER_H_
#define _TWITCH_BROADCASTER_H_

struct broadcaster_impl;

// Config for the broadcaster
typedef struct {
    char *file_sink; // if present indicates usage of filesink instead of rtmp (testing)
    // Source addresses (could be uri or localfile (file://...)
    char *source1;
    char *source2;
    char *source3;
    char *rtmp_address;
} Config;

typedef struct twitch_broadcaster {
    struct broadcaster_impl *impl;
} twitch_broadcaster;

/**
 * Allocates memory for a new broadcaster instance
 * @return twitch_broadcaster* pointing to newly allocated instance
 */
twitch_broadcaster* twitch_broadcaster_new();

/**
 * Initialises media pipeline according to the given config.
 * If succeeds broadcasting could be started by invoking twitch_broadcaster_run()
 * @return non 0 on failure, 0 otherwise
 */
int twitch_broadcaster_init(twitch_broadcaster *self, Config *config);

/**
 * Runs broadcaster
 * Starts media pipeline which will read data from give sources, process it
 * and sends it to configured location.
 * Function call will return only upon finishing the broadcast.
 * @return non 0 on failure, 0 otherwise
 */
int twitch_broadcaster_run(twitch_broadcaster *self);


/**
 * Releases all resources allocated by the given broadcaster instance.
 * Safe to call multiple times.
 */
void twitch_broadcaster_destroy(twitch_broadcaster *self);



#endif
