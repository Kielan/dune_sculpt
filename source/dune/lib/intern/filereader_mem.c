#include <string.h>

#include "lib_dunelib.h"
#include "lib_filereader.h"
#include "lib_mmap.h"

#include "mem_guardedalloc.h"

/* This file implements both mem-backed and mem-mapped-file-backed reading. */
typedef struct {
  FileReader reader;

  const char *data;
  LibMmapFile *mmap;
  size_t length;
} MemReader;

static int64_t mem_read_raw(FileReader *reader, void *buf, size_t size)
{
  MemReader *mem = (MemReader *)reader;

  /* Don't read more bytes than there are available in the buf. */
  size_t readsize = MIN2(size, (size_t)(mem->length - mem->reader.offset));

  memcpy(buf, mem->data + mem->reader.offset, readsize);
  mem->reader.offset += readsize;

  return readsize;
}

static off64_t mem_seek(FileReader *reader, off64_t offset, int whence)
{
  MemReader *mem = (MemReader *)reader;

  off64_t new_pos;
  if (whence == SEEK_CUR) {
    new_pos = mem->reader.offset + offset;
  }
  else if (whence == SEEK_SET) {
    new_pos = offset;
  }
  else if (whence == SEEK_END) {
    new_pos = mem->length + offset;
  }
  else {
    return -1;
  }

  if (new_pos < 0 || new_pos > mem->length) {
    return -1;
  }

  mem->reader.offset = new_pos;
  return mem->reader.offset;
}

static void mem_close_raw(FileReader *reader)
{
  mem_free(reader);
}

FileReader *lib_filereader_new_mem(const void *data, size_t len)
{
  MemReader *mem = mem_calloc(sizeof(MemReader), __func__);

  mem->data = (const char *)data;
  mem->length = len;

  mem->reader.read = mem_read_raw;
  mem->reader.seek = mem_seek;
  mem->reader.close = mem_close_raw;

  return (FileReader *)mem;
}

/* Mem-mapped file reading.
 * By using `mmap()`, we can map a file so that it can be treated like normal mem,
 * meaning that we can just read from it w `memcpy()` etc.
 * This avoids sys call overhead and can significantly speed up file loading. */
static int64_t mem_read_mmap(FileReader *reader, void *buf, size_t size)
{
  MemReader *mem = (MemReader *)reader;

  /* Don't read more bytes than there are available in the buf. */
  size_t readsize = MIN2(size, (size_t)(mem->length - mem->reader.offset));

  if (!lib_mmap_read(mem->mmap, buf, mem->reader.offset, readsize)) {
    return 0;
  }

  mem->reader.offset += readsize;

  return readsize;
}

static void mem_close_mmap(FileReader *reader)
{
  MemReader *mem = (MemReader *)reader;
  lib_mmap_free(mem->mmap);
  mem_free(mem);
}

FileReader *lib_filereader_new_mmap(int filedes)
{
  lib_mmap_file *mmap = lib_mmap_open(filedes);
  if (mmap == NULL) {
    return NULL;
  }

  MemReader *mem = mem_calloc(sizeof(MemReader), __func__);

  mem->mmap = mmap;
  mem->length = lib_mmap_get_length(mmap);

  mem->reader.read = mem_read_mmap;
  mem->reader.seek = mem_seek;
  mem->reader.close = mem_close_mmap;

  return (FileReader *)mem;
}
