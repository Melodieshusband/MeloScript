#ifndef MELO_STDLIB_VRAM_H
#define MELO_STDLIB_VRAM_H

#include "melo_vm.h"
#include <stdio.h>

int melo_stdlib_register_vram(MeloVM *vm);
const char **melo_stdlib_vram_native_names(int *out_count);

#endif
