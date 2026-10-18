MELO_SRC = ../MeloScript
MELO_FILE = tetris.melo
BIN = tetris.elf

all: $(BIN)

meloc: meloc.c $(MELO_SRC)/melo_mips_codegen.c
	gcc -O2 -I$(MELO_SRC) -o meloc meloc.c \
		$(MELO_SRC)/melo_mips_codegen.c \
		$(MELO_SRC)/melo_parser.c $(MELO_SRC)/melo_lexer.c \
		$(MELO_SRC)/melo_codegen.c $(MELO_SRC)/melo_aot.c \
		$(MELO_SRC)/melo_strpool.c $(MELO_SRC)/melo_vu1.c \
		$(MELO_SRC)/melo_stdlib_vram.c $(MELO_SRC)/melo_vm.c -lm

melo2c: melo2c.c
	gcc -O2 -I$(MELO_SRC) -o melo2c melo2c.c \
		$(MELO_SRC)/melo_parser.c $(MELO_SRC)/melo_lexer.c \
		$(MELO_SRC)/melo_codegen.c $(MELO_SRC)/melo_aot.c \
		$(MELO_SRC)/melo_strpool.c $(MELO_SRC)/melo_vu1.c \
		$(MELO_SRC)/melo_stdlib_vram.c $(MELO_SRC)/melo_vm.c -lm

$(BIN): meloc $(MELO_FILE) melo_ps2_runtime.c
	./meloc $(MELO_FILE) -o $(BIN)

legacy: meloc $(MELO_FILE) melo_main.c
	./meloc $(MELO_FILE) -o tetris_legacy.elf --backend=c

clean:
	rm -f $(BIN) tetris_direct.elf tetris_c.elf tetris_legacy.elf tetris.s hello.s hello.o tetris.o meloc melo2c *.tmp.s *_gen.c *.o
