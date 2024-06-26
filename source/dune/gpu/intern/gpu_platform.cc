/**
 * Wrap OpenGL features such as textures, shaders and GLSL
 * with checks for drivers and GPU support.
 */

#include "mem_guardedalloc.h"

#include "lib_dynstr.h"
#include "lib_string.h"

#include "gpu_platform.h"

#include "gpu_platform_private.hh"

/* -------------------------------------------------------------------- */
/** GPUPlatformGlobal **/

namespace dune::gpu {

GPUPlatformGlobal GPG;

static char *create_key(eGPUSupportLevel support_level,
                        const char *vendor,
                        const char *renderer,
                        const char *version)
{
  DynStr *ds = lib_dynstr_new();
  lib_dynstr_appendf(ds, "{%s/%s/%s}=", vendor, renderer, version);
  if (support_level == GPU_SUPPORT_LEVEL_SUPPORTED) {
    lib_dynstr_append(ds, "SUPPORTED");
  }
  else if (support_level == GPU_SUPPORT_LEVEL_LIMITED) {
    lib_dynstr_append(ds, "LIMITED");
  }
  else {
    lib_dynstr_append(ds, "UNSUPPORTED");
  }

  char *support_key = lib_dynstr_get_cstring(ds);
  lib_dynstr_free(ds);
  lib_str_replace_char(support_key, '\n', ' ');
  lib_str_replace_char(support_key, '\r', ' ');
  return support_key;
}

static char *create_gpu_name(const char *vendor, const char *renderer, const char *version)
{
  DynStr *ds = lib_dynstr_new();
  lib_dynstr_appendf(ds, "%s %s %s", vendor, renderer, version);

  char *gpu_name = lib_dynstr_get_cstring(ds);
  lib_dynstr_free(ds);
  lib_str_replace_char(gpu_name, '\n', ' ');
  lib_str_replace_char(gpu_name, '\r', ' ');
  return gpu_name;
}

void GPUPlatformGlobal::init(eGPUDeviceType gpu_device,
                             eGPUOSType os_type,
                             eGPUDriverType driver_type,
                             eGPUSupportLevel gpu_support_level,
                             eGPUBackendType backend,
                             const char *vendor_str,
                             const char *renderer_str,
                             const char *version_str)
{
  this->clear();

  this->initialized = true;

  this->device = gpu_device;
  this->os = os_type;
  this->driver = driver_type;
  this->support_level = gpu_support_level;

  this->vendor = lib_strdup(vendor_str);
  this->renderer = lib_strdup(renderer_str);
  this->version = lib_strdup(version_str);
  this->support_key = create_key(gpu_support_level, vendor_str, renderer_str, version_str);
  this->gpu_name = create_gpu_name(vendor_str, renderer_str, version_str);
  this->backend = backend;
}

void GPUPlatformGlobal::clear()
{
  MEM_SAFE_FREE(vendor);
  MEM_SAFE_FREE(renderer);
  MEM_SAFE_FREE(version);
  MEM_SAFE_FREE(support_key);
  MEM_SAFE_FREE(gpu_name);
  initialized = false;
}

}  // namespace dune::gpu

/* -------------------------------------------------------------------- */
/** C-API **/

using namespace dune::gpu;

eGPUSupportLevel gpu_platform_support_level()
{
  lib_assert(GPG.initialized);
  return GPG.support_level;
}

const char *gpu_platform_vendor()
{
  lib_assert(GPG.initialized);
  return GPG.vendor;
}

const char *gpu_platform_renderer()
{
  lib_assert(GPG.initialized);
  return GPG.renderer;
}

const char *gpu_platform_version()
{
  lib_assert(GPG.initialized);
  return GPG.version;
}

const char *gpu_platform_support_level_key()
{
  lib_assert(GPG.initialized);
  return GPG.support_key;
}

const char *gpu_platform_gpu_name()
{
  lib_assert(GPG.initialized);
  return GPG.gpu_name;
}

bool gpu_type_matches(eGPUDeviceType device, eGPUOSType os, eGPUDriverType driver)
{
  return gpu_type_matches_ex(device, os, driver, GPU_BACKEND_ANY);
}

bool gpu_type_matches_ex(eGPUDeviceType device,
                         eGPUOSType os,
                         eGPUDriverType driver,
                         eGPUBackendType backend)
{
  lib_assert(GPG.initialized);
  return (GPG.device & device) && (GPG.os & os) && (GPG.driver & driver) &&
         (GPG.backend & backend);
}
