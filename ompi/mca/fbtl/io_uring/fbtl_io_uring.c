/*
 * Copyright (c) 2026 RWTH Aachen University. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 * SPDX-License-Identifier: BSD-3-Clause-Open-MPI
 */

#include "ompi_config.h"
#include "fbtl_io_uring.h"

int mca_fbtl_io_uring_component_init_query(bool enable_progress_threads,
                                           bool enable_mpi_threads)
{
    /* Nothing to do yet */
    return OMPI_SUCCESS;
}

struct mca_fbtl_base_module_1_0_0_t *
mca_fbtl_io_uring_component_file_query(ompio_file_t *file, int *priority)
{
    /* Stub: the io_uring backend is not implemented yet, so never
       select this component. */
    return NULL;
}

int mca_fbtl_io_uring_component_file_unquery(ompio_file_t *file)
{
    /* file_query() is a no-op, so there is nothing to undo */
    return OMPI_SUCCESS;
}
