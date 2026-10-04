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

#include <errno.h>
#include <string.h>

#include <liburing.h>

#include "opal/util/output.h"

#include "fbtl_io_uring.h"

/*
 * Blocking write of all entries in file->f_io_array through the ring
 * of the file. Returns the number of bytes written, or OMPI_ERROR on
 * failure.
 *
 * See mca_fbtl_io_uring_preadv() for the description of the posting
 * and waiting model; the two paths are identical apart from the
 * direction of the operation. A zero-byte completion of a write can
 * legally only happen for a request that asked for nothing (handled
 * without touching the ring), so writes that complete short of the
 * requested total make the request return a short byte count for the
 * caller in common/ompio to report as an error.
 */
ssize_t mca_fbtl_io_uring_pwritev(ompio_file_t *file)
{
    struct mca_fbtl_io_uring_request_data_t data;
    struct mca_fbtl_io_uring_file_t *uf =
        (struct mca_fbtl_io_uring_file_t *) file->f_fbtl_data;
    int ret;

    if (NULL == uf) {
        /* fbtl_module_init() creates the ring before the module can
           be used, so this is a bug, not a user error */
        opal_output(1, "mca_fbtl_io_uring_pwritev: no io_uring instance for the file\n");
        return OMPI_ERROR;
    }

    ret = mca_fbtl_io_uring_setup(&data, file, MCA_FBTL_IO_URING_WRITE);
    if (OMPI_SUCCESS != ret) {
        return OMPI_ERROR;
    }

    /* An enforced MPI atomicity needs exclusive possession of the
       whole region for the duration of the operation; the engine
       skips its per-subrequest region locks in that case. */
    if (0 != mca_fbtl_io_uring_set_atomicity_lock(&data, F_WRLCK)) {
        opal_output(1, "mca_fbtl_io_uring_pwritev: error in atomicity lock: %s",
                    strerror(errno));
        mca_fbtl_io_uring_request_cleanup(&data);
        return OMPI_ERROR;
    }

    mca_fbtl_io_uring_run_blocking(uf, &data);

    mca_fbtl_io_uring_request_cleanup(&data);

    if (data.r_error) {
        opal_output(1, "mca_fbtl_io_uring_pwritev: io_uring write failed, errno=%d\n",
                    data.r_error_no);
        return OMPI_ERROR;
    }

    return data.r_total_len;
}
