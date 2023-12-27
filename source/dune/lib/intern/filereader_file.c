#ifndef WIN32
#  include <unistd.h> /* for read close */
#else
#  include "lib_winstuff.h"
#  include "winsock2.h"
#  include <io.h> /* for open close read */
#endif

#include "lib_dunelib.h"
#include "lib_filereader.h"

#include "mem_guardedalloc.h"

typedef struct {
  FileReader reader;

  int filedes;
} RawFileReader;

static int64_t file_read(FileReader *reader, void *buffer, size_t size)
{
  RawFileReader *rawfile = (RawFileReader *)reader;
  int64_t readsize = lib_read(rawfile->filedes, buffer, size);

  if (readsize >= 0) {
    rawfile->reader.offset += readsize;
  }

  return readsize;
}

static off64_t file_seek(FileReader *reader, off64_t offset, int whence)
{
  RawFileReader *rawfile = (RawFileReader *)reader;
  rawfile->reader.offset = lib_lseek(rawfile->filedes, offset, whence);
  return rawfile->reader.offset;
}

static void file_close(FileReader *reader)
{
  RawFileReader *rawfile = (RawFileReader *)reader;
  close(rawfile->filedes);
  mem_free(rawfile);
}

FileReader *lib_filereader_new_file(int filedes)
{
  RawFileReader *rawfile = mem_calloc(sizeof(RawFileReader), __func__);

  rawfile->filedes = filedes;

  rawfile->reader.read = file_read;
  rawfile->reader.seek = file_seek;
  rawfile->reader.close = file_close;

  return (FileReader *)rawfile;
}
