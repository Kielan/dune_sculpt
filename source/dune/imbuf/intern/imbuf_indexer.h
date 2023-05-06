#pragma once

#ifdef WIN32
#  include <io.h>
#endif

#include "imbuf_anim.h"
#include <stdio.h>
#include <stdlib.h>
/*
 * separate animation index files to solve the following problems:
 *
 * a) different time-codes within one file (like DTS/PTS, Time-code-Track,
 *    "implicit" time-codes within DV-files and HDV-files etc.)
 * b) seeking difficulties within FFMPEG for files with timestamp holes
 * c) broken files that miss several frames / have varying frame-rates
 * d) use proxies accordingly
 *
 * ... we need index files, that provide us with
 *
 * the binary(!) position, where we have to seek into the file *and*
 * the continuous frame number (ignoring the holes) starting from the
 * beginning of the file, so that we know, which proxy frame to serve.
 *
 * This index has to be only built once for a file and is written into
 * the BL_proxy directory structure for later reuse in different blender files.
 */

typedef struct anim_index_entry {
  int frameno;
  uint64_t seek_pos;
  uint64_t seek_pos_pts;
  uint64_t seek_pos_dts;
  uint64_t pts;
} anim_index_entry;

struct anim_index {
  char filepath[1024];

  int num_entries;
  struct anim_index_entry *entries;
};

struct anim_index_builder;

typedef struct anim_index_builder {
  FILE *fp;
  char filepath[FILE_MAX];
  char filepath_temp[FILE_MAX];

  void *private_data;

  void (*delete_priv_data)(struct anim_index_builder *idx);
  void (*proc_frame)(struct anim_index_builder *idx,
                     unsigned char *buffer,
                     int data_size,
                     struct anim_index_entry *entry);
} anim_index_builder;

anim_index_builder *imbuf_index_builder_create(const char *filepath);
void imbuf_index_builder_add_entry(anim_index_builder *fp,
                                 int frameno,
                                 uint64_t seek_pos,
                                 uint64_t seek_pos_pts,
                                 uint64_t seek_pos_dts,
                                 uint64_t pts);

void imbuf_index_builder_proc_frame(anim_index_builder *fp,
                                  unsigned char *buffer,
                                  int data_size,
                                  int frameno,
                                  uint64_t seek_pos,
                                  uint64_t seek_pos_pts,
                                  uint64_t seek_pos_dts,
                                  uint64_t pts);

void imbuf_index_builder_finish(anim_index_builder *fp, int rollback);

struct anim_index *imbuf_indexer_open(const char *name);
uint64_t imbuf_indexer_get_seek_pos(struct anim_index *idx, int frame_index);
uint64_t imbuf_indexer_get_seek_pos_pts(struct anim_index *idx, int frame_index);
uint64_t imbuf_indexer_get_seek_pos_dts(struct anim_index *idx, int frame_index);

int imbuf_indexer_get_frame_index(struct anim_index *idx, int frameno);
uint64_t imbuf_indexer_get_pts(struct anim_index *idx, int frame_index);
int imbuf_indexer_get_duration(struct anim_index *idx);

int imbuf_indexer_can_scan(struct anim_index *idx, int old_frame_index, int new_frame_index);

void imbuf_indexer_close(struct anim_index *idx);

void imbuf_free_indices(struct anim *anim);

struct anim *imbuf_anim_open_proxy(struct anim *anim, imbuf_Proxy_Size preview_size);
struct anim_index *imbuf_anim_open_index(struct anim *anim, imbuf_Timecode_Type tc);

int imbuf_proxy_size_to_array_index(imbuf_Proxy_Size pr_size);
int imbuf_timecode_to_array_index(imbuf_Timecode_Type tc);
