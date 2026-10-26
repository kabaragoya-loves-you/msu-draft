CC ?= gcc
NM ?= nm
CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -Werror -Iinclude
FS_CFLAGS := $(CFLAGS) -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-tree-loop-distribute-patterns
HOST_CFLAGS := $(CFLAGS) -Idemo
BUILD := build

ifeq ($(OS),Windows_NT)
  EXE := .exe
  MKDIR_BUILD := if not exist $(BUILD) mkdir $(BUILD)
  RM_BUILD := if exist $(BUILD) rmdir /s /q $(BUILD)
else
  EXE :=
  MKDIR_BUILD := mkdir -p $(BUILD)
  RM_BUILD := rm -rf $(BUILD)
endif

.PHONY: test demo clean check-symbols

test: $(BUILD)/msu_tests$(EXE) check-symbols
	$(BUILD)/msu_tests$(EXE)

demo: $(BUILD)/msu_demo$(EXE)
	$(BUILD)/msu_demo$(EXE)

check-symbols: $(BUILD)/msu.o
	$(NM) -u $(BUILD)/msu.o > $(BUILD)/msu.undef
ifeq ($(OS),Windows_NT)
	cmd /c "if not exist $(BUILD)\msu.undef exit /b 1 & findstr /R \".\" $(BUILD)\msu.undef >nul & if errorlevel 1 (exit /b 0) else (exit /b 1)"
else
	test -f $(BUILD)/msu.undef
	test ! -s $(BUILD)/msu.undef
endif
	$(CC) -nostdlib -nostartfiles -Wl,-e,msu_parser_init \
	  -o $(BUILD)/msu_check$(EXE) $(BUILD)/msu.o

$(BUILD)/msu.o: src/msu.c include/msu.h
	$(MKDIR_BUILD)
	$(CC) $(FS_CFLAGS) -c src/msu.c -o $(BUILD)/msu.o

$(BUILD)/msu_tests$(EXE): $(BUILD)/msu.o tests/test_msu.c demo/sim.c \
    demo/sim.h include/msu.h
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/msu_tests$(EXE) $(BUILD)/msu.o \
	  tests/test_msu.c demo/sim.c

$(BUILD)/msu_demo$(EXE): $(BUILD)/msu.o demo/main.c demo/sim.c demo/sim.h \
    include/msu.h
	$(CC) $(HOST_CFLAGS) -o $(BUILD)/msu_demo$(EXE) $(BUILD)/msu.o \
	  demo/main.c demo/sim.c

clean:
	$(RM_BUILD)
