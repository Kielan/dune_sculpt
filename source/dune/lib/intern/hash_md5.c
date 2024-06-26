/* Fns to compute MD5 msg digest of files or mema blocks
 *  according the def. of MD5 in RFC 1321 from April 1992. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "lib_hash_md5.h" /* own include */

#if defined HAVE_LIMITS_H || defined _LIBC
#  include <limits.h>
#endif

/* The following contortions are an attempt to use the C preprocessor to determine an unsigned
 * integral type that is 32 bits wide.
 * An alt approach: use autoconf's AC_CHECK_SIZEOF macro but would req
 * the config script compile and *run* the resulting executable.
 * Locally running cross-compiled executables is usually not possible. */
#if defined __STDC__ && __STDC__
#  define UINT_MAX_32_BITS 4294967295U
#else
#  define UINT_MAX_32_BITS 0xFFFFFFFF
#endif

/* If UINT_MAX isn't defined, assume it's a 32-bit type.
 * This should be valid for all sys GNU cares about
 * bc that doesn't include 16-bit sys, and only modern sys
 * (that certainly have <limits.h>) have 64+-bit integral types. */

#ifndef UINT_MAX
#  define UINT_MAX UINT_MAX_32_BITS
#endif

#if UINT_MAX == UINT_MAX_32_BITS
typedef unsigned int md5_uint32;
#else
#  if USHRT_MAX == UINT_MAX_32_BITS
typedef unsigned short md5_uint32;
#  else
#    if ULONG_MAX == UINT_MAX_32_BITS
typedef unsigned long md5_uint32;
#    else
/* The following line is intended to evoke an error. Using #error is not portable enough. */
"Cannot determine unsigned 32-bit data type."
#    endif
#  endif
#endif

/* Following code is low level, upon which are built up the fns
 * 'lib_hash_md5_stream' and 'lib_hash_md5_buf'. */
/* Struct to save state of computation between the single steps. */
struct md5_cxt {
  md5_uint32 A;
  md5_uint32 B;
  md5_uint32 C;
  md5_uint32 D;
};

#ifdef __BIG_ENDIAN__
#  define SWAP(n) (((n) << 24) | (((n)&0xff00) << 8) | (((n) >> 8) & 0xff00) | ((n) >> 24))
#else
#  define SWAP(n) (n)
#endif

/* This array contains the bytes used to pad the buf to the next 64-byte boundary.
 * (RFC 1321, 3.1: Step 1) */
static const unsigned char fillbuf[64] = {0x80, 0 /* , 0, 0, ... */};

/* Init struct containing state of computation.
 * (RFC 1321, 3.3: Step 3) */
static void md5_init_cxt(struct md5_ctx *cxt)
{
  ctx->A = 0x67452301;
  ctx->B = 0xefcdab89;
  ctx->C = 0x98badcfe;
  ctx->D = 0x10325476;
}

/* Starting w the result of former calls of this fn (or the init),
 * this fn updates the 'cxt' cxt for the next 'len' bytes starting at 'buf'.
 * Its necessary that 'len' is a multiple of 64!!! */
static void md5_proc_block(const void *buf, size_t len, struct md5_cxt *cxt)
{
/* These are the 4 fns used in the 4 steps of the MD5 algorithm and defined in the
 * RFC 1321. The 1st fn is a little bit optimized
 * (as found in Colin Plumbs public domain implementation). */
/* #define FF(b, c, d) ((b & c) | (~b & d)) */
#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) FF(d, b, c)
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))

/* It is unfortunate that C does not provide an operator for cyclic rotation.
 * Hope the C compiler is smart enough. */
