# -*- shell-script -*-
#
# Copyright (c) 2026 RWTH Aachen University. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI
#

# MCA_fbtl_io_uring_CONFIG(action-if-can-compile,
#                         [action-if-cant-compile])
# ------------------------------------------------
AC_DEFUN([MCA_ompi_fbtl_io_uring_CONFIG],[
    AC_CONFIG_FILES([ompi/mca/fbtl/io_uring/Makefile])

    fbtl_io_uring_happy=no
    OMPI_CHECK_LIBURING([fbtl_io_uring],
                        [fbtl_io_uring_happy="yes"],
                        [fbtl_io_uring_happy="no"])

    AS_IF([test "$fbtl_io_uring_happy" = "yes"],
          [$1],
          [$2])

    # substitute in the things needed to build the io_uring component
    # (OMPI_CHECK_LIBURING/OAC_CHECK_PACKAGE set them under the
    # fbtl_io_uring_ prefix)
    AC_SUBST([fbtl_io_uring_CPPFLAGS])
    AC_SUBST([fbtl_io_uring_LDFLAGS])
    AC_SUBST([fbtl_io_uring_LIBS])
])dnl
