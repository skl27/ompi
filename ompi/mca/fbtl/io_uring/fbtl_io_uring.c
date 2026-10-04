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

    return OMPI_SUCCESS;
}

/*
 * There is nothing for the module to initialize or release per file
 * yet: the byte transfer functions below are stubs, and once they are
 * implemented their io_uring ring will live in
 * struct mca_fbtl_io_uring_file_t on file->f_fbtl_data, created here
 * and torn down in finalize.
 */
int mca_fbtl_io_uring_module_init(ompio_file_t *file)
{
    return OMPI_SUCCESS;
}

int mca_fbtl_io_uring_module_finalize(ompio_file_t *file)
{
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

    /*
     * The byte transfer functions are filled in by follow-up
     * commits. Until then file_query() never hands out this module,
     * so the NULL entries are never called.
     */
    .fbtl_preadv           = NULL,
    .fbtl_ipreadv          = NULL,
    .fbtl_pwritev          = NULL,
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
