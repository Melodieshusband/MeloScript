#ifndef MELO_STDLIB_AUDIO_H
#define MELO_STDLIB_AUDIO_H

#include "../melo_vm.h"

#define MELO_AUDIO_MAX_CHANNELS 24

int melo_stdlib_register_audio(MeloVM *vm);
const char **melo_stdlib_audio_native_names(int *out_count);

int melo_audio_backend_init(void);
int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan);
void melo_audio_backend_stop(int channel);
int melo_audio_backend_playing(int channel);

#endif
