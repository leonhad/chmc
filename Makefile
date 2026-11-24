# Makefile for the CHM builder in C++17.
#
# Main targets:
#   make            builds chmc.exe
#   make run        generates example/project.chm
#   make clean      removes generated objects and executables
#   make test       validates with libmspack (included in the project)

.DEFAULT_GOAL := all

CXX      ?= c++
CC       ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CFLAGS   ?= -O2
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=
DEPFLAGS  = -MMD -MP

TARGET := chmc.exe
SRC    := chmc/main.cpp chmc/chm.cpp chmc/lzx.cpp
OBJ    := $(SRC:.cpp=.o)
DEPS   := $(OBJ:.o=.d)

.PHONY: all run clean rebuild help test test-lzx test-chm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPS)

run: $(TARGET)
	./$(TARGET) example/project.hhp

rebuild: clean all

clean:
	$(RM) $(OBJ) $(DEPS) $(TARGET)
	$(RM) lzxd.o chmd.o mspack_system.o crc32.o
	$(RM) test_lzx.exe test_chm.exe
	$(RM) lzx_test.lzx lzx_test.out

help:
	@echo "Available targets:"
	@echo "  all        builds $(TARGET) (default)"
	@echo "  run        generates exemplo/projeto.chm"
	@echo "  clean      removes generated objects and executables"
	@echo "  rebuild    runs clean and all"
	@echo "  test       runs test-lzx and test-chm"
	@echo "  test-lzx   validates LZX with lzxd.c from libmspack"
	@echo "  test-chm   validates CHM with chmd.c from libmspack"

# ---------------------------------------------------------------------------
# Tests with libmspack (Git submodule)
# ---------------------------------------------------------------------------
MSPACK_DIR := libmspack/libmspack/mspack
MSPACK_INC := -I$(MSPACK_DIR)
TEST_LZX   := test_lzx.exe
TEST_CHM   := test_chm.exe

test: test-lzx test-chm

test-lzx: $(TEST_LZX)
	./$(TEST_LZX) lzx_test

test-chm: $(TARGET) $(TEST_CHM)
	./$(TARGET) example/project.hhp example/project.chm
	./$(TEST_CHM) example/project.chm example

$(TEST_LZX): chmc chmc lzxd.o mspack_system.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MSPACK_INC) -o $@ \
		chmc/test_lzx.cpp chmc/lzx.o lzxd.o mspack_system.o $(LDLIBS)

$(TEST_CHM): chmc chmd.o lzxd.o mspack_system.o crc32.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MSPACK_INC) -o $@ \
		chmc/test_chm.cpp chmd.o lzxd.o mspack_system.o crc32.o $(LDLIBS)

lzxd.o: $(MSPACK_DIR)/lzxd.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

chmd.o: $(MSPACK_DIR)/chmd.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

mspack_system.o: $(MSPACK_DIR)/system.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

crc32.o: $(MSPACK_DIR)/crc32.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@
