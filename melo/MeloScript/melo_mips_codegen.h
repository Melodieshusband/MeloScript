#ifndef MELO_MIPS_CODEGEN_H
#define MELO_MIPS_CODEGEN_H

#include "melo_codegen.h"
#include <stdio.h>
#include <stddef.h>

int melo_mips_emit_assembly(const MeloCompiledModule *mod,
                           const char **native_names,
                           int native_count,
                           FILE *out,
                           char *err,
                           size_t err_cap);

#endif
