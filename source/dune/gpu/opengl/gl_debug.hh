#pragma once

#include "gl_cxt.hh"

#include "glew-mx.h"

/* Manual line breaks for readability. */
/* clang-format off */
#define _VA_ARG_LIST1(t) t
#define _VA_ARG_LIST2(t, a) t a
#define _VA_ARG_LIST4(t, a, b, c) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST2(b, c)
#define _VA_ARG_LIST6(t, a, b, c, d, e) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST4(b, c, d, e)
#define _VA_ARG_LIST8(t, a, b, c, d, e, f, g) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST6(b, c, d, e, f, g)
#define _VA_ARG_LIST10(t, a, b, c, d, e, f, g, h, i) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST8(b, c, d, e, f, g, h, i)
#define _VA_ARG_LIST12(t, a, b, c, d, e, f, g, h, i, j, k) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST10(b, c, d, e, f, g, h, i, j, k)
#define _VA_ARG_LIST14(t, a, b, c, d, e, f, g, h, i, j, k, l, m) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST12(b, c, d, e, f, g, h, i, j, k, l, m)
#define _VA_ARG_LIST16(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST14(b, c, d, e, f, g, h, i, j, k, l, m, o, p)
#define _VA_ARG_LIST18(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST16(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r)
#define _VA_ARG_LIST20(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST18(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u)
#define _VA_ARG_LIST22(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w) \
  _VA_ARG_LIST2(t, a), _VA_ARG_LIST20(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w)
#define ARG_LIST(...) VA_NARGS_CALL_OVERLOAD(_VA_ARG_LIST, __VA_ARGS__)

#define _VA_ARG_LIST_CALL1(t)
#define _VA_ARG_LIST_CALL2(t, a) a
#define _VA_ARG_LIST_CALL4(t, a, b, c) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL2(b, c)
#define _VA_ARG_LIST_CALL6(t, a, b, c, d, e) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL4(b, c, d, e)
#define _VA_ARG_LIST_CALL8(t, a, b, c, d, e, f, g) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL6(b, c, d, e, f, g)
#define _VA_ARG_LIST_CALL10(t, a, b, c, d, e, f, g, h, i) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL8(b, c, d, e, f, g, h, i)
#define _VA_ARG_LIST_CALL12(t, a, b, c, d, e, f, g, h, i, j, k) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL10(b, c, d, e, f, g, h, i, j, k)
#define _VA_ARG_LIST_CALL14(t, a, b, c, d, e, f, g, h, i, j, k, l, m) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL12(b, c, d, e, f, g, h, i, j, k, l, m)
#define _VA_ARG_LIST_CALL16(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL14(b, c, d, e, f, g, h, i, j, k, l, m, o, p)
#define _VA_ARG_LIST_CALL18(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL16(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r)
#define _VA_ARG_LIST_CALL20(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL18(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u)
#define _VA_ARG_LIST_CALL22(t, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w) \
  _VA_ARG_LIST_CALL2(t, a), _VA_ARG_LIST_CALL20(b, c, d, e, f, g, h, i, j, k, l, m, o, p, q, r, s, u, v, w)
#define ARG_LIST_CALL(...) VA_NARGS_CALL_OVERLOAD(_VA_ARG_LIST_CALL, __VA_ARGS__)
/* clang-format on */

#ifdef DEBUG
#  define GL_CHECK_RESOURCES(info) debug::check_gl_resources(info)
#else
#  define GL_CHECK_RESOURCES(info)
#endif

namespace dune {
namespace gpu {
namespace debug {

void raise_gl_error(const char *info);
void check_gl_error(const char *info);
void check_gl_resources(const char *info);
/* This fn needs to be called once per context. */
void init_gl_cbs();

/* Initialize a fallback layer (to KHR_debug) that covers only some functions.
 * We override the fns ptrs by our own implementation that just checks glGetError.
 * Some additional fns (not overridable) are covered inside the header using wrappers. */
void init_debug_layer();

void object_label(GLenum type, GLuint object, const char *name);

}  // namespace debug

#define DEBUG_FN_OVERRIDE(fn, ...) \
  inline void fn(ARG_LIST(__VA_ARGS__)) \
  { \
    if (GLCxt::debug_layer_workaround) { \
      debug::check_gl_error("generated before " #fn); \
      ::fn(ARG_LIST_CALL(__VA_ARGS__)); \
      debug::check_gl_error("" #fn); \
    } \
    else { \
      ::fn(ARG_LIST_CALL(__VA_ARGS__)); \
    } \
  }

/* Avoid very long declarations. */
/* clang-format off */
DEBUG_FN_OVERRIDE(glClear, GLbitfield, mask);
DEBUG_FN_OVERRIDE(glDeleteTextures, GLsizei, n, const GLuint *, textures);
DEBUG_FN_OVERRIDE(glDrawArrays, GLenum, mode, GLint, first, GLsizei, count);
DEBUG_FN_OVERRIDE(glFinish, void);
DEBUG_FN_OVERRIDE(glFlush, void);
DEBUG_FN_OVERRIDE(glGenTextures, GLsizei, n, GLuint *, textures);
DEBUG_FN_OVERRIDE(glGetTexImage, GLenum, target, GLint, level, GLenum, format, GLenum, type, void *, pixels);
DEBUG_FN_OVERRIDE(glReadBuffer, GLenum, mode);
DEBUG_FN_OVERRIDE(glReadPixels, GLint, x, GLint, y, GLsizei, width, GLsizei, height, GLenum, format, GLenum, type, void *, pixels);
DEBUG_FN_OVERRIDE(glTexImage1D, GLenum, target, GLint, level, GLint, internalformat, GLsizei, width, GLint, border, GLenum, format, GLenum, type, const void *, pixels);
DEBUG_FN_OVERRIDE(glTexImage2D, GLenum, target, GLint, level, GLint, internalformat, GLsizei, width, GLsizei, height, GLint, border, GLenum, format, GLenum, type, const void *, pixels);
DEBUG_FN_OVERRIDE(glTexParameteri, GLenum, target, GLenum, pname, GLint, param);
DEBUG_FN_OVERRIDE(glTexParameteriv, GLenum, target, GLenum, pname, const GLint *, params);
DEBUG_FN_OVERRIDE(glTexSubImage1D, GLenum, target, GLint, level, GLint, xoffset, GLsizei, width, GLenum, format, GLenum, type, const void *, pixels);
DEBUG_FN_OVERRIDE(glTexSubImage2D, GLenum, target, GLint, level, GLint, xoffset, GLint, yoffset, GLsizei, width, GLsizei, height, GLenum, format, GLenum, type, const void *, pixels);
/* clang-format on */

}  // namespace gpu
}  // namespace dune
