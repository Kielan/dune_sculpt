#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PackedFile {
  int size;
  int seek;
  void *data;
} PackedFile;

#ifdef __cplusplus
}
#endif
