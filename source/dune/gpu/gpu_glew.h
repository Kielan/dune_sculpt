#pragma once

#if defined(WITH_OPENGL)
#  include "glew-mx.h"
#  ifndef WITH_LEGACY_OPENGL
#    include "gpu_legacy_stubs.h"
#  endif
#endif
