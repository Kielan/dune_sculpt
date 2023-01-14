#pragma once

/**
 * The lines below use regex from scripts to extract their values,
 * Keep this in mind when modifying this file and keep this comment above the defines.
 *
 * Use STRINGIFY() rather than defining with quotes.
 */

/* Dune major and minor version. */
#define DUNE_VERSION 305
/* Dune patch version for bugfix releases. */
#define DUNE_VERSION_PATCH 0
/** Dune release cycle stage: alpha/beta/rc/release. */
#define DUNE_VERSION_CYCLE alpha

/* Dune file format version. */
#define DUNE_FILE_VERSION DUNE_VERSION
#define DUNE_FILE_SUBVERSION 8

/* Minimum Blender version that supports reading file written with the current
 * version. Older Blender versions will test this and show a warning if the file
 * was written with too new a version. */
#define DUNE_FILE_MIN_VERSION 305
#define DUNE_FILE_MIN_SUBVERSION 8

/** User readable version string. */
const char *KERNEL_dune_version_string(void);

/* Returns true when version cycle is alpha, otherwise (beta, rc) returns false. */
bool KERNEL_dune_version_is_alpha(void);
