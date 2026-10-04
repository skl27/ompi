/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
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

#include <liburing.h>

/*
 * The component serves every file it is selected for with its own
 * io_uring instance. fbtl modules are static singletons shared by all
 * files, so the per-file ring hangs on the f_fbtl_data pointer of
 * ompio_file_t: it is created in the module's fbtl_module_init() and
 * released in fbtl_module_finalize(), i.e. it lives exactly as long
 * as the open file it belongs to.
 *
 * The progress model is single-threaded: submissions to a ring and
 * harvests from its completion queue happen either from the thread
 * that issued the operations (the blocking wait loop) or from the
 * OMPIO progress function, which is the same assumption the posix
 * component makes for its AIO code.
 */

extern int mca_fbtl_io_uring_priority;
extern int mca_fbtl_io_uring_queue_depth;

/*
 * Per-file state, hung on ompio_file_t:f_fbtl_data.
 */
struct mca_fbtl_io_uring_file_t {
    struct io_uring ring;
};

/* Lifecycle of an individual subrequest (one per f_io_array entry). */
#define MCA_FBTL_IO_URING_PENDING  0  /* not (yet) posted to the ring */
#define MCA_FBTL_IO_URING_INFLIGHT 1  /* posted, completion pending   */
#define MCA_FBTL_IO_URING_DONE     2  /* completion has been applied  */

/* Direction of an I/O request. */
#define MCA_FBTL_IO_URING_READ  0
#define MCA_FBTL_IO_URING_WRITE 1

/*
 * One io_uring submission per entry of a request's f_io_array. The
 * s_data back-pointer lets a completion queue entry find the request
 * it belongs to, so completions of several requests may safely be
 * interleaved on one ring: whoever reaps a completion routes it to
 * its owner through this pointer.
 */
struct mca_fbtl_io_uring_subreq_t {
    int                                      s_fd;
    void                                    *s_memory;
    size_t                                   s_length;
    OMPI_MPI_OFFSET_TYPE                     s_offset;
    int                                      s_state;
    ssize_t                                  s_result;
    struct mca_fbtl_io_uring_request_data_t *s_data;
};

/*
 * Per-request state: stack allocated by the blocking read/write
 * functions, heap allocated (and owned) by an mca_ompio_request_t for
 * the asynchronous ones.
 */
struct mca_fbtl_io_uring_request_data_t {
    ompio_file_t                       *r_fh;
    int                                 r_req_type;
    int                                 r_req_count;
    int                                 r_next;      /* first subrequest not yet posted */
    int                                 r_inflight;  /* subrequests currently in the ring */
    ssize_t                             r_total_len; /* bytes completed so far */
    bool                                r_error;
    int                                 r_error_no;
    struct mca_fbtl_io_uring_subreq_t  *r_subs;
};

extern int mca_fbtl_io_uring_component_init_query(bool enable_progress_threads,
                                                  bool enable_mpi_threads);
extern struct mca_fbtl_base_module_1_0_0_t *
mca_fbtl_io_uring_component_file_query(ompio_file_t *file, int *priority);
extern int mca_fbtl_io_uring_component_file_unquery(ompio_file_t *file);

/*
 * One-shot availability probe of the io_uring syscall: creates a
 * minimal ring and tears it down again, caching the result. A kernel
 * without io_uring support, or one where seccomp blocks the syscall,
 * makes the component unselectable rather than failing I/O later.
 */
extern bool mca_fbtl_io_uring_ring_supported(void);

extern int mca_fbtl_io_uring_module_init(ompio_file_t *file);
extern int mca_fbtl_io_uring_module_finalize(ompio_file_t *file);

#endif /* MCA_FBTL_IO_URING_H */
