#include "melo_parser.h"
#include "melo_vu1.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static MeloFuncDecl *find_vukernel(MeloProgram *prog) {
    for (int i = 0; i < prog->count; i++) {
        if (prog->decls[i].kind == DECL_FUNC && prog->decls[i].as.func.is_vukernel) {
            return &prog->decls[i].as.func;
        }
    }
    return NULL;
}

static void test_vec4_constant_is_loaded_not_moved_into_itself(void) {
    const char *src =
        "vukernel add_offset(v: vec4) -> vec4 {\n"
        "    var one = vec4(1.0, 2.0, 3.0, 4.0);\n"
        "    return v + one;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloFuncDecl *fn = find_vukernel(prog);
    assert(fn != NULL);

    MeloVu1Program vu;
    int rc = melo_vu1_compile(fn, &vu);
    assert(rc == 0);
    assert(vu.const_count == 1);
    assert(vu.const_pool[0][0] == 1.0f);
    assert(vu.const_pool[0][1] == 2.0f);
    assert(vu.const_pool[0][2] == 3.0f);
    assert(vu.const_pool[0][3] == 4.0f);

    int found_lq_for_const = 0;
    int expected_addr = vu.param_count + 0;
    for (int i = 0; i < vu.instr_count; i++) {
        if (vu.instrs[i].lower_op == VU_LOWER_LQ && vu.instrs[i].imm == expected_addr) {
            found_lq_for_const = 1;
        }
    }
    assert(found_lq_for_const);

    int found_self_move = 0;
    for (int i = 0; i < vu.instr_count; i++) {
        if (vu.instrs[i].upper_op == VU_UPPER_MOVE && vu.instrs[i].fd == vu.instrs[i].fs) {
            found_self_move = 1;
        }
    }
    assert(!found_self_move);

    printf("[vec4_constant_is_loaded_not_moved_into_itself] OK (const=(%.1f,%.1f,%.1f,%.1f), loaded via lq at addr %d)\n",
           vu.const_pool[0][0], vu.const_pool[0][1], vu.const_pool[0][2], vu.const_pool[0][3], expected_addr);
}

static void test_result_address_does_not_collide_with_params(void) {
    const char *src =
        "vukernel identity(v: vec4) -> vec4 {\n"
        "    return v;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloFuncDecl *fn = find_vukernel(prog);
    assert(fn != NULL);

    MeloVu1Program vu;
    int rc = melo_vu1_compile(fn, &vu);
    assert(rc == 0);
    assert(vu.result_addr != 0);
    assert(vu.result_addr >= vu.param_count + vu.const_count);

    printf("[result_address_does_not_collide_with_params] OK (result_addr=%d, param_count=%d)\n",
           vu.result_addr, vu.param_count);
}

static void test_no_xgkick_emitted_for_value_returning_kernel(void) {
    const char *src =
        "vukernel scale_test(v: vec4) -> vec4 {\n"
        "    return v;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloFuncDecl *fn = find_vukernel(prog);
    assert(fn != NULL);

    MeloVu1Program vu;
    int rc = melo_vu1_compile(fn, &vu);
    assert(rc == 0);

    int found_xgkick = 0;
    int found_eret = 0;
    for (int i = 0; i < vu.instr_count; i++) {
        if (vu.instrs[i].lower_op == VU_LOWER_XGKICK) found_xgkick = 1;
        if (vu.instrs[i].lower_op == VU_LOWER_ERET) found_eret = 1;
    }
    assert(!found_xgkick);
    assert(found_eret);

    printf("[no_xgkick_emitted_for_value_returning_kernel] OK (eret present, xgkick absent)\n");
}

static void test_field_access_generates_masked_move(void) {
    const char *src =
        "vukernel get_x(v: vec4) -> vec4 {\n"
        "    var x = v.x;\n"
        "    return x;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloFuncDecl *fn = find_vukernel(prog);
    assert(fn != NULL);

    MeloVu1Program vu;
    int rc = melo_vu1_compile(fn, &vu);
    assert(rc == 0);

    int found_masked_move = 0;
    for (int i = 0; i < vu.instr_count; i++) {
        if (vu.instrs[i].upper_op == VU_UPPER_MOVE && vu.instrs[i].dest_mask == VU_DEST_X) {
            found_masked_move = 1;
        }
    }
    assert(found_masked_move);
    printf("[field_access_generates_masked_move] OK (.x lowered to vmove.x)\n");
}

static void test_field_access_invalid_component_is_error(void) {
    const char *src =
        "vukernel bad_field(v: vec4) -> vec4 {\n"
        "    var q = v.q;\n"
        "    return q;\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloFuncDecl *fn = find_vukernel(prog);
    assert(fn != NULL);

    MeloVu1Program vu;
    int rc = melo_vu1_compile(fn, &vu);
    assert(rc != 0);
    printf("[field_access_invalid_component_is_error] OK (%s)\n", vu.error_msg);
}

int main(void) {
    test_vec4_constant_is_loaded_not_moved_into_itself();
    test_result_address_does_not_collide_with_params();
    test_no_xgkick_emitted_for_value_returning_kernel();
    test_field_access_generates_masked_move();
    test_field_access_invalid_component_is_error();
    printf("ALL VU1 CODEGEN FIX TESTS PASSED\n");
    return 0;
}
