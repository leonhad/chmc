bin_PROGRAMS = chmc

chmc_SOURCES = \
	lzx.cpp \
    chm.cpp \
    main.cpp

noinst_HEADERS = \
	chm.h \
    lzx.h \
    objinst_table.h

chmc_CXXFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src

# ---------------------------------------------------------------------------
# Tests: built only when configure finds libmspack
# ---------------------------------------------------------------------------

if HAVE_MSPACK
check_PROGRAMS = test_chm

# test_chm validates CHM extraction (and therefore the LZX stream) with
# libmspack's public CHM decompressor API.
test_chm_SOURCES = test_chm.cpp
test_chm_CXXFLAGS = $(MSPACK_CFLAGS) -I$(top_srcdir)/src
test_chm_LDADD = $(MSPACK_LIBS)

# Bypass automake's test-driver wrapper, which fails on Windows/MinGW shells
# (e.g. "can't open ../test-driver: Invalid argument" with busybox sh)
LOG_COMPILER = true
TESTS = test_chm
endif

# Kept for reference only: the system libmspack does not export the internal
# lzxd_* API required by this low-level test.
EXTRA_DIST = test_lzx.cpp

