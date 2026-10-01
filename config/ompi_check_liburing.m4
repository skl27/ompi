dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2026      RWTH Aachen University. All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl SPDX-License-Identifier: BSD-3-Clause-Open-MPI
dnl

# OMPI_CHECK_LIBURING(prefix, [action-if-found], [action-if-not-found])
# --------------------------------------------------------
# check if liburing (io_uring) support can be found.  sets prefix_{CPPFLAGS,
# LDFLAGS, LIBS} as needed and runs action-if-found if there is
# support, otherwise executes action-if-not-found
AC_DEFUN([OMPI_CHECK_LIBURING],[
    OPAL_VAR_SCOPE_PUSH([ompi_check_liburing_happy])

    # Get some configuration information
    AC_ARG_WITH([liburing],
        [AS_HELP_STRING([--with-liburing(=DIR)],
             [Build liburing (io_uring) support, optionally adding DIR/include, DIR/lib, and DIR/lib64 to the search path for headers and libraries])])

    OAC_CHECK_PACKAGE([liburing],
                      [$1],
                      [liburing.h],
                      [uring],
                      [io_uring_queue_init],
                      [ompi_check_liburing_happy="yes"],
                      [ompi_check_liburing_happy="no"])

    AS_IF([test "$ompi_check_liburing_happy" = "yes"],
          [$2],
          [AS_IF([test ! -z "$with_liburing" && test "$with_liburing" != "no"],
                 [AC_MSG_ERROR([liburing support requested but not found.  Aborting])])
            $3])

    OPAL_VAR_SCOPE_POP
])