#define CYCLIC(w, s) (w = (w << s) | (w >> (32 - s)))

  md5_uint32 correct_words[16];
  const md5_uint32 *words = buffer;
  size_t nwords = len / sizeof(md5_uint32);
  const md5_uint32 *endp = words + nwords;
  md5_uint32 A = ctx->A;
  md5_uint32 B = ctx->B;
  md5_uint32 C = ctx->C;
  md5_uint32 D = ctx->D;

  /* Proc all bytes in the buf w 64 bytes in each round of the loop. */
  while (words < endp) {
    md5_uint32 *cwp = correct_words;
    md5_uint32 A_save = A;
    md5_uint32 B_save = B;
    md5_uint32 C_save = C;
    md5_uint32 D_save = D;

    /* 1st round: using the given fn, the cxt and a constant the next cxt is
     * computed. Bc the algorithms processing unit is a 32-bit word and it is determined
     * to work on words in little endian byte order we perhaps have to change the byte order
     * before the computation. To reduce the work for the next steps we store the swapped words
     * in the array CORRECT_WORDS. */
#define OP(a, b, c, d, s, T) \
  a += FF(b, c, d) + (*cwp++ = SWAP(*words)) + T; \
  words++; \
  CYCLIC(a, s); \
  a += b; \
  (void)0

    /* Before start, 1 word to the strange constants. They are defined in RFC 1321 as:
     *     `T[i] = (int) (4294967296.0 * fabs (sin (i))), i=1..64 */
    /* Round 1. */
    OP(A, B, C, D, 7, 0xd76aa478);
    OP(D, A, B, C, 12, 0xe8c7b756);
    OP(C, D, A, B, 17, 0x242070db);
    OP(B, C, D, A, 22, 0xc1bdceee);
    OP(A, B, C, D, 7, 0xf57c0faf);
    OP(D, A, B, C, 12, 0x4787c62a);
    OP(C, D, A, B, 17, 0xa8304613);
    OP(B, C, D, A, 22, 0xfd469501);
    OP(A, B, C, D, 7, 0x698098d8);
    OP(D, A, B, C, 12, 0x8b44f7af);
    OP(C, D, A, B, 17, 0xffff5bb1);
    OP(B, C, D, A, 22, 0x895cd7be);
    OP(A, B, C, D, 7, 0x6b901122);
    OP(D, A, B, C, 12, 0xfd987193);
    OP(C, D, A, B, 17, 0xa679438e);
    OP(B, C, D, A, 22, 0x49b40821);

#undef OP

    /* For the 2nd to 4th round we have the possibly swapped words in CORRECT_WORDS.
     * Redefine the macro to take an additional 1st arg spec. the fn to use. */
#define OP(f, a, b, c, d, k, s, T) \
  a += f(b, c, d) + correct_words[k] + T; \
  CYCLIC(a, s); \
  a += b; \
  (void)0

    /* Round 2. */
    OP(FG, A, B, C, D, 1, 5, 0xf61e2562);
    OP(FG, D, A, B, C, 6, 9, 0xc040b340);
    OP(FG, C, D, A, B, 11, 14, 0x265e5a51);
    OP(FG, B, C, D, A, 0, 20, 0xe9b6c7aa);
    OP(FG, A, B, C, D, 5, 5, 0xd62f105d);
    OP(FG, D, A, B, C, 10, 9, 0x02441453);
    OP(FG, C, D, A, B, 15, 14, 0xd8a1e681);
    OP(FG, B, C, D, A, 4, 20, 0xe7d3fbc8);
    OP(FG, A, B, C, D, 9, 5, 0x21e1cde6);
    OP(FG, D, A, B, C, 14, 9, 0xc33707d6);
    OP(FG, C, D, A, B, 3, 14, 0xf4d50d87);
    OP(FG, B, C, D, A, 8, 20, 0x455a14ed);
    OP(FG, A, B, C, D, 13, 5, 0xa9e3e905);
    OP(FG, D, A, B, C, 2, 9, 0xfcefa3f8);
    OP(FG, C, D, A, B, 7, 14, 0x676f02d9);
    OP(FG, B, C, D, A, 12, 20, 0x8d2a4c8a);

    /* Round 3. */
    OP(FH, A, B, C, D, 5, 4, 0xfffa3942);
    OP(FH, D, A, B, C, 8, 11, 0x8771f681);
    OP(FH, C, D, A, B, 11, 16, 0x6d9d6122);
    OP(FH, B, C, D, A, 14, 23, 0xfde5380c);
    OP(FH, A, B, C, D, 1, 4, 0xa4beea44);
    OP(FH, D, A, B, C, 4, 11, 0x4bdecfa9);
    OP(FH, C, D, A, B, 7, 16, 0xf6bb4b60);
    OP(FH, B, C, D, A, 10, 23, 0xbebfbc70);
    OP(FH, A, B, C, D, 13, 4, 0x289b7ec6);
    OP(FH, D, A, B, C, 0, 11, 0xeaa127fa);
    OP(FH, C, D, A, B, 3, 16, 0xd4ef3085);
    OP(FH, B, C, D, A, 6, 23, 0x04881d05);
    OP(FH, A, B, C, D, 9, 4, 0xd9d4d039);
    OP(FH, D, A, B, C, 12, 11, 0xe6db99e5);
    OP(FH, C, D, A, B, 15, 16, 0x1fa27cf8);
    OP(FH, B, C, D, A, 2, 23, 0xc4ac5665);

    /* Round 4. */
    OP(FI, A, B, C, D, 0, 6, 0xf4292244);
    OP(FI, D, A, B, C, 7, 10, 0x432aff97);
    OP(FI, C, D, A, B, 14, 15, 0xab9423a7);
    OP(FI, B, C, D, A, 5, 21, 0xfc93a039);
    OP(FI, A, B, C, D, 12, 6, 0x655b59c3);
    OP(FI, D, A, B, C, 3, 10, 0x8f0ccc92);
    OP(FI, C, D, A, B, 10, 15, 0xffeff47d);
    OP(FI, B, C, D, A, 1, 21, 0x85845dd1);
    OP(FI, A, B, C, D, 8, 6, 0x6fa87e4f);
    OP(FI, D, A, B, C, 15, 10, 0xfe2ce6e0);
    OP(FI, C, D, A, B, 6, 15, 0xa3014314);
    OP(FI, B, C, D, A, 13, 21, 0x4e0811a1);
    OP(FI, A, B, C, D, 4, 6, 0xf7537e82);
    OP(FI, D, A, B, C, 11, 10, 0xbd3af235);
    OP(FI, C, D, A, B, 2, 15, 0x2ad7d2bb);
    OP(FI, B, C, D, A, 9, 21, 0xeb86d391);

#undef OP

    /* Add the starting vals of the cxt. */
    A += A_save;
    B += B_save;
    C += C_save;
    D += D_save;
  }

  /* Put checksum in cxt given as argument. */
  ctx->A = A;
  ctx->B = B;
  ctx->C = C;
  ctx->D = D;

