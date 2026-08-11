#ifndef MELO_STDLIB_GS_H
#define MELO_STDLIB_GS_H

#include "melo_vm.h"

int melo_stdlib_register_gs(MeloVM *vm);
const char **melo_stdlib_gs_native_names(int *out_count);

int melo_gs_backend_init(void);
void melo_gs_backend_frame_begin(void);
void melo_gs_backend_frame_end(void);
void melo_gs_backend_draw_sprite(uint32_t vram_offset, uint32_t tex_w, uint32_t tex_h,
                                  float x, float y, float w, float h);

#endif
