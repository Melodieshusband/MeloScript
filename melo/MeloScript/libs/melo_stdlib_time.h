#ifndef MELO_STDLIB_TIME_H
#define MELO_STDLIB_TIME_H

#include "../melo_vm.h"

int melo_stdlib_register_time(MeloVM *vm);
const char **melo_stdlib_time_native_names(int *out_count);

void melo_time_backend_wait_vsync(void);
float melo_time_backend_frame_delta(void);
uint32_t melo_time_backend_ticks_ms(void);

#endif
