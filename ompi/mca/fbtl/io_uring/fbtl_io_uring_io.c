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
#include <string.h>
#include <errno.h>
#include <liburing.h>

#include "opal/util/output.h"

#include "fbtl_io_uring.h"

/*
 * The shared engine behind the blocking and the asynchronous byte
 * transfer functions: a request is described by one
 * mca_fbtl_io_uring_request_data_t with one subrequest per entry of
 * the file's f_io_array. Whoever drives a request repeatedly calls
 * mca_fbtl_io_uring_fill_ring() to post pending subrequests and feeds
 * every harvested completion queue entry to
 * mca_fbtl_io_uring_apply_cqe(), which routes it to its owning
 * request through the subrequest back-pointer. Completions of several
 * requests may therefore be interleaved on one ring.
 */

/*
 * Initialize the per-request state from the file's I/O vector. The
 * offset of an entry is handed over in the generic void * offset
 * field of the io_array, as elsewhere in OMPIO.
 *
 * Zero-length subrequests are marked done up front: there is nothing
 * to transfer for them, and read(2)/write(2) would merely confirm
 * that with a zero-byte completion.
 */
int mca_fbtl_io_uring_setup(struct mca_fbtl_io_uring_request_data_t *data,
                            ompio_file_t *file, int req_type)
{
    int i;

    data->r_fh = file;
    data->r_req_type = req_type;
    data->r_req_count = file->f_num_of_io_entries;
    data->r_next = 0;
    data->r_inflight = 0;
    data->r_total_len = 0;
    data->r_error = false;
    data->r_error_no = 0;
    data->r_alock_counter = 0;

    if (0 == data->r_req_count) {
        data->r_subs = NULL;
        return OMPI_SUCCESS;
    }

    data->r_subs = (struct mca_fbtl_io_uring_subreq_t *)
        calloc(data->r_req_count, sizeof(struct mca_fbtl_io_uring_subreq_t));
    if (NULL == data->r_subs) {
        opal_output(1, "mca_fbtl_io_uring_setup: could not allocate memory\n");
        data->r_req_count = 0;
        return OMPI_ERROR;
    }

    for (i = 0; i < data->r_req_count; i++) {
        struct mca_fbtl_io_uring_subreq_t *sub = &data->r_subs[i];
        sub->s_fd = file->fd;
        sub->s_memory = file->f_io_array[i].memory_address;
        sub->s_offset = (OMPI_MPI_OFFSET_TYPE)(intptr_t) file->f_io_array[i].offset;
        sub->s_length = file->f_io_array[i].length;
        sub->s_result = 0;
        sub->s_data = data;
        if (0 == sub->s_length) {
            sub->s_state = MCA_FBTL_IO_URING_DONE;
        } else {
            sub->s_state = MCA_FBTL_IO_URING_PENDING;
        }
    }

    return OMPI_SUCCESS;
}

/*
 * Post pending subrequests to the file's ring, starting at
 * data->r_next, until either all of them are posted, the submission
 * queue runs out of slots, or the in-flight windows are full: at most
 * queue_depth operations for this request and, across all requests
 * sharing the ring, at most queue_depth operations for the file (see
 * uf_inflight in the header). Subrequests that could not be posted
 * stay pending and are picked up on the next call.
 *
 * Subrequests are only counted as in flight after io_uring_submit()
 * really accepted the batch, so a submission failure leaves the
 * request state untouched apart from the recorded error. A negative
 * return means the kernel consumed no submission queue entry at all;
 * the windowing on queue_depth keeps the completion queue from
 * overflowing, which is the main source of submission errors, so this
 * path is a once-per-errno report rather than a retry mechanism.
 */
int mca_fbtl_io_uring_fill_ring(struct mca_fbtl_io_uring_file_t *uf,
                                struct mca_fbtl_io_uring_request_data_t *data)
{
    struct io_uring *ring = &uf->ring;
    struct mca_fbtl_io_uring_subreq_t *sub;
    int i, start, end, posted, room, ret;

