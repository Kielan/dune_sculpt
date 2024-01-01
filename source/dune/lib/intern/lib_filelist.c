#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef WIN32
#  include <dirent.h>
#endif

#include <string.h> /* #strcpy etc. */
#include <sys/stat.h>
#include <time.h>

#ifdef WIN32
#  include "lib_winstuff.h"
#  include "utfconv.h"
#  include <direct.h>
#  include <io.h>
#else
#  include <pwd.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

/* lib includes */
#include "mem_guardedalloc.h"

#include "types_list.h"

#include "lib_fileops.h"
#include "lib_fileops_types.h"
#include "lib_list.h"
#include "lib_path_util.h"
#include "lib_string.h"

#include "../imbuf/imbuf.h"

/* Ordering n for sorting lists of files/directories. Returns -1 if
 * entry1 belongs before entry2, 0 if they are equal, 1 if they should be swapped. */
static int lib_compare(struct direntry *entry1, struct direntry *entry2)
{
  /* type is equal to stat.st_mode */
  /* dirs come before non-dirs */
  if (S_ISDIR(entry1->type)) {
    if (S_ISDIR(entry2->type) == 0) {
      return -1;
    }
  }
  else {
    if (S_ISDIR(entry2->type)) {
      return 1;
    }
  }
  /* non-regular files come after regular files */
  if (S_ISREG(entry1->type)) {
    if (S_ISREG(entry2->type) == 0) {
      return -1;
    }
  }
  else {
    if (S_ISREG(entry2->type)) {
      return 1;
    }
  }
  /* arbitrary but consistent, ordering of diff types of non-regular files */
  if ((entry1->type & S_IFMT) < (entry2->type & S_IFMT)) {
    return -1;
  }
  if ((entry1->type & S_IFMT) > (entry2->type & S_IFMT)) {
    return 1;
  }

  /* OK, now we know their S_IFMT fields are the same, go on to a name comparison */
  /* make sure "." and ".." are always first */
  if (FILENAME_IS_CURRENT(entry1->relname)) {
    return -1;
  }
  if (FILENAME_IS_CURRENT(entry2->relname)) {
    return 1;
  }
  if (FILENAME_IS_PARENT(entry1->relname)) {
    return -1;
  }
  if (FILENAME_IS_PARENT(entry2->relname)) {
    return 1;
  }

  return (lib_strcasecmp_natural(entry1->relname, entry2->relname));
}

struct BuildDirCxt {
  struct direntry *files; /* array[nrfiles] */
  int nrfiles;
};

/* Scans the dir named *dirname and appends entries for its contents to files */
static void lib_builddir(struct BuildDirCxt *dir_cxt, const char *dirname)
{
  struct List dirbase = {NULL, NULL};
  int newnum = 0;
  DIR *dir;

  if ((dir = opendir(dirname)) != NULL) {
    const struct dirent *fname;
    bool has_current = false, has_parent = false;

    while ((fname = readdir(dir)) != NULL) {
      struct dirlink *const dlink = (struct dirlink *)malloc(sizeof(struct dirlink));
      if (dlink != NULL) {
        dlink->name = lib_strdup(fname->d_name);
        if (FILENAME_IS_PARENT(dlink->name)) {
          has_parent = true;
        }
        else if (FILENAME_IS_CURRENT(dlink->name)) {
          has_current = true;
        }
        lib_addhead(&dirbase, dlink);
        newnum++;
      }
    }

    if (!has_parent) {
      char pardir[FILE_MAXDIR];

      lib_strncpy(pardir, dirname, sizeof(pardir));
      if (lib_path_parent_dir(pardir) && (lib_access(pardir, R_OK) == 0)) {
        struct dirlink *const dlink = (struct dirlink *)malloc(sizeof(struct dirlink));
        if (dlink != NULL) {
          dlink->name = lib_strdup(FILENAME_PARENT);
          lib_addhead(&dirbase, dlink);
          newnum++;
        }
      }
    }
    if (!has_current) {
      struct dirlink *const dlink = (struct dirlink *)malloc(sizeof(struct dirlink));
      if (dlink != NULL) {
        dlink->name = lib_strdup(FILENAME_CURRENT);
        lib_addhead(&dirbase, dlink);
        newnum++;
      }
    }

    if (newnum) {
      if (dir_cxt->files) {
        void *const tmp = mem_reallocn(dir_cxt->files,
                                       (dir_cxt->nrfiles + newnum) * sizeof(struct direntry));
        if (tmp) {
          dir_cxt->files = (struct direntry *)tmp;
        }
        else { /* realloc fail */
          mem_freen(dir_cxt->files);
          dir_cxt->files = NULL;
        }
      }

      if (dir_cxt->files == NULL) {
        dir_cxt->files = (struct direntry *)mem_mallocn(newnum * sizeof(struct direntry),
                                                        __func__);
      }

      if (dir_cxt->files) {
        struct dirlink *dlink = (struct dirlink *)dirbase.first;
        struct direntry *file = &dir_cxt->files[dir_cxt->nrfiles];
        while (dlink) {
          char fullname[PATH_MAX];
          memset(file, 0, sizeof(struct direntry));
          file->relname = dlink->name;
          file->path = lib_strdupcat(dirname, dlink->name);
          lib_join_dirfile(fullname, sizeof(fullname), dirname, dlink->name);
          if (lib_stat(fullname, &file->s) != -1) {
            file->type = file->s.st_mode;
          }
          else if (FILENAME_IS_CURRPAR(file->relname)) {
            /* Hack around for UNC paths on windows:
             * does not support stat on '\\SERVER\foo\..', sigh... */
            file->type |= S_IFDIR;
          }
          dir_cxt->nrfiles++;
          file++;
          dlink = dlink->next;
        }
      }
      else {
        printf("Couldn't get mem for dir\n");
        exit(1);
      }

      lib_freelist(&dirbase);
      if (dir_cxt->files) {
        qsort(dir_cxt->files,
              dir_cxt->nrfiles,
              sizeof(struct direntry),
              (int (*)(const void *, const void *))lib_compare);
      }
    }
    else {
      printf("%s empty dir\n", dirname);
    }

    closedir(dir);
  }
  else {
    printf("%s non-existent directory\n", dirname);
  }
}

