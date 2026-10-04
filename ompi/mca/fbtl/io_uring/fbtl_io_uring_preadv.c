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

#include <liburing.h>

#include "opal/util/output.h"

#include "fbtl_io_uring.h"

/*
 * Blocking read of all entries in file->f_io_array through the ring of
 * the file. Returns the number of bytes read, or OMPI_ERROR on
 * failure. A short total is not an error: it is the end-of-file
 * indication, like read(2), and the caller in common/ompio decides
 * what that means for the MPI read.
 *
 * The engine loop of mca_fbtl_io_uring_run_blocking() interleaves
 * posting and waiting; see fbtl_io_uring_io.c for the details of
 * windowing, short completions and error handling.
 */
ssize_t mca_fbtl_io_uring_preadv(ompio_file_t *file)
{
    struct mca_fbtl_io_uring_request_data_t data;
    struct mca_fbtl_io_uring_file_t *uf =
        (struct mca_fbtl_io_uring_file_t *) file->f_fbtl_data;
    int ret;

    if (NULL == uf) {
        /* fbtl_module_init() creates the ring before the module can
           be used, so this is a bug, not a user error */
        opal_output(1, "mca_fbtl_io_uring_preadv: no io_uring instance for the file\n");
        return OMPI_ERROR;
    }

    ret = mca_fbtl_io_uring_setup(&data, file, MCA_FBTL_IO_URING_READ);
    if (OMPI_SUCCESS != ret) {
        return OMPI_ERROR;
    }

    mca_fbtl_io_uring_run_blocking(&uf->ring, &data);

    mca_fbtl_io_uring_request_cleanup(&data);

    if (data.r_error) {
        opal_output(1, "mca_fbtl_io_uring_preadv: io_uring read failed, errno=%d\n",
                    data.r_error_no);
        return OMPI_ERROR;
    }

    return data.r_total_len;
}
