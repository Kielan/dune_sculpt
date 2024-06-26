#include "lib_mmap.h"
#include "lib_fileops.h"
#include "lib_list.h"
#include "mem_guardedalloc.h"

#include <string.h>

#ifndef WIN32
#  include <signal.h>
#  include <stdlib.h>
#  include <sys/mman.h> /* For mmap. */
#  include <unistd.h>   /* For read close. */
#else
#  include "lib_winstuff.h"
#  include <io.h> /* For open close read. */
#endif

struct lib_mmap_file {
  /* Address to which the file was mapped. */
  char *mem;
  /* Length of the file (and therefore the mapped rgn). */
  size_t length;
  /* Platform-specific handle for the mapping. */
  void *handle;
  /* Flag to indicate IO errs. Must be volatile bc it's being set from
   * inside of the signal handler, not part of the normal ex flow. */
  volatile bool io_error;
};

#ifndef WIN32
/* When using mem-mapped files IO errs result in a SIGBUS signal.
 * Must catch that signal and stop reading the file in question.
 * Keep a list of all current FileDatas using mem-mapped files,
 * and if a SIGBUS is caught, check if the failed address
 * is inside 1 of the mapped rgns.
 * If yes: set a flag to indicate a failed read and remap the mem in
 * question to a zero-backed rgn to avoid additional signals.
 * The code that actually reads the mem area must
 * check if flag was set after it's done reading.
 * If the err occurred outside of a mem-mapped rgn, call the prev
 * handler if 1 was config'd and abort the proc otherwise. */
static struct err_handler_data {
  List open_mmaps;
  char configured;
  void (*next_handler)(int, siginfo_t *, void *);
} err_handler = {0};

static void sigbus_handler(int sig, siginfo_t *siginfo, void *ptr)
{
  /* We only handle SIGBUS here for now. */
  lib_assert(sig == SIGBUS);

  char *err_addr = (char *)siginfo->si_addr;
  /* Find the file that this err belongs to. */
  LIST_FOREACH (LinkData *, link, &err_handler.open_mmaps) {
    lib_mmap_file *file = link->data;

    /* Is the address where the err occurred in this file's mapped range? */
    if (error_addr >= file->mem && err_addr < file->mem + file->length) {
      file->io_err = true;

      /* Replace the mapped mem w zeroes. */
      const void *mapped_mem = mmap(
          file->mem, file->length, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
      if (mapped_mem == MAP_FAILED) {
        fprintf(stderr, "SIGBUS handler: Err replacing mapped file w zeros\n");
      }

      return;
    }
  }

  /* Fall back to other handler if there was one. */
  if (err_handler.next_handler) {
    err_handler.next_handler(sig, siginfo, ptr);
  }
  else {
    fprintf(stderr, "Unhandled SIGBUS caught\n");
    abort();
  }
}

/* Ensures that the err handler is set up and rdy. */
static bool sigbus_handler_setup(void)
{
  if (!error_handler.configured) {
    struct sigaction newact = {0}, oldact = {0};

    newact.sa_sigaction = sigbus_handler;
    newact.sa_flags = SA_SIGINFO;

    if (sigaction(SIGBUS, &newact, &oldact)) {
      return false;
    }

    /* Remember the prev configured handler to fall back to it if the err
     * does not belong to any of the mapped files. */
    err_handler.next_handler = oldact.sa_sigaction;
    err_handler.configured = 1;
  }

  return true;
}

/* Adds a file to the list that the err handler checks. */
static void sigbus_handler_add(lib_mmap_file *file)
{
  lib_addtail(&err_handler.open_mmaps, lib_genericNode(file));
}

/* Removes a file from the list that the err handler checks. */
static void sigbus_handler_remove(lib_mmap_file *file)
{
  LinkData *link = lib_findptr(&err_handler.open_mmaps, file, offsetof(LinkData, data));
  lib_freelink(&err_handler.open_mmaps, link);
}
#endif

lib_mmap_file *lib_mmap_open(int fd)
{
  void *mem, *handle = NULL;
  const size_t length = lib_lseek(fd, 0, SEEK_END);
  if (UNLIKELY(length == (size_t)-1)) {
    return NULL;
  }

#ifndef WIN32
  /* Ensure that the SIGBUS handler is config'd. */
  if (!sigbus_handler_setup()) {
    return NULL;
  }

  /* Map the given file to mem. */
  memory = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
  if (memory == MAP_FAILED) {
    return NULL;
  }
#else
  /* Convert the POSIX-style file descriptor to a Windows handle. */
  void *file_handle = (void *)_get_osfhandle(fd);
  /* Mem mapping on Windows is a 2-step proc: 1st create a mapping,
   * then create a view into that mapping.
   * Our case 1 view that spans the entire file is enough. */
  handle = CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
  if (handle == NULL) {
    return NULL;
  }
  mem = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
  if (mem == NULL) {
    CloseHandle(handle);
    return NULL;
  }
#endif

  /* The mapping was successful,
   * alloc mem and set up the lib_mmap_file. */
  lib_mmap_file *file = mem_calloc(sizeof(MmapFile), __func__);
  file->mem = mem;
  file->handle = handle;
  file->length = length;

#ifndef WIN32
  /* Register file w the err handler. */
  sigbus_handler_add(file);
#endif

  return file;
}

bool lib_mmap_read(MmapFile *file, void *dest, size_t offset, size_t length)
{
  /* If a prev read has alrdy failed or we try to read past the end,
   * don't attempt to read any further. */
  if (file->io_error || (offset + length > file->length)) {
    return false;
  }

#ifndef WIN32
  /* If an err occurs in this call,
   * sigbus_handler is called and set
   * file->io_err to true. */
  memcpy(dest, file->mem + offset, length);
#else
  /* On Windows use exception handling to be notified of errs. */
  __try
  {
    memcpy(dest, file->mem + offset, length);
  }
  __except (GetExceptionCode() == EXCEPTION_IN_PAGE_ERR ? EXCEPTION_EX_HANDLER :
                                                            EXCEPTION_CONTINUE_SEARCH)
  {
    file->io_err = true;
    return false;
  }
#endif

  return !file->io_err;
}

void *lib_mmap_get_ptr(MmapFile *file)
{
  return file->mem;
}

size_t lib_mmap_get_length(const MmapFile *file)
{
  return file->length;
}

void lib_mmap_free(MmapFile *file)
{
#ifndef WIN32
  munmap((void *)file->mem, file->length);
  sigbus_handler_remove(file);
#else
  UnmapViewOfFile(file->mem);
  CloseHandle(file->handle);
#endif

  mem_free(file);
}