unsigned int lib_filelist_dir_contents(const char *dirname, struct direntry **r_filelist)
{
  struct BuildDirCxt dir_cxt;

  dir_cxt.nrfiles = 0;
  dir_cxt.files = NULL;

  lib_builddir(&dir_cxt, dirname);

  if (dir_cxt.files) {
    *r_filelist = dir_cxt.files;
  }
  else {
    /* Keep Dune happy. Dune stores this in a var
     * where 0 has special meaning..... */
    *r_filelist = mem_mallocn(sizeof(**r_filelist), __func__);
  }

  return dir_cxt.nrfiles;
}

void lib_filelist_entry_size_to_string(const struct stat *st,
                                       const uint64_t sz,
                                       /* Used to change MB -> M, etc. - is that really useful? */
                                       const bool UNUSED(compact),
                                       char r_size[FILELIST_DIRENTRY_SIZE_LEN])
{
  /* Seems st_size is signed 32-bit val in *nix and Windows.  This
   * will buy us some time until files get bigger than 4GB or until
   * everyone starts using __USE_FILE_OFFSET64 or equivalent. */
  double size = (double)(st ? st->st_size : sz);
#ifdef WIN32
  lib_str_format_byte_unit(r_size, size, false);
#else
  lib_str_format_byte_unit(r_size, size, true);
#endif
}

void lib_filelist_entry_mode_to_string(const struct stat *st,
                                       const bool UNUSED(compact),
                                       char r_mode1[FILELIST_DIRENTRY_MODE_LEN],
                                       char r_mode2[FILELIST_DIRENTRY_MODE_LEN],
                                       char r_mode3[FILELIST_DIRENTRY_MODE_LEN])
{
  const char *types[8] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};

#ifdef WIN32
  lib_strncpy(r_mode1, types[0], sizeof(*r_mode1) * FILELIST_DIRENTRY_MODE_LEN);
  lib_strncpy(r_mode2, types[0], sizeof(*r_mode2) * FILELIST_DIRENTRY_MODE_LEN);
  lib_strncpy(r_mode3, types[0], sizeof(*r_mode3) * FILELIST_DIRENTRY_MODE_LEN);
#else
  const int mode = st->st_mode;

  lib_strncpy(r_mode1, types[(mode & 0700) >> 6], sizeof(*r_mode1) * FILELIST_DIRENTRY_MODE_LEN);
  lib_strncpy(r_mode2, types[(mode & 0070) >> 3], sizeof(*r_mode2) * FILELIST_DIRENTRY_MODE_LEN);
  lib_strncpy(r_mode3, types[(mode & 0007)], sizeof(*r_mode3) * FILELIST_DIRENTRY_MODE_LEN);

  if (((mode & S_ISGID) == S_ISGID) && (r_mode2[2] == '-')) {
    r_mode2[2] = 'l';
  }

  if (mode & (S_ISUID | S_ISGID)) {
    if (r_mode1[2] == 'x') {
      r_mode1[2] = 's';
    }
    else {
      r_mode1[2] = 'S';
    }

    if (r_mode2[2] == 'x') {
      r_mode2[2] = 's';
    }
  }

  if (mode & S_ISVTX) {
    if (r_mode3[2] == 'x') {
      r_mode3[2] = 't';
    }
    else {
      r_mode3[2] = 'T';
    }
  }
