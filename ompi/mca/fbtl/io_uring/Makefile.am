#
# Copyright (c) 2026 RWTH Aachen University. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
# SPDX-License-Identifier: BSD-3-Clause-Open-MPI
#

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

AM_CPPFLAGS = $(fbtl_io_uring_CPPFLAGS)

if MCA_BUILD_ompi_fbtl_io_uring_DSO
component_noinst =
component_install = mca_fbtl_io_uring.la
else
component_noinst = libmca_fbtl_io_uring.la
component_install =
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_fbtl_io_uring_la_SOURCES = $(fbtl_io_uring_sources)
mca_fbtl_io_uring_la_LDFLAGS = -module -avoid-version $(fbtl_io_uring_LDFLAGS)
mca_fbtl_io_uring_la_LIBADD = $(top_builddir)/ompi/libopen_mpi.la \
    $(OMPI_TOP_BUILDDIR)/ompi/mca/common/ompio/libmca_common_ompio.la \
    $(fbtl_io_uring_LIBS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_fbtl_io_uring_la_SOURCES = $(fbtl_io_uring_sources)
libmca_fbtl_io_uring_la_LDFLAGS = -module -avoid-version $(fbtl_io_uring_LDFLAGS)
# Only the external system libraries are added here; the project's own
# libraries (libopen_mpi, common/ompio) are already linked into
# libopen_mpi itself for static builds.
libmca_fbtl_io_uring_la_LIBADD = $(fbtl_io_uring_LIBS)

# Source files

fbtl_io_uring_sources = \
        fbtl_io_uring.h \
        fbtl_io_uring.c \
        fbtl_io_uring_io.c \
        fbtl_io_uring_lock.c \
        fbtl_io_uring_preadv.c \
        fbtl_io_uring_ipreadv.c \
        fbtl_io_uring_pwritev.c \
        fbtl_io_uring_ipwritev.c \
        fbtl_io_uring_progress.c \
        fbtl_io_uring_request_free.c \
        fbtl_io_uring_component.c
