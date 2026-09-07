#include "../include/ps5_platform.h"
#include "../xash/platform_ps5/audio_ps5.h"

#include <assert.h>

int main(void)
{
    assert(offsetof(struct ps5_batch_map_entry, protection) == 0x18u);
    assert(offsetof(struct ps5_batch_map_entry, memory_type) == 0x19u);
    assert(sizeof(struct ps5_video_attribute) == 80u);
    assert(sizeof(struct ps5_pad_data) == 120u);
    assert(offsetof(struct ps5_pad_data, left_stick) == 4u);
    assert(offsetof(struct ps5_pad_data, right_stick) == 6u);
    assert(offsetof(struct ps5_pad_data, l2) == 8u);
    assert(offsetof(struct ps5_pad_data, connected) == 76u);
    assert(offsetof(struct ps5_pad_data, timestamp) == 80u);
    assert(offsetof(struct ps5_pad_data, connected_count) == 104u);
    assert(offsetof(struct ps5_pad_data, device_unique_data_length) == 107u);
    assert(offsetof(struct ps5_pad_data, device_unique_data) == 108u);

    /*
     * libSceAudioOut PCM contract. The declarations must keep the six-argument
     * open signature and the port/format constants the accepted run used, so a
     * silent ABI drift cannot reach hardware.
     */
    {
        int (*open_fn)(int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t) =
            sceAudioOutOpen;
        int (*volume_fn)(int32_t, int32_t, const int32_t *) = sceAudioOutSetVolume;
        int (*output_fn)(int32_t, const void *) = sceAudioOutOutput;
        int (*init_fn)(void) = sceAudioOutInit;
        int (*close_fn)(int32_t) = sceAudioOutClose;

        assert(open_fn && volume_fn && output_fn && init_fn && close_fn);
    }
    assert(PS5_AUDIO_PORT_TYPE_MAIN == 0);
    assert(PS5_AUDIO_PORT_INDEX == 0);
    assert(PS5_AUDIO_USER_SYSTEM == 0xff);
    assert(PS5_AUDIO_FORMAT_S16_STEREO == 1u);
    assert(PS5_AUDIO_CHANNELS == 2u);
    assert(PS5_AUDIO_GRAIN == 256u);
    assert(PS5_AUDIO_GRAIN % 256u == 0u && PS5_AUDIO_GRAIN <= 2048u);
    assert(PS5_AUDIO_BLOCK_SHORTS == PS5_AUDIO_GRAIN * PS5_AUDIO_CHANNELS);
    assert(PS5_AUDIO_OUTPUT_RATE == 48000u);
    assert(PS5_AUDIO_INPUT_RATE == 44100u);
    assert(PS5_AUDIO_VOLUME_FLAGS == 3);
    assert(PS5_AUDIO_VOLUME_0DB == 0x8000);
    assert(PS5_AUDIO_VOLUME_CHANNELS == 8);
    /* 44100:48000 reduces to exactly 147:160. */
    assert(PS5_AUDIO_INPUT_RATE * PS5_AUDIO_RATIO_DEN ==
           PS5_AUDIO_OUTPUT_RATE * PS5_AUDIO_RATIO_NUM);
    /* One staging chunk's output plus a sub-block remainder must fit. */
    assert((PS5_AUDIO_STAGE_FRAMES * PS5_AUDIO_RATIO_DEN + PS5_AUDIO_RATIO_NUM - 1)
               / PS5_AUDIO_RATIO_NUM + PS5_AUDIO_GRAIN - 1 <= PS5_AUDIO_ACCUM_FRAMES);
    return 0;
}
