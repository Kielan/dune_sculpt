#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_endian_defines.h"
#include "BLI_endian_switch.h"
#include "BLI_fileops.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_customdata_file.h"

/************************* File Format Definitions ***************************/

#define CDF_ENDIAN_LITTLE 0
#define CDF_ENDIAN_BIG 1

#define CDF_DATA_FLOAT 0

typedef struct CDataFileHeader {
  char ID[4];      /* "BCDF" */
  char endian;     /* little, big */
  char version;    /* non-compatible versions */
  char subversion; /* compatible sub versions */
  char pad;        /* padding */

  int structbytes; /* size of this struct in bytes */
  int type;        /* image, mesh */
  int totlayer;    /* number of layers in the file */
} CDataFileHeader;

typedef struct CDataFileImageHeader {
  int structbytes; /* size of this struct in bytes */
  int width;       /* image width */
  int height;      /* image height */
  int tile_size;   /* tile size (required power of 2) */
} CDataFileImageHeader;

typedef struct CDataFileMeshHeader {
  int structbytes; /* size of this struct in bytes */
} CDataFileMeshHeader;

struct CDataFileLayer {
  int structbytes;               /* size of this struct in bytes */
  int datatype;                  /* only float for now */
  uint64_t datasize;             /* size of data in layer */
  int type;                      /* layer type */
  char name[CDF_LAYER_NAME_MAX]; /* layer name */
};

/**************************** Other Definitions ******************************/

#define CDF_VERSION 0
#define CDF_SUBVERSION 0
#define CDF_TILE_SIZE 64

struct CDataFile {
  int type;

  CDataFileHeader header;
  union {
    CDataFileImageHeader image;
    CDataFileMeshHeader mesh;
  } btype;

  CDataFileLayer *layer;
  int totlayer;

  FILE *readf;
  FILE *writef;
  int switchendian;
  size_t dataoffset;
};

/********************************* Create/Free *******************************/

static int cdf_endian(void)
{
  if (ENDIAN_ORDER == L_ENDIAN) {
    return CDF_ENDIAN_LITTLE;
  }

  return CDF_ENDIAN_BIG;
}

CDataFile *cdf_create(int type)
{
  CDataFile *cdf = MEM_callocN(sizeof(CDataFile), "CDataFile");

  cdf->type = type;

  return cdf;
}

void cdf_free(CDataFile *cdf)
{
  cdf_read_close(cdf);
  cdf_write_close(cdf);

  if (cdf->layer) {
    MEM_freeN(cdf->layer);
  }

  MEM_freeN(cdf);
}

/********************************* Read/Write ********************************/

static bool cdf_read_header(CDataFile *cdf)
{
  CDataFileHeader *header;
  CDataFileImageHeader *image;
  CDataFileMeshHeader *mesh;
  CDataFileLayer *layer;
  FILE *f = cdf->readf;
  size_t offset = 0;
  int a;

  header = &cdf->header;

  if (!fread(header, sizeof(CDataFileHeader), 1, cdf->readf)) {
    return false;
  }

  if (memcmp(header->ID, "BCDF", sizeof(header->ID)) != 0) {
    return false;
  }
  if (header->version > CDF_VERSION) {
    return false;
  }

  cdf->switchendian = header->endian != cdf_endian();
  header->endian = cdf_endian();

  if (cdf->switchendian) {
    BLI_endian_switch_int32(&header->type);
    BLI_endian_switch_int32(&header->totlayer);
    BLI_endian_switch_int32(&header->structbytes);
  }

  if (!ELEM(header->type, CDF_TYPE_IMAGE, CDF_TYPE_MESH)) {
    return false;
  }

  offset += header->structbytes;
  header->structbytes = sizeof(CDataFileHeader);

  if (BLI_fseek(f, offset, SEEK_SET) != 0) {
    return false;
  }

  if (header->type == CDF_TYPE_IMAGE) {
    image = &cdf->btype.image;
    if (!fread(image, sizeof(CDataFileImageHeader), 1, f)) {
      return false;
    }

    if (cdf->switchendian) {
      BLI_endian_switch_int32(&image->width);
      BLI_endian_switch_int32(&image->height);
      BLI_endian_switch_int32(&image->tile_size);
      BLI_endian_switch_int32(&image->structbytes);
    }

    offset += image->structbytes;
    image->structbytes = sizeof(CDataFileImageHeader);
  }
  else if (header->type == CDF_TYPE_MESH) {
    mesh = &cdf->btype.mesh;
    if (!fread(mesh, sizeof(CDataFileMeshHeader), 1, f)) {
      return false;
    }

    if (cdf->switchendian) {
      BLI_endian_switch_int32(&mesh->structbytes);
    }

    offset += mesh->structbytes;
    mesh->structbytes = sizeof(CDataFileMeshHeader);
  }

  if (BLI_fseek(f, offset, SEEK_SET) != 0) {
    return false;
  }

  cdf->layer = MEM_calloc_arrayN(header->totlayer, sizeof(CDataFileLayer), "CDataFileLayer");
  cdf->totlayer = header->totlayer;

  if (!cdf->layer) {
    return false;
  }

  for (a = 0; a < header->totlayer; a++) {
    layer = &cdf->layer[a];

    if (!fread(layer, sizeof(CDataFileLayer), 1, f)) {
      return false;
    }

    if (cdf->switchendian) {
      BLI_endian_switch_int32(&layer->type);
      BLI_endian_switch_int32(&layer->datatype);
      BLI_endian_switch_uint64(&layer->datasize);
      BLI_endian_switch_int32(&layer->structbytes);
    }

    if (layer->datatype != CDF_DATA_FLOAT) {
      return false;
    }

    offset += layer->structbytes;
    layer->structbytes = sizeof(CDataFileLayer);

    if (BLI_fseek(f, offset, SEEK_SET) != 0) {
      return false;
    }
  }

  cdf->dataoffset = offset;

  return true;
}

static bool cdf_write_header(CDataFile *cdf)
{
  CDataFileHeader *header;
  CDataFileImageHeader *image;
  CDataFileMeshHeader *mesh;
  CDataFileLayer *layer;
  FILE *f = cdf->writef;
  int a;

  header = &cdf->header;

  if (!fwrite(header, sizeof(CDataFileHeader), 1, f)) {
    return false;
  }

  if (header->type == CDF_TYPE_IMAGE) {
    image = &cdf->btype.image;
    if (!fwrite(image, sizeof(CDataFileImageHeader), 1, f)) {
      return false;
    }
  }
  else if (header->type == CDF_TYPE_MESH) {
    mesh = &cdf->btype.mesh;
    if (!fwrite(mesh, sizeof(CDataFileMeshHeader), 1, f)) {
      return false;
    }
  }

  for (a = 0; a < header->totlayer; a++) {
    layer = &cdf->layer[a];

    if (!fwrite(layer, sizeof(CDataFileLayer), 1, f)) {
      return false;
    }
  }

  return true;
}