#endif
}

void lib_filelist_entry_owner_to_string(const struct stat *st,
                                        const bool UNUSED(compact),
                                        char r_owner[FILELIST_DIRENTRY_OWNER_LEN])
{
#ifdef WIN32
  strcpy(r_owner, "unknown");
#else
  struct passwd *pwuser = getpwuid(st->st_uid);

  if (pwuser) {
    lib_strncpy(r_owner, pwuser->pw_name, sizeof(*r_owner) * FILELIST_DIRENTRY_OWNER_LEN);
  }
  else {
    lib_snprintf(r_owner, sizeof(*r_owner) * FILELIST_DIRENTRY_OWNER_LEN, "%u", st->st_uid);
  }
#endif
}

void lib_filelist_entry_datetime_to_string(const struct stat *st,
                                           const int64_t ts,
                                           const bool compact,
                                           char r_time[FILELIST_DIRENTRY_TIME_LEN],
                                           char r_date[FILELIST_DIRENTRY_DATE_LEN],
                                           bool *r_is_today,
                                           bool *r_is_yesterday)
{
  int today_year = 0;
  int today_yday = 0;
  int yesterday_year = 0;
  int yesterday_yday = 0;

  if (r_is_today || r_is_yesterday) {
    /* `localtime()` has only one buf so need to get data out before called again. */
    const time_t ts_now = time(NULL);
    struct tm *today = localtime(&ts_now);

    today_year = today->tm_year;
    today_yday = today->tm_yday;
    /* Handle a yesterday that spans a year */
    today->tm_mday--;
    mktime(today);
    yesterday_year = today->tm_year;
    yesterday_yday = today->tm_yday;

    if (r_is_today) {
      *r_is_today = false;
    }
    if (r_is_yesterday) {
      *r_is_yesterday = false;
    }
  }

  const time_t ts_mtime = ts;
  const struct tm *tm = localtime(st ? &st->st_mtime : &ts_mtime);
  const time_t zero = 0;

  /* Prevent impossible dates in windows. */
  if (tm == NULL) {
    tm = localtime(&zero);
  }

  if (r_time) {
    strftime(r_time, sizeof(*r_time) * FILELIST_DIRENTRY_TIME_LEN, "%H:%M", tm);
  }

  if (r_date) {
    strftime(r_date,
             sizeof(*r_date) * FILELIST_DIRENTRY_DATE_LEN,
             compact ? "%d/%m/%y" : "%d %b %Y",
             tm);
  }

  if (r_is_today && (tm->tm_year == today_year) && (tm->tm_yday == today_yday)) {
    *r_is_today = true;
  }
  else if (r_is_yesterday && (tm->tm_year == yesterday_year) && (tm->tm_yday == yesterday_yday)) {
    *r_is_yesterday = true;
  }
}

void lib_filelist_entry_duplicate(struct direntry *dst, const struct direntry *src)
{
  *dst = *src;
  if (dst->relname) {
    dst->relname = mem_dupallocn(src->relname);
  }
  if (dst->path) {
    dst->path = mem_dupallocn(src->path);
  }
}

void lib_filelist_dup(struct direntry **dest_filelist,
                      struct direntry *const src_filelist,
                      const unsigned int nrentries)
{
  unsigned int i;

  *dest_filelist = mem_mallocn(sizeof(**dest_filelist) * (size_t)(nrentries), __func__);
  for (i = 0; i < nrentries; i++) {
    struct direntry *const src = &src_filelist[i];
    struct direntry *dst = &(*dest_filelist)[i];
    lib_filelist_entry_dup(dst, src);
  }
}

void lib_filelist_entry_free(struct direntry *entry)
{
  if (entry->relname) {
    mem_freen((void *)entry->relname);
  }
  if (entry->path) {
    mem_freen((void *)entry->path);
  }
}

void lib_filelist_free(struct direntry *filelist, const unsigned int nrentries)
{
  unsigned int i;
  for (i = 0; i < nrentries; i++) {
    lib_filelist_entry_free(&filelist[i]);
  }

  if (filelist != NULL) {
    mem_freen(filelist);
  }
}
