#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BLI_math.h"
#include "BLI_noise.h"
#include "BLI_rand.h"
#include "BLI_utildefines.h"

#include "DNA_anim_types.h"
#include "DNA_image_types.h"
#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_texture_types.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf_types.h"

#include "BKE_colorband.h"
#include "BKE_image.h"
#include "BKE_material.h"
#include "BKE_node.h"
#include "BKE_scene.h"
#include "BKE_texture.h"

#include "NOD_texture.h"

#include "MEM_guardedalloc.h"

#include "render_types.h"
#include "texture_common.h"

#include "RE_texture.h"

static RNG_THREAD_ARRAY *random_tex_array;

void RE_texture_rng_init(void)
{
  random_tex_array = BLI_rng_threaded_new();
}

void RE_texture_rng_exit(void)
{
  if (random_tex_array == NULL) {
    return;
  }
  BLI_rng_threaded_free(random_tex_array);
  random_tex_array = NULL;
}

/* ------------------------------------------------------------------------- */

/* This allows color-banded textures to control normals as well. */
static void tex_normal_derivate(const Tex *tex, TexResult *texres)
{
  if (tex->flag & TEX_COLORBAND) {
    float col[4];
    if (BKE_colorband_evaluate(tex->coba, texres->tin, col)) {
      float fac0, fac1, fac2, fac3;

      fac0 = (col[0] + col[1] + col[2]);
      BKE_colorband_evaluate(tex->coba, texres->nor[0], col);
      fac1 = (col[0] + col[1] + col[2]);
      BKE_colorband_evaluate(tex->coba, texres->nor[1], col);
      fac2 = (col[0] + col[1] + col[2]);
      BKE_colorband_evaluate(tex->coba, texres->nor[2], col);
      fac3 = (col[0] + col[1] + col[2]);

      texres->nor[0] = (fac0 - fac1) / 3.0f;
      texres->nor[1] = (fac0 - fac2) / 3.0f;
      texres->nor[2] = (fac0 - fac3) / 3.0f;

      return;
    }
  }
  texres->nor[0] = texres->tin - texres->nor[0];
  texres->nor[1] = texres->tin - texres->nor[1];
  texres->nor[2] = texres->tin - texres->nor[2];
}

static int blend(const Tex *tex, const float texvec[3], TexResult *texres)
{
  float x, y, t;

  if (tex->flag & TEX_FLIPBLEND) {
    x = texvec[1];
    y = texvec[0];
  }
  else {
    x = texvec[0];
    y = texvec[1];
  }

  if (tex->stype == TEX_LIN) { /* Linear. */
    texres->tin = (1.0f + x) / 2.0f;
  }
  else if (tex->stype == TEX_QUAD) { /* Quadratic. */
    texres->tin = (1.0f + x) / 2.0f;
    if (texres->tin < 0.0f) {
      texres->tin = 0.0f;
    }
    else {
      texres->tin *= texres->tin;
    }
  }
  else if (tex->stype == TEX_EASE) { /* Ease. */
    texres->tin = (1.0f + x) / 2.0f;
    if (texres->tin <= 0.0f) {
      texres->tin = 0.0f;
    }
    else if (texres->tin >= 1.0f) {
      texres->tin = 1.0f;
    }
    else {
      t = texres->tin * texres->tin;
      texres->tin = (3.0f * t - 2.0f * t * texres->tin);
    }
  }
  else if (tex->stype == TEX_DIAG) { /* Diagonal. */
    texres->tin = (2.0f + x + y) / 4.0f;
  }
  else if (tex->stype == TEX_RAD) { /* Radial. */
    texres->tin = (atan2f(y, x) / (float)(2 * M_PI) + 0.5f);
  }
  else { /* sphere TEX_SPHERE */
    texres->tin = 1.0f - sqrtf(x * x + y * y + texvec[2] * texvec[2]);
    if (texres->tin < 0.0f) {
      texres->tin = 0.0f;
    }
    if (tex->stype == TEX_HALO) {
      texres->tin *= texres->tin; /* Halo. */
    }
  }

  BRICONT;

  return TEX_INT;
}

/* ------------------------------------------------------------------------- */
/* ************************************************************************* */

/* newnoise: all noise-based types now have different noise-bases to choose from. */

