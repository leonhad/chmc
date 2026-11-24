bin_PROGRAMS = chmc

chmc_SOURCES = \
	lzx.cpp \
    chm.cpp \
    main.cpp

noinst_HEADERS = \
	chm.h \
    lzx.h \
    objinst_table.h

MSPACK_DIR = $(top_srcdir)/libmspack
MSPACK_INC = -I$(MSPACK_DIR)

chmc_CXXFLAGS = -I.. -I. $(MSPACK_INC)

# ---------------------------------------------------------------------------
# libmspack helper library (static, for tests)
# ---------------------------------------------------------------------------

noinst_LIBRARIES = libmspack_helper.a

libmspack_helper_a_SOURCES = \
	$(MSPACK_DIR)/chmd.c \
	$(MSPACK_DIR)/lzxd.c \
	$(MSPACK_DIR)/system.c \
	$(MSPACK_DIR)/crc32.c

libmspack_helper_a_CPPFLAGS = $(MSPACK_INC) -I$(top_builddir)

# ---------------------------------------------------------------------------
# Test binaries (from Makefile.win)
# ---------------------------------------------------------------------------

check_PROGRAMS = test_lzx test_chm

# test_lzx: validates LZX compression against libmspack decoder
test_lzx_SOURCES = test_lzx.cpp lzx.cpp
test_lzx_CXXFLAGS = $(MSPACK_INC) -I.
test_lzx_LDADD = libmspack_helper.a

# test_chm: validates CHM extraction against libmspack decoder
test_chm_SOURCES = test_chm.cpp
test_chm_CXXFLAGS = $(MSPACK_INC) -I.
test_chm_LDADD = libmspack_helper.a

# Test execution
# Bypass automake's test-driver wrapper, which fails on Windows/MinGW shells
# (e.g. "can't open ../test-driver: Invalid argument" with busybox sh)
LOG_COMPILER = true
TESTS = test_lzx test_chm

