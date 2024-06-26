/* Jitter offset table */
#include "mem_guardedalloc.h"
#include <math.h>
#include <string.h>

#include "lib_jitter_2d.h"
#include "lib_rand.h"

#include "lib_strict_flags.h"

void lib_jitter1(float (*jit1)[2], float (*jit2)[2], int num, float radius1)
{
  int i, j, k;
  float vecx, vecy, dvecx, dvecy, x, y, len;

  for (i = num - 1; i >= 0; i--) {
    dvecx = dvecy = 0.0;
    x = jit1[i][0];
    y = jit1[i][1];
    for (j = num - 1; j >= 0; j--) {
      if (i != j) {
        vecx = jit1[j][0] - x - 1.0f;
        vecy = jit1[j][1] - y - 1.0f;
        for (k = 3; k > 0; k--) {
          if (fabsf(vecx) < radius1 && fabsf(vecy) < radius1) {
            len = sqrtf(vecx * vecx + vecy * vecy);
            if (len > 0 && len < radius1) {
              len = len / radius1;
              dvecx += vecx / len;
              dvecy += vecy / len;
            }
          }
          vecx += 1.0f;

          if (fabsf(vecx) < radius1 && fabsf(vecy) < radius1) {
            len = sqrtf(vecx * vecx + vecy * vecy);
            if (len > 0 && len < radius1) {
              len = len / radius1;
              dvecx += vecx / len;
              dvecy += vecy / len;
            }
          }
          vecx += 1.0f;

          if (fabsf(vecx) < radius1 && fabsf(vecy) < radius1) {
            len = sqrtf(vecx * vecx + vecy * vecy);
            if (len > 0 && len < radius1) {
              len = len / radius1;
              dvecx += vecx / len;
              dvecy += vecy / len;
            }
          }
          vecx -= 2.0f;
          vecy += 1.0f;
        }
      }
    }

    x -= dvecx / 18.0f;
    y -= dvecy / 18.0f;
    x -= floorf(x);
    y -= floorf(y);
    jit2[i][0] = x;
    jit2[i][1] = y;
  }
  memcpy(jit1, jit2, 2 * (uint)num * sizeof(float));
}

void lib_jitter2(float (*jit1)[2], float (*jit2)[2], int num, float radius2)
{
  int i, j;
  float vecx, vecy, dvecx, dvecy, x, y;

  for (i = num - 1; i >= 0; i--) {
    dvecx = dvecy = 0.0;
    x = jit1[i][0];
    y = jit1[i][1];
    for (j = num - 1; j >= 0; j--) {
      if (i != j) {
        vecx = jit1[j][0] - x - 1.0f;
        vecy = jit1[j][1] - y - 1.0f;

        if (fabsf(vecx) < radius2) {
          dvecx += vecx * radius2;
        }
        vecx += 1.0f;
        if (fabsf(vecx) < radius2) {
          dvecx += vecx * radius2;
        }
        vecx += 1.0f;
        if (fabsf(vecx) < radius2) {
          dvecx += vecx * radius2;
        }

        if (fabsf(vecy) < radius2) {
          dvecy += vecy * radius2;
        }
        vecy += 1.0f;
        if (fabsf(vecy) < radius2) {
          dvecy += vecy * radius2;
        }
        vecy += 1.0f;
        if (fabsf(vecy) < radius2) {
          dvecy += vecy * radius2;
        }
      }
    }

    x -= dvecx / 2.0f;
    y -= dvecy / 2.0f;
    x -= floorf(x);
    y -= floorf(y);
    jit2[i][0] = x;
    jit2[i][1] = y;
  }
  memcpy(jit1, jit2, (uint)num * sizeof(float[2]));
}

void lib_jitter_init(float (*jitarr)[2], int num)
{
  float(*jit2)[2];
  float num_fl, num_fl_sqrt;
  float x, rad1, rad2, rad3;
  RNG *rng;
  int i;

  if (num == 0) {
    return;
  }

  num_fl = (float)num;
  num_fl_sqrt = sqrtf(num_fl);

  jit2 = mem_malloc(12 + (uint)num * sizeof(float[2]), "initjit");
  rad1 = 1.0f / num_fl_sqrt;
  rad2 = 1.0f / num_fl;
  rad3 = num_fl_sqrt / num_fl;

  rng = lib_rng_new(31415926 + (uint)num);

  x = 0;
  for (i = 0; i < num; i++) {
    jitarr[i][0] = x + rad1 * (float)(0.5 - lib_rng_get_double(rng));
    jitarr[i][1] = (float)i / num_fl + rad1 * (float)(0.5 - lib_rng_get_double(rng));
    x += rad3;
    x -= floorf(x);
  }

  lib_rng_free(rng);

  for (i = 0; i < 24; i++) {
    lib_jitter1(jitarr, jit2, num, rad1);
    lib_jitter1(jitarr, jit2, num, rad1);
    lib_jitter2(jitarr, jit2, num, rad2);
  }

  mem_free(jit2);

  /* Finally, move jitter to be centered around (0, 0). */
  for (i = 0; i < num; i++) {
    jitarr[i][0] -= 0.5f;
    jitarr[i][1] -= 0.5f;
  }
}
