#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MemArena;

#
#
typedef struct TypesStructMember {
  /* This struct must not change, it's only a convenience view for raw data stored in Types structs. */

  /* An index into Types->types. */
  short type;
  /* An index into Types->names. */
  short name;
} TypesStructMember;

#
#
typedef TypesStruct {
  /* This struct must not change, it's only a convenience view for raw data stored in Types structs. */

  /* An index into TYPES->types. */
  short type;
  /* The amount of members in this struct. */
  short members_len;
  /* "Flexible array member" that contains info about all members of this struct. */
  TypesStructMember members[];
} TypesStruct;

#
#
typedef struct Types {
  /* Full copy of 'encoded' data (when data_alloc is set, otherwise borrowed). */
  const char *data;
  /* Length of data. */
  int data_len;
  bool data_alloc;

  /* Total num of struct members. */
  int names_len, names_len_alloc;
  /* Struct member names. */
  const char **names;
  /* Result of types_elem_array_size (aligned with #names). */
  short *names_array_len;

  /* Size of a ptr in bytes. */
  int ptr_size;

  /* Type names. */
  const char **types;
  /* Num of basic types + struct types. */
  int types_len;

  /* Type lengths. */
  short *types_size;

  /* Info about structs and their members. */
  TypesStruct **structs;
  /* Num of struct types. */
  int structs_len;

  /* GHash for faster lookups, requires WITH_TYPES_GHASH to be used for now. */
  struct GHash *structs_map;

  /* Tmp mem currently only used for version patching DNA. */
  struct MemArena *mem_arena;
  /* Runtime versions of data stored in Types, lazy init,
   * only diff when renaming is done. */
  struct {
    /* Aligned w Types.names, same ptrs when unchanged. */
    const char **names;
    /* Aligned w Types.types, same ptr when unchanged. */
    const char **types;
    /* A version of Types.structs_map that uses Types.alias.types for its keys. */
    struct GHash *structs_map;
  } alias;
} Types;

#
#
typedef struct BHead {
  int code, len;
  const void *old;
  int Typesnr, nr;
} BHead;
#
#
typedef struct BHead4 {
  int code, len;
  uint old;
  int Typesnr, nr;
} BHead4;
#
#
typedef struct BHead8 {
  int code, len;
  uint64_t old;
  int Typesnr, nr;
} BHead8;

#ifdef __cplusplus
}
#endif