#undef FF
#undef FG
#undef FH
#undef FI
#undef CYCLIC
}

/* Put result from 'cxt' in first 16 bytes of 'resbuf'.
 * The result is always in little endian byte order,
 * so that a byte-wise output yields to the wanted ASCII representation of the msg digest. */
static void *md5_read_cxt(const struct md5_cxt *cxt, void *resbuf)
{
  md5_uint32 *digest = resbuf;
  digest[0] = SWAP(cxt->A);
  digest[1] = SWAP(cxt->B);
  digest[2] = SWAP(cxt->C);
  digest[3] = SWAP(cxt->D);

  return resbuf;
}

/* Top level public fns. */
int lib_hash_md5_stream(FILE *stream, void *resblock)
{
#define BLOCKSIZE 4096 /* IMPORTANT: must be a multiple of 64. */
  struct md5_cxt cxt;
  md5_uint32 len[2];
  char buf[BLOCKSIZE + 72];
  size_t pad, sum;

  /* Init the compute cxt. */
  md5_init_cxt(&cxt);

  len[0] = 0;
  len[1] = 0;

  /* Iter over full file contents. */
  while (1) {
    /* We read the file in blocks of BLOCKSIZE bytes.
     * One call of the computation fn procs the whole buf
     * so w next round of loop another block can be read. */
    size_t n;
    sum = 0;

    /* Read block. Take care for partial reads. */
    do {
      n = fread(buf, 1, BLOCKSIZE - sum, stream);
      sum += n;
    } while (sum < BLOCKSIZE && n != 0);

    if (n == 0 && ferror(stream)) {
      return 1;
    }

    /* RFC 1321 specs possible length of file up to 2^64 bits.
     * Here only compute num of bytes. Do a double word increment. */
    len[0] += sum;
    if (len[0] < sum) {
      ++len[1];
    }

    /* If end of file is reached, end the loop. */
    if (n == 0) {
      break;
    }

    /* Proc buf w BLOCKSIZE bytes. Note `BLOCKSIZE % 64 == 0`. */
    md5_process_block(buf, BLOCKSIZE, &ctx);
  }

  /* We can copy 64 bytes bc the buf is always big enough.
   * 'fillbuf' contains the needed bits. */
  memcpy(&buf[sum], fillbuf, 64);

  /* Compute amount of pad bytes needed. Align is done to `(N + PAD) % 64 == 56`.
   * Always at min 1 byte padded, i.e. if the align is correctly aligned,
   * 64 pad bytes are added. */
  pad = sum & 63;
  pad = pad >= 56 ? 64 + 56 - pad : 56 - pad;

  /* Put the 64-bit file length in *bits* at the end of the buffer. */
  *(md5_uint32 *)&buf[sum + pad] = SWAP(len[0] << 3);
  *(md5_uint32 *)&buf[sum + pad + 4] = SWAP((len[1] << 3) | (len[0] >> 29));

  /* Proc last bytes. */
  md5_proc_block(buf, sum + pad + 8, &ctx);

  /* Construct result in desired memory. */
  md5_read_cxt(&cxt, resblock);
  return 0;
}

