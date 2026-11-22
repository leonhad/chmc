# Makefile para o gerador de CHM em C++17.
#
# Alvos principais:
#   make            compila chm_builder.exe
#   make run        gera exemplo/projeto.chm
#   make clean      remove objetos e executáveis gerados
#   make test       valida com libmspack (exige LIBMSPACK=/caminho/para/libmspack)

.DEFAULT_GOAL := all

CXX      ?= c++
CC       ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CFLAGS   ?= -O2
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=
DEPFLAGS  = -MMD -MP

TARGET := chm_builder.exe
SRC    := chm_builder/main.cpp chm_builder/chm.cpp chm_builder/lzx.cpp
OBJ    := $(SRC:.cpp=.o)
DEPS   := $(OBJ:.o=.d)

.PHONY: all run clean rebuild help test test-lzx test-chm require-libmspack

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPS)

run: $(TARGET)
	./$(TARGET) exemplo/projeto.hhp

rebuild: clean all

clean:
	$(RM) $(OBJ) $(DEPS) $(TARGET)
	$(RM) lzxd.o chmd.o mspack_system.o
	$(RM) test_lzx.exe test_chm.exe
	$(RM) lzx_teste.lzx lzx_teste.out

help:
	@echo "Alvos disponíveis:"
	@echo "  all        compila $(TARGET) (padrão)"
	@echo "  run        gera exemplo/projeto.chm"
	@echo "  clean      remove objetos e executáveis gerados"
	@echo "  rebuild    executa clean e all"
	@echo "  test       executa test-lzx e test-chm"
	@echo "  test-lzx   valida o LZX com lzxd.c do libmspack"
	@echo "  test-chm   valida o CHM com chmd.c do libmspack"
	@echo ""
	@echo "Para usar os testes:"
	@echo "  make test LIBMSPACK=/caminho/para/libmspack"

# ---------------------------------------------------------------------------
# Testes opcionais com libmspack
# ---------------------------------------------------------------------------
ifeq ($(strip $(LIBMSPACK)),)

test test-lzx test-chm:
	@echo "Defina LIBMSPACK=/caminho/para/libmspack para usar os testes."
	@exit 1

else

MSPACK_INC := -I$(LIBMSPACK)
TEST_LZX   := test_lzx.exe
TEST_CHM   := test_chm.exe

test: test-lzx test-chm

test-lzx: $(TEST_LZX)
	./$(TEST_LZX) lzx_teste

test-chm: $(TARGET) $(TEST_CHM)
	./$(TARGET) exemplo/projeto.hhp exemplo/projeto.chm
	./$(TEST_CHM) exemplo/projeto.chm exemplo

$(TEST_LZX): chm_builder/test_lzx.cpp chm_builder/lzx.o lzxd.o mspack_system.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MSPACK_INC) -o $@ \
		chm_builder/test_lzx.cpp chm_builder/lzx.o lzxd.o mspack_system.o $(LDLIBS)

$(TEST_CHM): chm_builder/test_chm.cpp chmd.o lzxd.o mspack_system.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(MSPACK_INC) -o $@ \
		chm_builder/test_chm.cpp chmd.o lzxd.o mspack_system.o $(LDLIBS)

lzxd.o: $(LIBMSPACK)/lzxd.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

chmd.o: $(LIBMSPACK)/chmd.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

mspack_system.o: $(LIBMSPACK)/system.c
	$(CC) $(CFLAGS) $(MSPACK_INC) -c $< -o $@

endif
