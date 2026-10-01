/*
 * Copyright (c) 2026 RWTH Aachen University. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 * SPDX-License-Identifier: BSD-3-Clause-Open-MPI
 *
 * These symbols are in a file by themselves to provide nice linker
 * semantics.  Since linkers generally pull in symbols by object
 * files, keeping these symbols as the only symbols in this file
 * prevents utility programs such as "ompi_info" from having to import
 * entire components just to query their version and parameters.
 */

#include "ompi_config.h"
#include "fbtl_io_uring.h"

/*
 * Public string showing the fbtl io_uring component version number
 */
const char *mca_fbtl_io_uring_component_version_string =
    "OMPI/MPI io_uring FBTL MCA component version " OMPI_VERSION;

int mca_fbtl_io_uring_priority = 10;

/*
 * Private functions
 */
static int register_component(void);

/*
 * Instantiate the public struct with all of our public information
 * and pointers to our public functions in it
 */
mca_fbtl_base_component_2_0_0_t mca_fbtl_io_uring_component = {

    /* First, the mca_component_t struct containing meta information
       about the component itself */

    .fbtlm_version = {
        MCA_FBTL_BASE_VERSION_2_0_0,

        /* Component name and version */
        .mca_component_name = "io_uring",
        MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION,
                              OMPI_RELEASE_VERSION),
        .mca_register_component_params = register_component,
    },
    .fbtlm_data = {
        /* This component is checkpointable */
        MCA_BASE_METADATA_PARAM_CHECKPOINT
    },
    .fbtlm_init_query = mca_fbtl_io_uring_component_init_query,      /* get thread level */
    .fbtlm_file_query = mca_fbtl_io_uring_component_file_query,      /* get priority and actions */
    .fbtlm_file_unquery = mca_fbtl_io_uring_component_file_unquery,  /* undo what was done by previous function */
};
MCA_BASE_COMPONENT_INIT(ompi, fbtl, io_uring)

static int register_component(void)
{
    mca_fbtl_io_uring_priority = 10;
    (void) mca_base_component_var_register(&mca_fbtl_io_uring_component.fbtlm_version,
                                           "priority", "Priority of the fbtl io_uring component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_fbtl_io_uring_priority);

    return OMPI_SUCCESS;
}
