#ifndef MELO_STDLIB_PAD_H
#define MELO_STDLIB_PAD_H

#include "../melo_vm.h"

#define MELO_PAD_BTN_SELECT   0x0001
#define MELO_PAD_BTN_L3       0x0002
#define MELO_PAD_BTN_R3       0x0004
#define MELO_PAD_BTN_START    0x0008
#define MELO_PAD_BTN_UP       0x0010
#define MELO_PAD_BTN_RIGHT    0x0020
#define MELO_PAD_BTN_DOWN     0x0040
#define MELO_PAD_BTN_LEFT     0x0080
#define MELO_PAD_BTN_L2       0x0100
#define MELO_PAD_BTN_R2       0x0200
#define MELO_PAD_BTN_L1       0x0400
#define MELO_PAD_BTN_R1       0x0800
#define MELO_PAD_BTN_TRIANGLE 0x1000
#define MELO_PAD_BTN_CIRCLE   0x2000
#define MELO_PAD_BTN_CROSS    0x4000
#define MELO_PAD_BTN_SQUARE   0x8000

int melo_stdlib_register_pad(MeloVM *vm);
const char **melo_stdlib_pad_native_names(int *out_count);

int melo_pad_backend_init(int port, int slot);
void melo_pad_backend_poll(int port, int slot);
uint16_t melo_pad_backend_buttons(int port, int slot);
float melo_pad_backend_stick(int port, int slot, int stick_index, int axis_index);

#endif
