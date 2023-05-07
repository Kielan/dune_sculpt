/* These structs are the foundation for all linked lists in the library system.
 * Doubly-linked lists start from a ListBase and contain elements beginning
 * with Link. */

#pragma once

/* Generic - all structs which are put into linked lists begin with this. */
typedef struct Link {
  struct Link *next, *prev;
} Link;

/* Simple subclass of Link. Use this when it is not worth defining a custom one. */
typedef struct LinkData {
  struct LinkData *next, *prev;
  void *data;
} LinkData;

/* Never change the size of this! structs_genfile.c detects pointer_size with it. */
typedef struct List {
  void *first, *last;
} List;

/* 8 byte alignment! */
