/*
 * Copyright (c) 2026 RWTH Aachen University. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 * SPDX-License-Identifier: BSD-3-Clause-Open-MPI
 */

#ifndef MCA_FBTL_IO_URING_H
#define MCA_FBTL_IO_URING_H

#include "ompi_config.h"
#include "ompi/mca/mca.h"
#include "ompi/mca/fbtl/fbtl.h"
#include "ompi/mca/common/ompio/common_ompio.h"

extern int mca_fbtl_io_uring_priority;

BEGIN_C_DECLS

int mca_fbtl_io_uring_component_init_query(bool enable_progress_threads,
                                           bool enable_mpi_threads);
struct mca_fbtl_base_module_1_0_0_t *
mca_fbtl_io_uring_component_file_query(ompio_file_t *file, int *priority);
int mca_fbtl_io_uring_component_file_unquery(ompio_file_t *file);

OMPI_DECLSPEC extern mca_fbtl_base_component_2_0_0_t mca_fbtl_io_uring_component;

END_C_DECLS

#endif /* MCA_FBTL_IO_URING_H */
