MELO_SRC = ../MeloScript
MELO_FILE = hello.melo
GEN_C = hello_gen.c

EE_BIN = hello.elf
EE_OBJS = melo_main.o $(GEN_C:.c=.o)
EE_LIBS = -ldebug -lkernel

all: $(EE_BIN)

melo2c: melo2c.c
	gcc -O2 -I$(MELO_SRC) -o melo2c melo2c.c \
		$(MELO_SRC)/melo_parser.c $(MELO_SRC)/melo_lexer.c \
		$(MELO_SRC)/melo_codegen.c $(MELO_SRC)/melo_aot.c \
		$(MELO_SRC)/melo_strpool.c $(MELO_SRC)/melo_vu1.c \
		$(MELO_SRC)/melo_stdlib_vram.c $(MELO_SRC)/melo_vm.c -lm

$(GEN_C): melo2c $(MELO_FILE)
	./melo2c $(MELO_FILE) $(GEN_C)

EE_CFLAGS += -I$(MELO_SRC)

$(EE_BIN): melo2c

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(GEN_C) melo2c

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

include Makefile.pref
include Makefile.eeglobal
