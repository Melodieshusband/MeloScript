#include "melo_parser.h"
#include "melo_aot.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    const char *src =
        "vukernel add_offset(v: vec4) -> vec4 {\n"
        "    var one = vec4(1.0, 1.0, 1.0, 1.0);\n"
        "    return v + one;\n"
        "}\n"
        "\n"
        "func main() -> vec4 {\n"
        "    return add_offset(vec4(1.0, 2.0, 3.0, 4.0));\n"
        "}\n";

    MeloParser parser;
    melo_parser_init(&parser, src);
    MeloProgram *prog = melo_parse_program(&parser);
    assert(!parser.had_error);

    MeloAotResult result;
    int rc = melo_aot_translate_program(prog, &result);
    if (rc) {
        printf("AOT ERROR: %s\n", result.error_msg);
        return 1;
    }

    assert(result.unit_count == 1);
    assert(strcmp(result.units[0].name, "add_offset") == 0);
    assert(strstr(result.units[0].c_source, "melo_vu1_add_offset_microcode") != NULL);
    assert(strstr(result.units[0].c_source, "vadd.xyzw") != NULL);
    assert(strstr(result.units[0].c_source, "xgkick") != NULL);
    assert(strstr(result.units[0].c_source, "eret") != NULL);

    printf("=== generated VU1 unit for '%s' ===\n", result.units[0].name);
    printf("%s\n", result.units[0].c_source);

    melo_aot_free(&result);
    printf("VU1 AOT TEST PASSED\n");
    return 0;
}
