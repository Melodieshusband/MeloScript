#include <tamtypes.h>
#include <sifrpc.h>
#include <debug.h>
#include "melo_value.h"

extern MeloValue melo_hot_add_ints(const MeloValue *melo_args);

int main(int argc, char *argv[]) {
    sceSifInitRpc(0);
    init_scr();

    MeloValue args[2];
    args[0] = melo_int(19);
    args[1] = melo_int(23);

    MeloValue result = melo_hot_add_ints(args);

    scr_printf("MeloScript @hot result: %d\n", result.as.i32);

    return 0;
}
