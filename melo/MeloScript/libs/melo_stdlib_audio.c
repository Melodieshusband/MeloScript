#include "melo_stdlib_audio.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    int active;
    int paused;
    int streaming;
    int volume;
    int pan;
} MeloAudioChanState;

static MeloAudioChanState chan_state[MELO_AUDIO_MAX_CHANNELS];
static int master_volume = MELO_AUDIO_VOL_MAX;

static int melo_audio_clamp_vol(int v) {
    if (v < MELO_AUDIO_VOL_MIN) return MELO_AUDIO_VOL_MIN;
    if (v > MELO_AUDIO_VOL_MAX) return MELO_AUDIO_VOL_MAX;
    return v;
}

static int melo_audio_scaled_vol(int channel_vol) {
    int v = (channel_vol * master_volume) / MELO_AUDIO_VOL_MAX;
    return melo_audio_clamp_vol(v);
}

#ifdef MELO_PS2_TARGET
#include <libsd.h>
#include <sifrpc.h>

static int audio_inited = 0;

int melo_audio_backend_init(void) {
    if (audio_inited) return 0;
    if (sceSdInit(SD_INIT_COLD) < 0) return -1;
    memset(chan_state, 0, sizeof(chan_state));
    master_volume = MELO_AUDIO_VOL_MAX;
    audio_inited = 1;
    return 0;
}

static void melo_audio_apply_volpan(int channel) {
    int vol = melo_audio_scaled_vol(chan_state[channel].volume);
    sceSdSetParam(channel | SD_PARAM_VOLL, vol);
    sceSdSetParam(channel | SD_PARAM_VOLR, vol);
    (void)chan_state[channel].pan;
}

int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan, int loop) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    sceSdSetTransIntrHandler(0, NULL, NULL);
    sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, (void *)sample_addr, 0, sample_size);
    chan_state[channel].volume = melo_audio_clamp_vol(volume);
    chan_state[channel].pan = pan;
    melo_audio_apply_volpan(channel);
    sceSdSetSwitch(channel | SD_SW_NOISE_OFF, 0);
    if (loop) {
        sceSdSetAddr(channel | SD_ADDR_LSAX, sample_addr);
    }
    sceSdSetSwitch(channel | SD_SW_KEY_ON, 0);
    chan_state[channel].active = 1;
    chan_state[channel].paused = 0;
    chan_state[channel].streaming = 0;
    return 0;
}

void melo_audio_backend_stop(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    sceSdSetSwitch(channel | SD_SW_KEY_OFF, 0);
    chan_state[channel].active = 0;
    chan_state[channel].paused = 0;
    chan_state[channel].streaming = 0;
}

int melo_audio_backend_playing(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return chan_state[channel].active && !chan_state[channel].paused;
}

int melo_audio_backend_paused(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return chan_state[channel].paused;
}

void melo_audio_backend_pause(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    if (!chan_state[channel].active || chan_state[channel].paused) return;
    sceSdSetParam(channel | SD_PARAM_VOLL, 0);
    sceSdSetParam(channel | SD_PARAM_VOLR, 0);
    chan_state[channel].paused = 1;
}

void melo_audio_backend_resume(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    if (!chan_state[channel].active || !chan_state[channel].paused) return;
    melo_audio_apply_volpan(channel);
    chan_state[channel].paused = 0;
}

void melo_audio_backend_set_volume(int channel, int volume) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    chan_state[channel].volume = melo_audio_clamp_vol(volume);
    if (chan_state[channel].active && !chan_state[channel].paused) {
        melo_audio_apply_volpan(channel);
    }
}

void melo_audio_backend_set_pan(int channel, int pan) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    chan_state[channel].pan = pan;
}

void melo_audio_backend_set_master_volume(int volume) {
    master_volume = melo_audio_clamp_vol(volume);
    for (int i = 0; i < MELO_AUDIO_MAX_CHANNELS; i++) {
        if (chan_state[i].active && !chan_state[i].paused) {
            melo_audio_apply_volpan(i);
        }
    }
}

int melo_audio_backend_stream_feed(int channel, uint32_t chunk_addr, uint32_t chunk_size) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    if (!chan_state[channel].streaming) return -1;
    sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, (void *)chunk_addr, 0, chunk_size);
    return 0;
}
#else

int melo_audio_backend_init(void) {
    memset(chan_state, 0, sizeof(chan_state));
    master_volume = MELO_AUDIO_VOL_MAX;
    return 0;
}

int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan, int loop) {
    (void)sample_addr; (void)sample_size;
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    chan_state[channel].active = 1;
    chan_state[channel].paused = 0;
    chan_state[channel].streaming = 0;
    chan_state[channel].volume = melo_audio_clamp_vol(volume);
    chan_state[channel].pan = pan;
    (void)loop;
    return 0;
}

void melo_audio_backend_stop(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    chan_state[channel].active = 0;
    chan_state[channel].paused = 0;
    chan_state[channel].streaming = 0;
}

int melo_audio_backend_playing(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return chan_state[channel].active && !chan_state[channel].paused;
}

int melo_audio_backend_paused(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return chan_state[channel].paused;
}

void melo_audio_backend_pause(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    if (!chan_state[channel].active) return;
    chan_state[channel].paused = 1;
}

void melo_audio_backend_resume(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    if (!chan_state[channel].active) return;
    chan_state[channel].paused = 0;
}

void melo_audio_backend_set_volume(int channel, int volume) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    chan_state[channel].volume = melo_audio_clamp_vol(volume);
}

void melo_audio_backend_set_pan(int channel, int pan) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    chan_state[channel].pan = pan;
}

