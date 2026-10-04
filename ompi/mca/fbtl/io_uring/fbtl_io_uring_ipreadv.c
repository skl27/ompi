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
 * Non-blocking read of all entries in file->f_io_array.
 *
 * The caller (common/ompio) has already allocated the request; this
 * function builds the io_uring specific state on it and posts as much
 * of the work as the in-flight windows allow before returning, so a
 * small read is usually fully submitted right away. Whatever is still
 * pending or in flight when the caller looks at the request is driven
 * by mca_fbtl_io_uring_progress(), which the ompio progress engine
 * invokes for every outstanding request.
 *
 * Returning an error synchronously is only allowed while nothing has
 * been submitted: the caller then completes the request itself with
 * MPI_ERR_IO, and an operation still on the ring would dangle on
 * memory that is freed with the request. Submission problems that hit
 * after some operations reached the ring are therefore reported
 * asynchronously through the request status, like an AIO error would
 * be.
 */
ssize_t mca_fbtl_io_uring_ipreadv(ompio_file_t *file, ompi_request_t *request)
{
    mca_ompio_request_t *req = (mca_ompio_request_t *) request;
    struct mca_fbtl_io_uring_request_data_t *data;
    struct mca_fbtl_io_uring_file_t *uf =
        (struct mca_fbtl_io_uring_file_t *) file->f_fbtl_data;

    if (NULL == uf) {
        /* fbtl_module_init() creates the ring before the module can
           be used, so this is a bug, not a user error */
        opal_output(1, "mca_fbtl_io_uring_ipreadv: no io_uring instance for the file\n");
        return OMPI_ERROR;
    }

    data = (struct mca_fbtl_io_uring_request_data_t *)
        calloc(1, sizeof(struct mca_fbtl_io_uring_request_data_t));
    if (NULL == data) {
        opal_output(1, "mca_fbtl_io_uring_ipreadv: could not allocate memory\n");
        return OMPI_ERROR;
    }

    if (OMPI_SUCCESS != mca_fbtl_io_uring_setup(data, file, MCA_FBTL_IO_URING_READ)) {
        free(data);
        return OMPI_ERROR;
    }

    /* post the first window of subrequests right away */
    (void) mca_fbtl_io_uring_fill_ring(uf, data);

    if (data->r_error && 0 == data->r_inflight) {
        /* nothing reached the ring: fail the request synchronously,
           the caller reports MPI_ERR_IO for the issuing operation */
        opal_output(1, "mca_fbtl_io_uring_ipreadv: io_uring read submission failed, errno=%d\n",
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
