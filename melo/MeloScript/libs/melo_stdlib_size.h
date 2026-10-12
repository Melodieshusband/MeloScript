#ifndef MELO_STDLIB_SIZE_H
#define MELO_STDLIB_SIZE_H

#include "../melo_vm.h"

int melo_stdlib_register_size(MeloVM *vm);
const char **melo_stdlib_size_native_names(int *out_count);

uint32_t melo_size_value_bytes(const MeloValue *v);
uint32_t melo_size_script_bytes(const MeloVM *vm);
uint32_t melo_size_script_hash(const MeloVM *vm);

#endif
