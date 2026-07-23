#ifndef MELO_AOT_H
#define MELO_AOT_H

#include "melo_ast.h"
#include "melo_value.h"
#include <stddef.h>

#define MELO_AOT_MAX_UNITS 128

typedef struct {
    char *name;
    char *c_source;
    char *c_symbol;
} MeloAotUnit;

typedef struct {
    MeloAotUnit units[MELO_AOT_MAX_UNITS];
    int unit_count;
    int had_error;
    char error_msg[256];
} MeloAotResult;

int melo_aot_translate_program(MeloProgram *prog, MeloAotResult *out);
void melo_aot_free(MeloAotResult *out);

int melo_aot_write_unit_file(const MeloAotUnit *unit, const char *path);
int melo_aot_write_bundle_file(const MeloAotResult *result, const char *path,
                                const char *header_includes);

#ifndef MELO_AOT_FN_DEFINED
#define MELO_AOT_FN_DEFINED
typedef MeloValue (*MeloAotFn)(const MeloValue *args);
#endif

#endif