    if (data->r_error) {
        /* a failed request stops issuing new operations; the caller
           still drains the ones that are in flight */
        return OMPI_SUCCESS;
    }

    start = data->r_next;
    room = mca_fbtl_io_uring_queue_depth - data->r_inflight;
    if (mca_fbtl_io_uring_queue_depth - uf->uf_inflight < room) {
        room = mca_fbtl_io_uring_queue_depth - uf->uf_inflight;
    }
    posted = 0;

    for (i = start; i < data->r_req_count && posted < room; i++) {
        struct io_uring_sqe *sqe;

        sub = &data->r_subs[i];
        if (MCA_FBTL_IO_URING_PENDING != sub->s_state) {
            continue;
        }

        /* Lock the exact byte range of the subrequest for as long
           as it is in flight. The posix component does this with
           OMPIO_LOCK_ENTIRE_REGION for its single and datasieving
           paths; applying it to every subrequest matches those two
           and strictly covers more than the span-edge locking of
           the generic path, while never locking the holes between
           strided entries. The lock is taken once per subrequest,
           not once per posting: a resubmitted short completion
           keeps the (superset) lock it already holds. With an
           enforced atomicity the whole region is locked anyway, and
           unlocking a completed subrequest would punch a hole into
           that still-held atomicity lock, so the per-subrequest
           locks are skipped entirely. */
        if (0 == sub->s_lock_counter && !data->r_fh->f_atomicity) {
            ret = mca_fbtl_io_uring_lock(&sub->s_lock, data->r_fh,
                                         (MCA_FBTL_IO_URING_WRITE == data->r_req_type) ?
                                         F_WRLCK : F_RDLCK,
                                         sub->s_offset, (off_t) sub->s_length,
                                         OMPIO_LOCK_ENTIRE_REGION, &sub->s_lock_counter);
            if (0 != ret) {
                if (!data->r_error) {
                    data->r_error = true;
                    data->r_error_no = errno;
                }
                break;
            }
        }

        sqe = io_uring_get_sqe(ring);
        if (NULL == sqe) {
            /* submission queue full: since every fill ends with a
               submit, this only means the queue was smaller than
               expected; leave the subrequest pending */
            break;
        }

        if (MCA_FBTL_IO_URING_WRITE == data->r_req_type) {
            io_uring_prep_write(sqe, sub->s_fd, sub->s_memory,
                                (unsigned) sub->s_length, sub->s_offset);
        } else {
            io_uring_prep_read(sqe, sub->s_fd, sub->s_memory,
                               (unsigned) sub->s_length, sub->s_offset);
        }
        io_uring_sqe_set_data(sqe, sub);
        posted++;
    }

    /* i is the end of the scanned range: either the request array,
       the flight window, or a full submission queue stopped us */
    end = i;

    if (0 == posted) {
        /* a failed lock stops issuing just like a failed submit does */
        data->r_next = (data->r_error) ? data->r_req_count : end;
        return OMPI_SUCCESS;
    }

    ret = io_uring_submit(ring);
    if (ret < 0) {
        if (!data->r_error) {
            data->r_error = true;
            data->r_error_no = -ret;
        }
        /* stop issuing, let the caller drain what is still in
           flight, and report the error once it is empty */
        data->r_next = data->r_req_count;
        return OMPI_SUCCESS;
    }

    /* Mark exactly the subrequests the kernel took, in the order they
       were filled above; the rest stay pending. A partial submit
       without an error is practically impossible with a per-file
       ring whose queue is empty on entry, but treating it as a
       request failure is the safe answer if it ever happens. */
    if (ret < posted) {
        if (!data->r_error) {
            data->r_error = true;
            data->r_error_no = EAGAIN;
        }
    }
    room = ret < posted ? ret : posted;
    for (i = start; i < end && 0 < room; i++) {
        sub = &data->r_subs[i];
        if (MCA_FBTL_IO_URING_PENDING == sub->s_state) {
            sub->s_state = MCA_FBTL_IO_URING_INFLIGHT;
            data->r_inflight++;
            uf->uf_inflight++;
            room--;
        }
    }
    if (data->r_error) {
        /* partial submit: stop issuing, drain what the kernel took */
        data->r_next = data->r_req_count;
    } else {
        data->r_next = end;
    }

