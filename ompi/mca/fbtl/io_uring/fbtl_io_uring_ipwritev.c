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

#include <stdlib.h>
#include <liburing.h>

#include "opal/util/output.h"

#include "fbtl_io_uring.h"

/*
 * Non-blocking write of all entries in file->f_io_array; the exact
 * counterpart of mca_fbtl_io_uring_ipreadv() for the write direction,
 * including the rule that an error may only be reported
 * synchronously as long as no operation reached the ring.
 */
ssize_t mca_fbtl_io_uring_ipwritev(ompio_file_t *file, ompi_request_t *request)
{
    mca_ompio_request_t *req = (mca_ompio_request_t *) request;
    struct mca_fbtl_io_uring_request_data_t *data;
    struct mca_fbtl_io_uring_file_t *uf =
        (struct mca_fbtl_io_uring_file_t *) file->f_fbtl_data;

    if (NULL == uf) {
        /* fbtl_module_init() creates the ring before the module can
           be used, so this is a bug, not a user error */
        opal_output(1, "mca_fbtl_io_uring_ipwritev: no io_uring instance for the file\n");
        return OMPI_ERROR;
    }

    data = (struct mca_fbtl_io_uring_request_data_t *)
        calloc(1, sizeof(struct mca_fbtl_io_uring_request_data_t));
    if (NULL == data) {
        opal_output(1, "mca_fbtl_io_uring_ipwritev: could not allocate memory\n");
        return OMPI_ERROR;
    }

    if (OMPI_SUCCESS != mca_fbtl_io_uring_setup(data, file, MCA_FBTL_IO_URING_WRITE)) {
        free(data);
        return OMPI_ERROR;
    }

    /* post the first window of subrequests right away */
    (void) mca_fbtl_io_uring_fill_ring(uf, data);

    if (data->r_error && 0 == data->r_inflight) {
        /* nothing reached the ring: fail the request synchronously,
           the caller reports the failure of the issuing operation */
        opal_output(1, "mca_fbtl_io_uring_ipwritev: io_uring write submission failed, errno=%d\n",
                    data->r_error_no);
        mca_fbtl_io_uring_request_cleanup(data);
        free(data);
        return OMPI_ERROR;
    }

    req->req_data = (void *) data;
    req->req_progress_fn = mca_fbtl_io_uring_progress;
    req->req_free_fn = mca_fbtl_io_uring_request_free;

    return OMPI_SUCCESS;
}