void melo_audio_backend_set_master_volume(int volume) {
    master_volume = melo_audio_clamp_vol(volume);
}

int melo_audio_backend_stream_feed(int channel, uint32_t chunk_addr, uint32_t chunk_size) {
    (void)chunk_addr; (void)chunk_size;
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    if (!chan_state[channel].streaming) return -1;
    return 0;
}
#endif

static int native_audio_init(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 0) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_init() takes no arguments");
        return -1;
    }
    int rc = melo_audio_backend_init();
    *out_result = melo_int(rc == 0 ? 1 : 0);
    return 0;
}

static int melo_audio_check_channel(MeloVM *vm, int32_t channel, const char *fn_name) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "%s: channel out of range (0-%d)", fn_name, MELO_AUDIO_MAX_CHANNELS - 1);
        return -1;
    }
    return 0;
}

static int native_audio_play(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 5 ||
        args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32 ||
        args[4].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_play(channel, sample_addr, sample_size, volume, pan) requires five int32 arguments");
        return -1;
    }
    int32_t channel = args[0].as.i32;
    if (melo_audio_check_channel(vm, channel, "audio_play") < 0) return -1;
    int rc = melo_audio_backend_play(channel, (uint32_t)args[1].as.i32,
                                      (uint32_t)args[2].as.i32, args[3].as.i32, args[4].as.i32, 0);
    *out_result = melo_int(rc == 0 ? 1 : 0);
    return 0;
}

static int native_audio_play_loop(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 5 ||
        args[0].type != MELO_INT32 || args[1].type != MELO_INT32 ||
        args[2].type != MELO_INT32 || args[3].type != MELO_INT32 ||
        args[4].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_play_loop(channel, sample_addr, sample_size, volume, pan) requires five int32 arguments");
        return -1;
    }
    int32_t channel = args[0].as.i32;
    if (melo_audio_check_channel(vm, channel, "audio_play_loop") < 0) return -1;
    int rc = melo_audio_backend_play(channel, (uint32_t)args[1].as.i32,
                                      (uint32_t)args[2].as.i32, args[3].as.i32, args[4].as.i32, 1);
    *out_result = melo_int(rc == 0 ? 1 : 0);
    return 0;
}

static int native_audio_pause(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_pause(channel: int32) requires one int32 argument");
        return -1;
    }
    if (melo_audio_check_channel(vm, args[0].as.i32, "audio_pause") < 0) return -1;
    melo_audio_backend_pause(args[0].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_resume(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_resume(channel: int32) requires one int32 argument");
        return -1;
    }
    if (melo_audio_check_channel(vm, args[0].as.i32, "audio_resume") < 0) return -1;
    melo_audio_backend_resume(args[0].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_paused(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_paused(channel: int32) requires one int32 argument");
        return -1;
    }
    if (melo_audio_check_channel(vm, args[0].as.i32, "audio_paused") < 0) return -1;
    *out_result = melo_int(melo_audio_backend_paused(args[0].as.i32));
    return 0;
}

static int native_audio_set_volume(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_set_volume(channel: int32, volume: int32) requires two int32 arguments");
        return -1;
    }
    if (melo_audio_check_channel(vm, args[0].as.i32, "audio_set_volume") < 0) return -1;
    melo_audio_backend_set_volume(args[0].as.i32, args[1].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_set_pan(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 2 || args[0].type != MELO_INT32 || args[1].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_set_pan(channel: int32, pan: int32) requires two int32 arguments");
        return -1;
    }
    if (melo_audio_check_channel(vm, args[0].as.i32, "audio_set_pan") < 0) return -1;
    melo_audio_backend_set_pan(args[0].as.i32, args[1].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_master_volume(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_master_volume(volume: int32) requires one int32 argument");
        return -1;
    }
    melo_audio_backend_set_master_volume(args[0].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_stop(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_stop(channel: int32) requires one int32 argument");
        return -1;
    }
    melo_audio_backend_stop(args[0].as.i32);
    *out_result = melo_nil();
    return 0;
}

static int native_audio_playing(MeloVM *vm, MeloValue *args, int arg_count, MeloValue *out_result) {
    if (arg_count != 1 || args[0].type != MELO_INT32) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg), "audio_playing(channel: int32) requires one int32 argument");
        return -1;
    }
    *out_result = melo_int(melo_audio_backend_playing(args[0].as.i32));
    return 0;
}

int melo_stdlib_register_audio(MeloVM *vm) {
    static const struct {
        const char *name;
        MeloNativeFn fn;
    } entries[] = {
        { "audio_init",          native_audio_init },
        { "audio_play",          native_audio_play },
        { "audio_play_loop",     native_audio_play_loop },
        { "audio_stop",          native_audio_stop },
        { "audio_playing",       native_audio_playing },
        { "audio_pause",         native_audio_pause },
        { "audio_resume",        native_audio_resume },
        { "audio_paused",        native_audio_paused },
        { "audio_set_volume",    native_audio_set_volume },
        { "audio_set_pan",       native_audio_set_pan },
        { "audio_master_volume", native_audio_master_volume },
    };
    int count = (int)(sizeof(entries) / sizeof(entries[0]));
    for (int i = 0; i < count; i++) {
        if (melo_vm_register_native(vm, entries[i].name, entries[i].fn) < 0) {
            return -1;
        }
    }
    return 0;
}

const char **melo_stdlib_audio_native_names(int *out_count) {
    static const char *names[] = {
        "audio_init", "audio_play", "audio_play_loop", "audio_stop", "audio_playing",
        "audio_pause", "audio_resume", "audio_paused",
        "audio_set_volume", "audio_set_pan", "audio_master_volume"
    };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