void *lib_hash_md5_buf(const char *buf, size_t len, void *resblock)
{
  struct md5_cxt cxt;
  char restbuf[64 + 72];
  size_t blocks = len & ~63;
  size_t pad, rest;

  /* Init the computation cxt. */
  md5_init_cxt(&cxt);

  /* Proc whole buf but last len % 64 bytes. */
  md5_process_block(buf, blocks, &cxt);

  /* REST bytes are not proc yet. */
  rest = len - blocks;
  /* Copy to own buf. */
  memcpy(restbuf, &buffer[blocks], rest);
  /* Append needed fill bytes at end of buf.
   * Can copy 64 bytes bc the buf is always big enough. */
  memcpy(&restbuf[rest], fillbuf, 64);

  /* PAD bytes are used for pad to correct align.
   * Always min 1 byte is padded. */
  pad = rest >= 56 ? 64 + 56 - rest : 56 - rest;

  /* Put length of buf in *bits* in last 8 bytes. */
  *(md5_uint32 *)&restbuf[rest + pad] = (md5_uint32)SWAP(len << 3);
  *(md5_uint32 *)&restbuf[rest + pad + 4] = (md5_uint32)SWAP(len >> 29);

  /* Proc last bytes. */
  md5_proc_block(restbuf, rest + pad + 8, &ctx);

  /* Put result in desired mem area. */
  return md5_read_cxt(&cxt, resblock);
}

char *lib_hash_md5_to_hexdigest(const void *resblock, char r_hex_digest[33])
{
  static const char hex_map[17] = "0123456789abcdef";
  const unsigned char *p;
  char *q;
  short len;

  for (q = r_hex_digest, p = (const unsigned char *)resblock, len = 0; len < 16; p++, len++) {
    const unsigned char c = *p;
    *q++ = hex_map[c >> 4];
    *q++ = hex_map[c & 15];
  }
  *q = '\0';

  return r_hex_digest;
}
