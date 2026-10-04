/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2017-2021 University of Houston. All rights reserved.
 * Copyright (c) 2018      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2026      RWTH Aachen University. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 * SPDX-License-Identifier: BSD-3-Clause-Open-MPI
 */

#include "ompi_config.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "opal/util/output.h"

#include "ompi/constants.h"
#include "ompi/mca/fbtl/fbtl.h"

#include "fbtl_io_uring.h"

/*
 * POSIX record locking helpers for the io_uring byte transfer layer,
 * adapted from the posix component's fbtl_posix_lock.c so that both
 * components present the same locking semantics to the rest of OMPIO:
 * the fs component steers through fh->f_flags, the operation through
 * the flags argument, and an enforced MPI atomicity is expressed as a
 * lock over the whole I/O region of the request.
 *
 * The lock counter follows the posix convention: while it is above one
 * a lock is already held for the caller's storage slot and further
 * lock/unlock pairs merely nest without extra syscalls.
 */

#define MAX_ERRCOUNT 100

/*
  op:    can be F_WRLCK or F_RDLCK
  flags: can be OMPIO_LOCK_ENTIRE_REGION or OMPIO_LOCK_SELECTIVE. This is typically set by the operation, not the fs component.
         e.g. a collective and an individual component might require different level of protection through locking,
         also one might need to do different things for blocking (pwritev,preadv) operations and non-blocking (aio) operations.

  fh->f_flags can contain similar sounding flags, those were set by the fs component and/or user requests.
*/

int mca_fbtl_io_uring_lock ( struct flock *lock, ompio_file_t *fh, int op,
                             OMPI_MPI_OFFSET_TYPE offset, off_t len, int flags,
                             int *lock_counter)
{
    off_t lmod, bmod;
    int ret, err_count;

    *lock_counter = *lock_counter + 1;
    if ( (*lock_counter) > 1 ) {
        /*
        ** This lock was already initialized, most likely through an atomicity operation.
        ** No need to do anything except increment the lock_counter;
        */
        return 0;
    }

    lock->l_type   = op;
    lock->l_whence = SEEK_SET;
    lock->l_start  =-1;
    lock->l_len    =-1;
    lock->l_pid    = 0;

    if ( 0 == len ) {
        return 0;
    }

    if ( fh->f_flags & OMPIO_LOCK_ENTIRE_FILE ) {
        lock->l_start = (off_t) 0;
        lock->l_len   = 0;
    }
    else {
        if ( (fh->f_flags & OMPIO_LOCK_NEVER) ||
             (fh->f_flags & OMPIO_LOCK_NOT_THIS_OP )){
            /* OMPIO_LOCK_NEVER:
                 ompio tells us not to worry about locking. This can be due to three
                 reasons:
                 1. user enforced
                 2. single node job where the locking is handled already in the kernel
                 3. file view is set to distinct regions such that multiple processes
                    do not collide on the block level.
               OMPIO_LOCK_NOT_THIS_OP:
                 will typically be set by fcoll components indicating that the file partitioning
                 ensures no overlap in blocks.
            */
            return 0;
        }
        if ( flags == OMPIO_LOCK_ENTIRE_REGION ) {
            lock->l_start = (off_t) offset;
            lock->l_len   = len;
        }
        else {
            /* We only try to lock the first block in the data range if
               the starting offset is not the starting offset of a file
               system block. And the last block in the data range if the
               offset+len is not equal to the end of a file system
               block. If we need to lock both beginning + end, we
               combine the two into a single lock.
            */
            bmod = offset % fh->f_fs_block_size;
            if ( bmod  ) {
                lock->l_start = (off_t) offset;
                lock->l_len   = bmod;
            }
            lmod = (offset+len)%fh->f_fs_block_size;
            if ( lmod ) {
                if ( !bmod ) {
                    lock->l_start = (offset+len-lmod );
                    lock->l_len   = lmod;
                }
                else {
                    lock->l_len = len;
                }
            }
            if ( -1 == lock->l_start && -1 == lock->l_len ) {
                /* no need to lock in this instance */
                return 0;
            }
        }
    }

#ifdef OMPIO_DEBUG
    printf("%d: acquiring lock for offset %ld length %ld requested offset %ld request len %ld \n",
           fh->f_rank, lock->l_start, lock->l_len, offset, len);
#endif
    err_count=0;
    do {
        errno=0;
        ret = fcntl ( fh->fd, F_SETLKW, lock);
        if ( ret ) {
#ifdef OMPIO_DEBUG
            printf("[%d] ret = %d errno=%d %s\n", fh->f_rank, ret, errno, strerror(errno) );
#endif
            err_count++;
        }
    } while (  ret && ((errno == EINTR) || ((errno == EINPROGRESS) && err_count < MAX_ERRCOUNT )));

    return ret;
}

