AC_PREREQ([2.71])
AC_INIT([chmc],[1.0.0],[leonhad@gmail.com],[chmc],[https://github.com/leonhad/chmc])
AM_INIT_AUTOMAKE([-Wall tar-ustar no-define nostdinc subdir-objects])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

dnl This mechanism allows one to enable debug compilations...
AC_ARG_ENABLE(debug,
[  --enable-debug     enable debugging and disable optimization],
[  if test "$enable_debug" = "yes"; then
      AX_CHECK_COMPILE_FLAG([-g], [CXXFLAGS="$CXXFLAGS -g"])
      AX_CHECK_COMPILE_FLAG([-fPIC], [CXXFLAGS="$CXXFLAGS -fPIC"])
      AX_CHECK_COMPILE_FLAG([-O0], [CXXFLAGS="$CXXFLAGS -O0"])
      AX_CHECK_COMPILE_FLAG([-DDEBUG], [CXXFLAGS="$CXXFLAGS -DDEBUG"])
      AX_CHECK_COMPILE_FLAG([-fno-inline], [CXXFLAGS="$CXXFLAGS -fno-inline"])
      AX_CHECK_COMPILE_FLAG([-fsanitize=address -fno-omit-frame-pointer], [CXXFLAGS="$CXXFLAGS -fsanitize=address -fno-omit-frame-pointer"])
      AX_CHECK_LINK_FLAG([-static-libasan], [LDFLAGS="$LDFLAGS -static-libasan"])
   else
      AX_CHECK_COMPILE_FLAG([-O3], [CXXFLAGS="$CXXFLAGS -O3"])
      AX_CHECK_COMPILE_FLAG([-DNDEBUG], [CXXFLAGS="$CXXFLAGS -DNDEBUG"])
   fi
])

AC_CANONICAL_HOST

dnl Checks for programs.
AC_LANG([C++])
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS="$CXXFLAGS -Wall"])
AX_CHECK_COMPILE_FLAG([-pipe], [CXXFLAGS="$CXXFLAGS -pipe"])
AX_CHECK_COMPILE_FLAG([-pedantic], [CXXFLAGS="$CXXFLAGS -pedantic"])

dnl clang flags
AX_CHECK_COMPILE_FLAG([-Weverything], [CXXFLAGS="$CXXFLAGS -Weverything"])
AX_CHECK_COMPILE_FLAG([-Wno-padded], [CXXFLAGS="$CXXFLAGS -Wno-padded"])
AX_CHECK_COMPILE_FLAG([-Wno-switch-enum], [CXXFLAGS="$CXXFLAGS -Wno-switch-enum"])

AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_PROG_AR
LT_INIT

AC_CONFIG_FILES([Makefile
                 src/Makefile
])
AC_OUTPUT
