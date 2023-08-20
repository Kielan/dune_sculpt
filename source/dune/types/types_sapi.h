#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct MemArena;

#
#
typedef struct TYPES_StructMember {
  /** This struct must not change, it's only a convenience view for raw data stored in SDNA. */

  /** An index into STYPES->types. */
  short type;
  /** An index into STYPES->names. */
  short name;
} TYPES_StructMember;

#
#
typedef struct TYPES_Struct {
  /** This struct must not change, it's only a convenience view for raw data stored in SDNA. */

  /** An index into TYPES->types. */
  short type;
  /** The amount of members in this struct. */
  short members_len;
  /** "Flexible array member" that contains information about all members of this struct. */
  TYPES_StructMember members[];
} TYPES_Struct;

#
#
typedef struct TYPES {
  /** Full copy of 'encoded' data (when data_alloc is set, otherwise borrowed). */
  const char *data;
  /** Length of data. */
  int data_len;
  bool data_alloc;

  /** Total number of struct members. */
  int names_len, names_len_alloc;
  /** Struct member names. */
  const char **names;
  /** Result of #types_elem_array_size (aligned with #names). */
  short *names_array_len;

  /** Size of a ptr in bytes. */
  int ptr_size;

  /** Type names. */
  const char **types;
  /** Number of basic types + struct types. */
  int types_len;

  /** Type lengths. */
  short *types_size;

  /** Information about structs and their members. */
  SDNA_Struct **structs;
  /** Number of struct types. */
  int structs_len;

  /** #GHash for faster lookups, requires WITH_DNA_GHASH to be used for now. */
  struct GHash *structs_map;

  /** Temporary memory currently only used for version patching DNA. */
  struct MemArena *mem_arena;
  /** Runtime versions of data stored in DNA, lazy initialized,
   * only different when renaming is done. */
  struct {
    /** Aligned with #SDNA.names, same pointers when unchanged. */
    const char **names;
    /** Aligned with #SDNA.types, same pointers when unchanged. */
    const char **types;
    /** A version of #SDNA.structs_map that uses #SDNA.alias.types for its keys. */
    struct GHash *structs_map;
  } alias;
} TYPES;

#
#
typedef struct BHead {
  int code, len;
  const void *old;
  int TYPESnr, nr;
} BHead;
#
#
typedef struct BHead4 {
  int code, len;
  uint old;
  int TYPESnr, nr;
} BHead4;
#
#
typedef struct BHead8 {
  int code, len;
  uint64_t old;
  int SDNAnr, nr;
} BHead8;

#ifdef __cplusplus
}
#endif
