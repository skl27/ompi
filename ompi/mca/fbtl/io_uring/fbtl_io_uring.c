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

#include <string.h>
#include <liburing.h>

#include "opal/util/output.h"

#include "fbtl_io_uring.h"

/*
 * Cached result of the io_uring availability probe, run once during
 * the component's init_query(). See mca_fbtl_io_uring_ring_supported()
 * below for what is probed and why.
 */
static bool io_uring_probe_done = false;
static bool io_uring_probe_ok = false;

bool mca_fbtl_io_uring_ring_supported(void)
{
    if (!io_uring_probe_done) {
        struct io_uring probe_ring;

        io_uring_probe_done = true;

        /* The probe only has to find out whether the io_uring syscall
           exists and is allowed by any installed seccomp filter; the
           size of the ring is irrelevant for that. Real rings are
           sized per file by the queue_depth MCA parameter, and their
           creation may still fail for resource reasons
           (RLIMIT_MEMLOCK, file descriptors) - that failure is handled
           where the open() itself reports errors. */
        if (0 == io_uring_queue_init(8, &probe_ring, 0)) {
            io_uring_queue_exit(&probe_ring);
            io_uring_probe_ok = true;
        }
    }

    return io_uring_probe_ok;
}

int mca_fbtl_io_uring_component_init_query(bool enable_progress_threads,
                                           bool enable_mpi_threads)
{
    /* Run the availability probe now, while it is cheap and
       infrequent, instead of on the first MPI_File_open(). */
    (void) mca_fbtl_io_uring_ring_supported();

    /* io_uring_queue_init() needs a usable submission queue size;
       fall back to the default instead of failing every open. */
    if (1 > mca_fbtl_io_uring_queue_depth) {
        opal_output(0, "fbtl:io_uring: ignoring invalid queue_depth %d, using the default of 128\n",
                    mca_fbtl_io_uring_queue_depth);
        mca_fbtl_io_uring_queue_depth = 128;
    }

    return OMPI_SUCCESS;
}

/*
 * Create the io_uring instance that serves this file for as long as
 * it is open; the byte transfer functions find it through
 * file->f_fbtl_data. A failure here fails the MPI_File_open() call:
 * fbtl_base_file_select() propagates the module_init() return code,
 * so a system whose resources ran out since the availability probe
 * degrades to an open error, not to broken I/O later.
 */
int mca_fbtl_io_uring_module_init(ompio_file_t *file)
{
    struct mca_fbtl_io_uring_file_t *uf;
    int ret;

    uf = (struct mca_fbtl_io_uring_file_t *)
        calloc(1, sizeof(struct mca_fbtl_io_uring_file_t));
    if (NULL == uf) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    /* liburing rounds the submission queue size up to the next power
       of two and sizes the completion queue accordingly; the
       queue_depth MCA parameter was sanitized in init_query(). */
    ret = io_uring_queue_init(mca_fbtl_io_uring_queue_depth, &uf->ring, 0);
    if (0 != ret) {
        opal_output(0, "fbtl:io_uring: could not create io_uring instance for file %s: %s\n",
                    file->f_filename, strerror(-ret));
        free(uf);
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    file->f_fbtl_data = (void *) uf;

    return OMPI_SUCCESS;
}

int mca_fbtl_io_uring_module_finalize(ompio_file_t *file)
{
    struct mca_fbtl_io_uring_file_t *uf =
        (struct mca_fbtl_io_uring_file_t *) file->f_fbtl_data;

    if (NULL != uf) {
        /* No outstanding requests can exist here: the MPI standard
           requires all non-blocking I/O on a file handle to have
           completed before MPI_File_close(), which is where fbtl
           modules are finalized. io_uring_queue_exit() releases the
           ring, waiting for nothing except unsubmitted entries. */
        io_uring_queue_exit(&uf->ring);
        free(uf);
        file->f_fbtl_data = NULL;
    }

    return OMPI_SUCCESS;
}

/*
 * Static module: all per-file state will live on the file, not in
 * this struct, so one instance can serve every file the component is
 * selected for.
 */
static mca_fbtl_base_module_1_0_0_t io_uring_module = {
    .fbtl_module_init      = mca_fbtl_io_uring_module_init,
    .fbtl_module_finalize  = mca_fbtl_io_uring_module_finalize,

    .fbtl_preadv           = mca_fbtl_io_uring_preadv,
    .fbtl_pwritev          = mca_fbtl_io_uring_pwritev,

    /*
     * The asynchronous paths and check_atomicity() are filled in by
     * follow-up commits. Until those exist and file_query() hands out
     * this module, the NULL entries below are never called; the
     * blocking functions above are covered by the fact that
     * file_query() still returns NULL.
     */
    .fbtl_ipreadv          = NULL,
    .fbtl_ipwritev         = NULL,
    .fbtl_progress         = NULL,
    .fbtl_request_free     = NULL,
    .fbtl_check_atomicity  = NULL,
};

struct mca_fbtl_base_module_1_0_0_t *
mca_fbtl_io_uring_component_file_query(ompio_file_t *file, int *priority)
{
    /*
     * Stub: the io_uring byte transfer functions are not implemented
     * yet, so this component must never be selected. Once they are,
     * this becomes the availability probe plus the configured
     * priority, which keeps the component unselected by default.
     */
    return NULL;
}

int mca_fbtl_io_uring_component_file_unquery(ompio_file_t *file)
{
    /* file_query() never hands out this module, so there is nothing
       to undo */
    return OMPI_SUCCESS;
}
