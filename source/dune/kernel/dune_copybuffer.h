#pragma once

#include "lib_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Id;
struct Main;
struct ReportList;
struct Cxt;

/* Copy-buffer (wrapper for dune_file_write_partial). */

/* Initialize a copy operation. */
void dune_copybuffer_copy_begin(struct Main *main_src);
/* Mark an Id to be copied. Should only be called after a call to BKE_copybuffer_copy_begin. */
void dune_copybuffer_copy_tag_id(struct Id *id);
/* Finalize a copy operation into given .dune file 'buffer'.
 *
 * param filename: Full path to the .dune file used as copy/paste buffer.
 *
 * return true on success, false otherwise. */
bool dune_copybuffer_copy_end(struct Main *main_src,
                             const char *filename,
                             struct ReportList *reports);
/* Paste data-blocks from the given .dune file 'buffer' (i.e. append them).
 *
 * Unlike dune_copybuffer_paste, it does not perform any instantiation of collections/objects/etc.
 *
 * param libname: Full path to the .dune file used as copy/paste buffer.
 * param id_types_mask: Only directly link IDs of those types from the given .blend file buffer.
 *
 * return true on success, false otherwise. */
bool dune_copybuffer_read(struct Main *main_dst,
                         const char *libname,
                         struct ReportList *reports,
                         uint64_t id_types_mask);
/* Paste data-blocks from the given .dune file 'buffer'  (i.e. append them).
 *
 * Similar to dune_copybuffer_read, but also handles instantiation of collections/objects/etc.
 *
 * param libname: Full path to the .dune file used as copy/paste buffer.
 * param flag: A combination of eBLOLibLinkFlags andeFileSel_Params_Flag to control
 * link/append behavior.
 * note Ignores FILE_LINK flag, since it always appends Ids.
 * param id_types_mask: Only directly link Ids of those types from the given .dune file buffer.
 *
 * return Number of Ids directly pasted from the buffer
 * (does not includes indirectly linked ones). */
int dune_copybuffer_paste(struct Cxt *C,
                         const char *libname,
                         int flag,
                         struct ReportList *reports,
                         uint64_t id_types_mask);

#ifdef __cplusplus
}
#endif
