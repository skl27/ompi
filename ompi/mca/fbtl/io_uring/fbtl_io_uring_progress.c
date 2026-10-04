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
 * Per-request progress function of an asynchronous I/O request,
 * called repeatedly by the ompio progress engine until it returns
 * true; the caller then marks the MPI request as completed and
 * publishes the status set here.
 *
 * The function must never block and must never re-enter the ompio
 * progress engine (the same constraint the posix component documents
 * for its AIO progress): everything it does is non-blocking by
 * construction - harvest whatever completion is already on the file's
 * ring, post another window of this request's pending subrequests,
 * harvest again, and check whether the request has run dry.
 *
 * Harvesting takes completions of *all* requests sharing this file's
 * ring, not just this one's; apply_cqe() routes them to their owners,
 * whose own progress calls will find their state already advanced.
 */
bool mca_fbtl_io_uring_progress(mca_ompio_request_t *req)
{
    struct mca_fbtl_io_uring_request_data_t *data =
        (struct mca_fbtl_io_uring_request_data_t *) req->req_data;
    struct mca_fbtl_io_uring_file_t *uf;

    if (NULL == data) {
        /* ipreadv()/ipwritev() set the progress function together
           with the data, so this is a bug, not a user error */
        opal_output(1, "mca_fbtl_io_uring_progress: request without data\n");
        req->req_ompi.req_status.MPI_ERROR = OMPI_ERROR;
        req->req_ompi.req_status._ucount = 0;
        return true;
    }

    uf = (struct mca_fbtl_io_uring_file_t *) data->r_fh->f_fbtl_data;
    if (NULL == uf) {
        /* The ring only exists while the file is open. MPI forbids
           closing a file with outstanding I/O requests on it, so this
           too can only be reached in error situations; complete the
           request with what was written/read so far. */
        req->req_ompi.req_status.MPI_ERROR = OMPI_ERROR;
        req->req_ompi.req_status._ucount = data->r_total_len;
        return true;
    }

    /* make room in the in-flight windows first ... */
    mca_fbtl_io_uring_poll_ring(uf);

    /* ... then post another window of this request's pending
       subrequests, and collect whatever completed while they were on
       their way */
    (void) mca_fbtl_io_uring_fill_ring(uf, data);
    mca_fbtl_io_uring_poll_ring(uf);

    if (0 < data->r_inflight || data->r_next < data->r_req_count) {
        return false;
    }

    /* Nothing left to post, nothing in flight: the request has
       settled. Release the locks as early as posix does at this
       point, before the caller finally frees the request; the per
       subrequest region locks have already been dropped by
       apply_cqe() with their completions, what remains is the
       atomicity lock (and anything an error path left behind).
       A recorded error (submission or operation failure)
       turns it into a failed MPI request; otherwise the byte count
       collected from the completions is the result, and a short one
       is the end-of-file indication, exactly like the return value of
       the blocking path. */
    mca_fbtl_io_uring_release_locks(data);

    req->req_ompi.req_status.MPI_ERROR = (data->r_error) ? OMPI_ERROR : OMPI_SUCCESS;
    req->req_ompi.req_status._ucount = data->r_total_len;

    return true;
}
