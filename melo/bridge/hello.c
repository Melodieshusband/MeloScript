#include <tamtypes.h>
#include <sifrpc.h>
#include <debug.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    sceSifInitRpc(0);
    init_scr();

    scr_printf("MeloScript bridge: PS2SDK OK\n");

    sleep(10);

    return 0;
}