    return OMPI_SUCCESS;
}

/*
 * Apply one completion to the request that owns it. The owning
 * subrequest is found through the submission queue entry user data,
 * which is why a completion of any in-flight request can be consumed
 * from any of this component's wait and progress paths: whoever reaps
 * it routes it here, and it is never "kept" by the harvester.
 *
 * Three outcomes, matching read(2)/write(2) semantics:
 *
 * - a negative result is an error: the first one per request is
 *   recorded and no new operations are issued for that request
 *   anymore; the caller drains the in-flight ones and reports the
 *   error, like the posix component does for AIO failures;
 *
 * - a short completion (some, but not all requested bytes) is
 *   resubmitted for the remainder, mirroring the retry loops of the
 *   posix blocking paths so that a short read at end-of-file in the
 *   middle of the vector does not silently truncate later
 *   subrequests;
 *
 * - a zero-byte completion terminates a subrequest: for reads it is
 *   the end-of-file indication, and a short retry would return zero
 *   again. The posix AIO code can spin forever on this case; io_uring
 *   completions must not.
 */
void mca_fbtl_io_uring_apply_cqe(struct io_uring_cqe *cqe)
{
    struct mca_fbtl_io_uring_subreq_t *sub =
        (struct mca_fbtl_io_uring_subreq_t *) io_uring_cqe_get_data(cqe);
    struct mca_fbtl_io_uring_request_data_t *data;
    int idx;
    unsigned res;

    if (NULL == sub) {
        /* never happens: every submission queue entry we fill gets a
           subrequest as its user data */
        return;
    }

    data = sub->s_data;
    data->r_inflight--;
    /* the file-wide window frees the same slot; a request abandoned
       its completions only if the ring already broke down, in which
       case the file is beyond saving anyway */
    if (NULL != data->r_fh->f_fbtl_data) {
        ((struct mca_fbtl_io_uring_file_t *) data->r_fh->f_fbtl_data)->uf_inflight--;
    }

    if (cqe->res < 0) {
        sub->s_result = cqe->res;
        sub->s_state = MCA_FBTL_IO_URING_DONE;
        if (0 != sub->s_lock_counter) {
            mca_fbtl_io_uring_unlock(&sub->s_lock, data->r_fh, &sub->s_lock_counter);
        }
        if (!data->r_error) {
            data->r_error = true;
            data->r_error_no = -cqe->res;
            data->r_next = data->r_req_count;
        }
        return;
    }

    res = (unsigned) cqe->res;
    sub->s_result = res;
    data->r_total_len += res;

    if (0 == res || res >= (unsigned) sub->s_length) {
        sub->s_state = MCA_FBTL_IO_URING_DONE;
        if (0 != sub->s_lock_counter) {
            mca_fbtl_io_uring_unlock(&sub->s_lock, data->r_fh, &sub->s_lock_counter);
        }
    } else {
        /* short completion: count what made it, and queue the
           remainder as a fresh pending subrequest, keeping its
           region lock; rewinding r_next lets fill_ring pick it
           up again */
        sub->s_offset += res;
        sub->s_memory = (char *) sub->s_memory + res;
        sub->s_length -= res;
        sub->s_state = MCA_FBTL_IO_URING_PENDING;
        idx = (int) (sub - data->r_subs);
        if (idx < data->r_next) {
            data->r_next = idx;
        }
    }
}

/*
 * Drive a request on its file's ring until nothing is left to post
 * and nothing is in flight: the blocking wait loop of
 * preadv()/pwritev(). Completions belonging to other requests that
 * share the ring are routed to their owners by apply_cqe() on the
 * way.
 */