void  mca_fbtl_io_uring_unlock ( struct flock *lock, ompio_file_t *fh, int *lock_counter)
{
    *lock_counter  = *lock_counter - 1;
    if ( (*lock_counter) > 0 ) {
        return;
    }

    if ( -1 == lock->l_start && -1 == lock->l_len ) {
        return;
    }

    lock->l_type = F_UNLCK;
#ifdef OMPIO_DEBUG
    printf("%d: releasing lock for offset %ld length %ld\n", fh->f_rank, lock->l_start, lock->l_len);
#endif
    fcntl ( fh->fd, F_SETLK, lock);
    lock->l_start = -1;
    lock->l_len   = -1;

    return;
}

/*
 * Take the atomicity lock for a request whose MPI atomicity was
 * enforced through MPI_File_set_atomicity(): a single write lock (or
 * read lock for input operations) over the whole region covered by the
 * file's current I/O vector, temporarily overriding the fs component's
 * wish not to have this operation locked. Mirrors the
 * OMPIO_SET_ATOMICITY_LOCK macro of the posix component.
 *
 * Returns 0 if no lock was required or the lock was taken, -1 on
 * failure with errno set.
 */
int mca_fbtl_io_uring_set_atomicity_lock(struct mca_fbtl_io_uring_request_data_t *data,
                                         int op)
{
    ompio_file_t *fh = data->r_fh;
    uint32_t orig_flags;
    off_t end_offset, len;

    if (!fh->f_atomicity || 0 == fh->f_num_of_io_entries) {
        return 0;
    }

    orig_flags = fh->f_flags;
    fh->f_flags &= ~OMPIO_LOCK_NEVER;
    fh->f_flags &= ~OMPIO_LOCK_NOT_THIS_OP;

    end_offset = (off_t) fh->f_io_array[fh->f_num_of_io_entries - 1].offset +
                 (off_t) fh->f_io_array[fh->f_num_of_io_entries - 1].length;
    len = end_offset - (off_t) fh->f_io_array[0].offset;

    int ret = mca_fbtl_io_uring_lock (&data->r_alock, fh, op,
                                      (OMPI_MPI_OFFSET_TYPE)(intptr_t) fh->f_io_array[0].offset,
                                      len, OMPIO_LOCK_ENTIRE_REGION, &data->r_alock_counter);
    fh->f_flags = orig_flags;

    return ret;
}

/*
 * Release every lock a request might still hold: the atomicity lock
 * and the region locks of all subrequests that were submitted but
 * never completed (or completed on an error path that did not get to
 * unlock them). Unlocking an already released lock is a no-op, so this
 * can safely be called when a request settles, before its subrequest
 * array goes away, regardless of how the request ended.
 */
void mca_fbtl_io_uring_release_locks(struct mca_fbtl_io_uring_request_data_t *data)
{
    int i;

    for (i = 0; i < data->r_req_count; i++) {
        struct mca_fbtl_io_uring_subreq_t *sub = &data->r_subs[i];
        if (0 != sub->s_lock_counter) {
            mca_fbtl_io_uring_unlock(&sub->s_lock, data->r_fh, &sub->s_lock_counter);
        }
    }

    if (0 != data->r_alock_counter) {
        mca_fbtl_io_uring_unlock(&data->r_alock, data->r_fh, &data->r_alock_counter);
    }
}

/*
 * Report whether an enforced MPI atomicity (MPI_File_set_atomicity())
 * can be implemented on this file by locking it. Adapted verbatim in
 * behaviour from the posix component: probe the file with F_GETLK for a
 * write lock over the whole file and treat any successful reply as
 * "locking works here" - the kernel only reports on whether the
 * filesystem supports POSIX record locks at all, not on whether some
 * other process happens to hold one. A failing probe (e.g. ENOLCK or
 * EPERM on a filesystem that disallows locking) makes the caller in
 * io_ompio refuse the atomicity flag.
 */
bool mca_fbtl_io_uring_check_atomicity(ompio_file_t *file)
{
    struct flock lock;

    lock.l_type   = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start  = 0;
    lock.l_len    = 0;
    lock.l_pid    = 0;

    if (fcntl(file->fd, F_GETLK, &lock) < 0) {
        return false;
    }

    return true;
}
