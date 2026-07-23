#include "melo_stdlib_audio.h"
#include <string.h>
#include <stdio.h>

#ifdef MELO_PS2_TARGET
#include <libsd.h>
#include <sifrpc.h>

static int audio_inited = 0;
static int channel_active[MELO_AUDIO_MAX_CHANNELS];

int melo_audio_backend_init(void) {
    if (audio_inited) return 0;
    if (sceSdInit(SD_INIT_COLD) < 0) return -1;
    memset(channel_active, 0, sizeof(channel_active));
    audio_inited = 1;
    return 0;
}

int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    sceSdSetTransIntrHandler(0, NULL, NULL);
    sceSdVoiceTrans(channel, SD_TRANS_MODE_WRITE, (void *)sample_addr, 0, sample_size);
    sceSdSetParam(channel | SD_PARAM_VOLL, volume);
    sceSdSetParam(channel | SD_PARAM_VOLR, volume);
    sceSdSetSwitch(channel | SD_SW_NOISE_OFF, 0);
    sceSdSetSwitch(channel | SD_SW_KEY_ON, 0);
    (void)pan;
    channel_active[channel] = 1;
    return 0;
}

void melo_audio_backend_stop(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    sceSdSetSwitch(channel | SD_SW_KEY_OFF, 0);
    channel_active[channel] = 0;
}

int melo_audio_backend_playing(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return channel_active[channel];
}
#else
static int channel_active[MELO_AUDIO_MAX_CHANNELS];
static int audio_inited = 0;

int melo_audio_backend_init(void) {
    memset(channel_active, 0, sizeof(channel_active));
    audio_inited = 1;
    return 0;
}

int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan) {
    (void)sample_addr; (void)sample_size; (void)volume; (void)pan;
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return -1;
    channel_active[channel] = 1;
    return 0;
}

void melo_audio_backend_stop(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return;
    channel_active[channel] = 0;
}

int melo_audio_backend_playing(int channel) {
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) return 0;
    return channel_active[channel];
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
    if (channel < 0 || channel >= MELO_AUDIO_MAX_CHANNELS) {
        vm->error = 1;
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "audio_play: channel out of range (0-%d)", MELO_AUDIO_MAX_CHANNELS - 1);
        return -1;
    }
    int rc = melo_audio_backend_play(channel, (uint32_t)args[1].as.i32,
                                      (uint32_t)args[2].as.i32, args[3].as.i32, args[4].as.i32);
    *out_result = melo_int(rc == 0 ? 1 : 0);
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
        { "audio_init",    native_audio_init },
        { "audio_play",    native_audio_play },
        { "audio_stop",    native_audio_stop },
        { "audio_playing", native_audio_playing },
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
    static const char *names[] = { "audio_init", "audio_play", "audio_stop", "audio_playing" };
    *out_count = (int)(sizeof(names) / sizeof(names[0]));
    return names;
}