void mca_fbtl_io_uring_run_blocking(struct mca_fbtl_io_uring_file_t *uf,
                                    struct mca_fbtl_io_uring_request_data_t *data)
{
    bool waiting = true;

    while (waiting &&
           (0 < data->r_inflight || data->r_next < data->r_req_count)) {

        (void) mca_fbtl_io_uring_fill_ring(uf, data);

        while (0 < data->r_inflight) {
            struct io_uring_cqe *cqe = NULL;
            int ret;

            do {
                ret = io_uring_wait_cqe(&uf->ring, &cqe);
            } while (-EINTR == ret);

            if (0 != ret) {
                /* The ring is unusable or lost completions; there is
                   no way to account for the in-flight operations
                   anymore. Fail the request instead of waiting
                   forever for completions that may never come. The
                   file-wide window may keep slots counted that are
                   really in flight, but a broken ring only ever
                   concerns a file that is about to be closed
                   anyway. */
                if (!data->r_error) {
                    data->r_error = true;
                    data->r_error_no = -ret;
                }
                data->r_inflight = 0;
                waiting = false;
                break;
            }

            mca_fbtl_io_uring_apply_cqe(cqe);
            io_uring_cqe_seen(&uf->ring, cqe);
        }
    }
}

/*
 * Harvest every completion that is available on the file's ring right
 * now, without ever blocking. Completions of other requests sharing
 * the ring are routed to their owners just like in the blocking loop,
 * so a single poll advances every outstanding request of this file.
 */
void mca_fbtl_io_uring_poll_ring(struct mca_fbtl_io_uring_file_t *uf)
{
    struct io_uring_cqe *cqe = NULL;
    int ret;

    while (0 == (ret = io_uring_peek_cqe(&uf->ring, &cqe))) {
        mca_fbtl_io_uring_apply_cqe(cqe);
        io_uring_cqe_seen(&uf->ring, cqe);
    }

    if (ret != -EAGAIN) {
        /* io_uring_peek_cqe() only fails like this when the ring is
           broken; there is nothing to harvest and nothing sensible to
           attribute the failure to. The next submission or the
           blocking drain will run into the same problem and report it
           for the request that hits it. */
        opal_output(1, "mca_fbtl_io_uring_poll_ring: unexpected completion queue error %d\n",
                    -ret);
    }
}

/*
 * Block until every operation this request still has on its file's
 * ring has been harvested: used before the subrequest array that the
 * completion queue entries point into is freed. Completions of other
 * requests that turn up on the way are routed to their owners.
 */
void mca_fbtl_io_uring_drain_ring(struct mca_fbtl_io_uring_file_t *uf,
                                  struct mca_fbtl_io_uring_request_data_t *data)
{
    while (0 < data->r_inflight) {
        struct io_uring_cqe *cqe = NULL;
        int ret;

        do {
            ret = io_uring_wait_cqe(&uf->ring, &cqe);
        } while (-EINTR == ret);

        if (0 != ret) {
            /* same last-resort abandonment as the blocking wait loop
               above: the ring is broken, waiting further can only
               hang */
            if (!data->r_error) {
                data->r_error = true;
                data->r_error_no = -ret;
            }
            data->r_inflight = 0;
            break;
        }

        mca_fbtl_io_uring_apply_cqe(cqe);
        io_uring_cqe_seen(&uf->ring, cqe);
    }
}

/* Release whatever locks the request might still hold (regions of
   operations that never completed, e.g. after an error aborted the
   request, and the atomicity lock) and free the subrequest array;
   the caller owns the data struct itself, which is stack allocated
   for blocking requests. */
void mca_fbtl_io_uring_request_cleanup(struct mca_fbtl_io_uring_request_data_t *data)
{
    mca_fbtl_io_uring_release_locks(data);
    free(data->r_subs);
    data->r_subs = NULL;
}
