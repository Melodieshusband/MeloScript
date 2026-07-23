EE_BIN = hello.elf
EE_OBJS = hello.o
EE_LIBS = -lkernel

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

include Makefile.pref
include Makefile.eeglobal
