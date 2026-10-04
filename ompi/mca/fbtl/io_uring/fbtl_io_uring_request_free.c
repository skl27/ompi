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
 * Free the io_uring specific part of an asynchronous request, called
 * through req_free_fn when the ompio layer releases the request.
 *
 * Normally the request completed before this point and nothing is in
 * flight anymore. The exceptional case - a request freed while its
 * operations are still on the ring, e.g. after the upper layer gave
 * up on it - must not simply let the subrequest array go away:
 * completion queue entries carry raw pointers into it. Draining the
 * request's own in-flight operations synchronously is therefore the
 * only safe way out; io_uring, unlike POSIX AIO, cannot cancel an
 * operation that has already reached the file description without
 * racing with its completion.
 *
 * Afterwards req_data is reset: the mca_ompio_request_t destructor
 * releases whatever is still hanging on it, and double freeing the
 * data would be fatal.
 */
void mca_fbtl_io_uring_request_free(mca_ompio_request_t *req)
{
    struct mca_fbtl_io_uring_request_data_t *data =
        (struct mca_fbtl_io_uring_request_data_t *) req->req_data;
    struct mca_fbtl_io_uring_file_t *uf;

    if (NULL == data) {
        return;
    }

    uf = (struct mca_fbtl_io_uring_file_t *) data->r_fh->f_fbtl_data;
    if (NULL != uf) {
        mca_fbtl_io_uring_drain_ring(uf, data);
    } else if (0 != data->r_inflight) {
        /* the file is already closed; io_uring_queue_exit() of the
           module finalize went through the ring, so these completions
           are gone for good - nothing left to drain */
        data->r_inflight = 0;
    }

    mca_fbtl_io_uring_request_cleanup(data);
    free(data);
    req->req_data = NULL;
}