static int clouds(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;

  texres->tin = BLI_noise_generic_turbulence(tex->noisesize,
                                             texvec[0],
                                             texvec[1],
                                             texvec[2],
                                             tex->noisedepth,
                                             (tex->noisetype != TEX_NOISESOFT),
                                             tex->noisebasis);

  if (texres->nor != NULL) {
    /* calculate bumpnormal */
    texres->nor[0] = BLI_noise_generic_turbulence(tex->noisesize,
                                                  texvec[0] + tex->nabla,
                                                  texvec[1],
                                                  texvec[2],
                                                  tex->noisedepth,
                                                  (tex->noisetype != TEX_NOISESOFT),
                                                  tex->noisebasis);
    texres->nor[1] = BLI_noise_generic_turbulence(tex->noisesize,
                                                  texvec[0],
                                                  texvec[1] + tex->nabla,
                                                  texvec[2],
                                                  tex->noisedepth,
                                                  (tex->noisetype != TEX_NOISESOFT),
                                                  tex->noisebasis);
    texres->nor[2] = BLI_noise_generic_turbulence(tex->noisesize,
                                                  texvec[0],
                                                  texvec[1],
                                                  texvec[2] + tex->nabla,
                                                  tex->noisedepth,
                                                  (tex->noisetype != TEX_NOISESOFT),
                                                  tex->noisebasis);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  if (tex->stype == TEX_COLOR) {
    /* in this case, int. value should really be computed from color,
     * and bumpnormal from that, would be too slow, looks ok as is */
    texres->trgba[0] = texres->tin;
    texres->trgba[1] = BLI_noise_generic_turbulence(tex->noisesize,
                                                    texvec[1],
                                                    texvec[0],
                                                    texvec[2],
                                                    tex->noisedepth,
                                                    (tex->noisetype != TEX_NOISESOFT),
                                                    tex->noisebasis);
    texres->trgba[2] = BLI_noise_generic_turbulence(tex->noisesize,
                                                    texvec[1],
                                                    texvec[2],
                                                    texvec[0],
                                                    tex->noisedepth,
                                                    (tex->noisetype != TEX_NOISESOFT),
                                                    tex->noisebasis);
    BRICONTRGB;
    texres->trgba[3] = 1.0;
    return (rv | TEX_RGB);
  }

  BRICONT;

  return rv;
}

/* creates a sine wave */
static float tex_sin(float a)
{
  a = 0.5f + 0.5f * sinf(a);

  return a;
}

/* creates a saw wave */
static float tex_saw(float a)
{
  const float b = 2 * M_PI;

  int n = (int)(a / b);
  a -= n * b;
  if (a < 0) {
    a += b;
  }
  return a / b;
}

/* creates a triangle wave */
static float tex_tri(float a)
{
  const float b = 2 * M_PI;
  const float rmax = 1.0;

  a = rmax - 2.0f * fabsf(floorf((a * (1.0f / b)) + 0.5f) - (a * (1.0f / b)));

  return a;
}

/* computes basic wood intensity value at x,y,z */
static float wood_int(const Tex *tex, float x, float y, float z)
{
  float wi = 0;
  /* wave form:   TEX_SIN=0,  TEX_SAW=1,  TEX_TRI=2 */
  short wf = tex->noisebasis2;
  /* wood type:   TEX_BAND=0, TEX_RING=1, TEX_BANDNOISE=2, TEX_RINGNOISE=3 */
  short wt = tex->stype;

  float (*waveform[3])(float); /* create array of pointers to waveform functions */
  waveform[0] = tex_sin;       /* assign address of tex_sin() function to pointer array */
  waveform[1] = tex_saw;
  waveform[2] = tex_tri;

  if ((wf > TEX_TRI) || (wf < TEX_SIN)) {
    wf = 0; /* check to be sure noisebasis2 is initialized ahead of time */
  }

  if (wt == TEX_BAND) {
    wi = waveform[wf]((x + y + z) * 10.0f);
  }
  else if (wt == TEX_RING) {
    wi = waveform[wf](sqrtf(x * x + y * y + z * z) * 20.0f);
  }
  else if (wt == TEX_BANDNOISE) {
    wi = tex->turbul *
         BLI_noise_generic_noise(
             tex->noisesize, x, y, z, (tex->noisetype != TEX_NOISESOFT), tex->noisebasis);
    wi = waveform[wf]((x + y + z) * 10.0f + wi);
  }
  else if (wt == TEX_RINGNOISE) {
    wi = tex->turbul *
         BLI_noise_generic_noise(
             tex->noisesize, x, y, z, (tex->noisetype != TEX_NOISESOFT), tex->noisebasis);
    wi = waveform[wf](sqrtf(x * x + y * y + z * z) * 20.0f + wi);
  }

  return wi;
}

static int wood(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;

  texres->tin = wood_int(tex, texvec[0], texvec[1], texvec[2]);
  if (texres->nor != NULL) {
    /* calculate bumpnormal */
    texres->nor[0] = wood_int(tex, texvec[0] + tex->nabla, texvec[1], texvec[2]);
    texres->nor[1] = wood_int(tex, texvec[0], texvec[1] + tex->nabla, texvec[2]);
    texres->nor[2] = wood_int(tex, texvec[0], texvec[1], texvec[2] + tex->nabla);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}

/* computes basic marble intensity at x,y,z */
static float marble_int(const Tex *tex, float x, float y, float z)
{
  float n, mi;
  short wf = tex->noisebasis2; /* wave form:   TEX_SIN=0, TEX_SAW=1, TEX_TRI=2 */
  short mt = tex->stype;       /* marble type: TEX_SOFT=0, TEX_SHARP=1, TEX_SHAPER=2 */

  float (*waveform[3])(float); /* create array of pointers to waveform functions */
  waveform[0] = tex_sin;       /* assign address of tex_sin() function to pointer array */
  waveform[1] = tex_saw;
  waveform[2] = tex_tri;

  if ((wf > TEX_TRI) || (wf < TEX_SIN)) {
    wf = 0; /* check to be sure noisebasis2 isn't initialized ahead of time */
  }

  n = 5.0f * (x + y + z);

  mi = n + tex->turbul * BLI_noise_generic_turbulence(tex->noisesize,
                                                      x,
                                                      y,
                                                      z,
                                                      tex->noisedepth,
                                                      (tex->noisetype != TEX_NOISESOFT),
                                                      tex->noisebasis);

  if (mt >= TEX_SOFT) { /* TEX_SOFT always true */
    mi = waveform[wf](mi);
    if (mt == TEX_SHARP) {
      mi = sqrtf(mi);
    }
    else if (mt == TEX_SHARPER) {
      mi = sqrtf(sqrtf(mi));
    }
  }

  return mi;
}

static int marble(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;

  texres->tin = marble_int(tex, texvec[0], texvec[1], texvec[2]);

  if (texres->nor != NULL) {
    /* calculate bumpnormal */
    texres->nor[0] = marble_int(tex, texvec[0] + tex->nabla, texvec[1], texvec[2]);
    texres->nor[1] = marble_int(tex, texvec[0], texvec[1] + tex->nabla, texvec[2]);
    texres->nor[2] = marble_int(tex, texvec[0], texvec[1], texvec[2] + tex->nabla);

    tex_normal_derivate(tex, texres);

    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}

/* ------------------------------------------------------------------------- */

static int magic(const Tex *tex, const float texvec[3], TexResult *texres)
{
  float x, y, z, turb;
  int n;

  n = tex->noisedepth;
  turb = tex->turbul / 5.0f;

  x = sinf((texvec[0] + texvec[1] + texvec[2]) * 5.0f);
  y = cosf((-texvec[0] + texvec[1] - texvec[2]) * 5.0f);
  z = -cosf((-texvec[0] - texvec[1] + texvec[2]) * 5.0f);
  if (n > 0) {
    x *= turb;
    y *= turb;
    z *= turb;
    y = -cosf(x - y + z);
    y *= turb;
    if (n > 1) {
      x = cosf(x - y - z);
      x *= turb;
      if (n > 2) {
        z = sinf(-x - y - z);
        z *= turb;
        if (n > 3) {
          x = -cosf(-x + y - z);
          x *= turb;
          if (n > 4) {
            y = -sinf(-x + y + z);
            y *= turb;
            if (n > 5) {
              y = -cosf(-x + y + z);
              y *= turb;
              if (n > 6) {
                x = cosf(x + y + z);
                x *= turb;
                if (n > 7) {
                  z = sinf(x + y - z);
                  z *= turb;
                  if (n > 8) {
                    x = -cosf(-x - y + z);
                    x *= turb;
                    if (n > 9) {
                      y = -sinf(x - y + z);
                      y *= turb;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  if (turb != 0.0f) {
    turb *= 2.0f;
    x /= turb;
    y /= turb;
    z /= turb;
  }
  texres->trgba[0] = 0.5f - x;
  texres->trgba[1] = 0.5f - y;
  texres->trgba[2] = 0.5f - z;

  texres->tin = (1.0f / 3.0f) * (texres->trgba[0] + texres->trgba[1] + texres->trgba[2]);

  BRICONTRGB;
  texres->trgba[3] = 1.0f;

  return TEX_RGB;
}

/* ------------------------------------------------------------------------- */

/* newnoise: stucci also modified to use different noisebasis */
static int stucci(const Tex *tex, const float texvec[3], TexResult *texres)
{
  float nor[3], b2, ofs;
  int retval = TEX_INT;

  b2 = BLI_noise_generic_noise(tex->noisesize,
                               texvec[0],
                               texvec[1],
                               texvec[2],
                               (tex->noisetype != TEX_NOISESOFT),
                               tex->noisebasis);

  ofs = tex->turbul / 200.0f;

  if (tex->stype) {
    ofs *= (b2 * b2);
  }
  nor[0] = BLI_noise_generic_noise(tex->noisesize,
                                   texvec[0] + ofs,
                                   texvec[1],
                                   texvec[2],
                                   (tex->noisetype != TEX_NOISESOFT),
                                   tex->noisebasis);
  nor[1] = BLI_noise_generic_noise(tex->noisesize,
                                   texvec[0],
                                   texvec[1] + ofs,
                                   texvec[2],
                                   (tex->noisetype != TEX_NOISESOFT),
                                   tex->noisebasis);
  nor[2] = BLI_noise_generic_noise(tex->noisesize,
                                   texvec[0],
                                   texvec[1],
                                   texvec[2] + ofs,
                                   (tex->noisetype != TEX_NOISESOFT),
                                   tex->noisebasis);

  texres->tin = nor[2];

  if (texres->nor) {

    copy_v3_v3(texres->nor, nor);
    tex_normal_derivate(tex, texres);

    if (tex->stype == TEX_WALLOUT) {
      texres->nor[0] = -texres->nor[0];
      texres->nor[1] = -texres->nor[1];
      texres->nor[2] = -texres->nor[2];
    }

    retval |= TEX_NOR;
  }

  if (tex->stype == TEX_WALLOUT) {
    texres->tin = 1.0f - texres->tin;
  }

  if (texres->tin < 0.0f) {
    texres->tin = 0.0f;
  }

  return retval;
}

/* ------------------------------------------------------------------------- */
/* newnoise: musgrave terrain noise types */

static int mg_mFractalOrfBmTex(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;
  float (*mgravefunc)(float, float, float, float, float, float, int);

  if (tex->stype == TEX_MFRACTAL) {
    mgravefunc = BLI_noise_mg_multi_fractal;
  }
  else {
    mgravefunc = BLI_noise_mg_fbm;
  }

  texres->tin = tex->ns_outscale * mgravefunc(texvec[0],
                                              texvec[1],
                                              texvec[2],
                                              tex->mg_H,
                                              tex->mg_lacunarity,
                                              tex->mg_octaves,
                                              tex->noisebasis);

  if (texres->nor != NULL) {
    float ofs = tex->nabla / tex->noisesize; /* also scaling of texvec */

    /* calculate bumpnormal */
    texres->nor[0] = tex->ns_outscale * mgravefunc(texvec[0] + ofs,
                                                   texvec[1],
                                                   texvec[2],
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->noisebasis);
    texres->nor[1] = tex->ns_outscale * mgravefunc(texvec[0],
                                                   texvec[1] + ofs,
                                                   texvec[2],
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->noisebasis);
    texres->nor[2] = tex->ns_outscale * mgravefunc(texvec[0],
                                                   texvec[1],
                                                   texvec[2] + ofs,
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->noisebasis);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}

static int mg_ridgedOrHybridMFTex(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;
  float (*mgravefunc)(float, float, float, float, float, float, float, float, int);

  if (tex->stype == TEX_RIDGEDMF) {
    mgravefunc = BLI_noise_mg_ridged_multi_fractal;
  }
  else {
    mgravefunc = BLI_noise_mg_hybrid_multi_fractal;
  }

  texres->tin = tex->ns_outscale * mgravefunc(texvec[0],
                                              texvec[1],
                                              texvec[2],
                                              tex->mg_H,
                                              tex->mg_lacunarity,
                                              tex->mg_octaves,
                                              tex->mg_offset,
                                              tex->mg_gain,
                                              tex->noisebasis);

  if (texres->nor != NULL) {
    float ofs = tex->nabla / tex->noisesize; /* also scaling of texvec */

    /* calculate bumpnormal */
    texres->nor[0] = tex->ns_outscale * mgravefunc(texvec[0] + ofs,
                                                   texvec[1],
                                                   texvec[2],
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->mg_offset,
                                                   tex->mg_gain,
                                                   tex->noisebasis);
    texres->nor[1] = tex->ns_outscale * mgravefunc(texvec[0],
                                                   texvec[1] + ofs,
                                                   texvec[2],
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->mg_offset,
                                                   tex->mg_gain,
                                                   tex->noisebasis);
    texres->nor[2] = tex->ns_outscale * mgravefunc(texvec[0],
                                                   texvec[1],
                                                   texvec[2] + ofs,
                                                   tex->mg_H,
                                                   tex->mg_lacunarity,
                                                   tex->mg_octaves,
                                                   tex->mg_offset,
                                                   tex->mg_gain,
                                                   tex->noisebasis);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}

static int mg_HTerrainTex(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;

  texres->tin = tex->ns_outscale * BLI_noise_mg_hetero_terrain(texvec[0],
                                                               texvec[1],
                                                               texvec[2],
                                                               tex->mg_H,
                                                               tex->mg_lacunarity,
                                                               tex->mg_octaves,
                                                               tex->mg_offset,
                                                               tex->noisebasis);

  if (texres->nor != NULL) {
    float ofs = tex->nabla / tex->noisesize; /* also scaling of texvec */

    /* calculate bumpnormal */
    texres->nor[0] = tex->ns_outscale * BLI_noise_mg_hetero_terrain(texvec[0] + ofs,
                                                                    texvec[1],
                                                                    texvec[2],
                                                                    tex->mg_H,
                                                                    tex->mg_lacunarity,
                                                                    tex->mg_octaves,
                                                                    tex->mg_offset,
                                                                    tex->noisebasis);
    texres->nor[1] = tex->ns_outscale * BLI_noise_mg_hetero_terrain(texvec[0],
                                                                    texvec[1] + ofs,
                                                                    texvec[2],
                                                                    tex->mg_H,
                                                                    tex->mg_lacunarity,
                                                                    tex->mg_octaves,
                                                                    tex->mg_offset,
                                                                    tex->noisebasis);
    texres->nor[2] = tex->ns_outscale * BLI_noise_mg_hetero_terrain(texvec[0],
                                                                    texvec[1],
                                                                    texvec[2] + ofs,
                                                                    tex->mg_H,
                                                                    tex->mg_lacunarity,
                                                                    tex->mg_octaves,
                                                                    tex->mg_offset,
                                                                    tex->noisebasis);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}

static int mg_distNoiseTex(const Tex *tex, const float texvec[3], TexResult *texres)
{
  int rv = TEX_INT;

  texres->tin = BLI_noise_mg_variable_lacunarity(
      texvec[0], texvec[1], texvec[2], tex->dist_amount, tex->noisebasis, tex->noisebasis2);

  if (texres->nor != NULL) {
    float ofs = tex->nabla / tex->noisesize; /* also scaling of texvec */

    /* calculate bumpnormal */
    texres->nor[0] = BLI_noise_mg_variable_lacunarity(texvec[0] + ofs,
                                                      texvec[1],
                                                      texvec[2],
                                                      tex->dist_amount,
                                                      tex->noisebasis,
                                                      tex->noisebasis2);
    texres->nor[1] = BLI_noise_mg_variable_lacunarity(texvec[0],
                                                      texvec[1] + ofs,
                                                      texvec[2],
                                                      tex->dist_amount,
                                                      tex->noisebasis,
                                                      tex->noisebasis2);
    texres->nor[2] = BLI_noise_mg_variable_lacunarity(texvec[0],
                                                      texvec[1],
                                                      texvec[2] + ofs,
                                                      tex->dist_amount,
                                                      tex->noisebasis,
                                                      tex->noisebasis2);

    tex_normal_derivate(tex, texres);
    rv |= TEX_NOR;
  }

  BRICONT;

  return rv;
}
