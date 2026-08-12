#ifndef MELO_STDLIB_AUDIO_H
#define MELO_STDLIB_AUDIO_H

#include "../melo_vm.h"

#define MELO_AUDIO_MAX_CHANNELS 24
#define MELO_AUDIO_VOL_MIN 0
#define MELO_AUDIO_VOL_MAX 16383

int melo_stdlib_register_audio(MeloVM *vm);
const char **melo_stdlib_audio_native_names(int *out_count);

int melo_audio_backend_init(void);
int melo_audio_backend_play(int channel, uint32_t sample_addr, uint32_t sample_size, int volume, int pan, int loop);
void melo_audio_backend_stop(int channel);
int melo_audio_backend_playing(int channel);
int melo_audio_backend_paused(int channel);
void melo_audio_backend_pause(int channel);
void melo_audio_backend_resume(int channel);
void melo_audio_backend_set_volume(int channel, int volume);
void melo_audio_backend_set_pan(int channel, int pan);
void melo_audio_backend_set_master_volume(int volume);

/* Feeds one more chunk of decoded sample data into a channel that was
   opened in streaming mode. Intended for long-form music where the full
   track cannot fit in a single 32MB-RAM-resident buffer; the caller
   (host game loop / IOP-side RPC) is responsible for producing chunks in
   time, this only forwards the next chunk into the channel's transfer.
   Returns 0 on success, -1 if the channel has no active stream. */
int melo_audio_backend_stream_feed(int channel, uint32_t chunk_addr, uint32_t chunk_size);

#endif
