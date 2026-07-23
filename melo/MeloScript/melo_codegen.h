#ifndef MELO_CODEGEN_H
#define MELO_CODEGEN_H

#include "melo_ast.h"
#include "melo_vm.h"
#include "melo_strpool.h"

typedef struct {
    MeloInstr *code;
    int code_count;
    int code_cap;

    MeloValue consts[MELO_MAX_CONSTS];
    int const_count;

    MeloStringPool strpool;

    MeloFuncInfo funcs[MELO_MAX_FUNCS];
    int func_count;
    int main_func_id;

    MeloStructInfo structs[MELO_MAX_STRUCTS];
    int struct_count;

    int had_error;
    char error_msg[256];
} MeloCompiledModule;

int melo_codegen_compile(MeloProgram *prog, MeloCompiledModule *out);
int melo_codegen_compile_with_natives(MeloProgram *prog, MeloCompiledModule *out,
                                       const char **native_names, int native_count);
void melo_codegen_free(MeloCompiledModule *mod);

#endif
