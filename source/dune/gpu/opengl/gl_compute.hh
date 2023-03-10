#pragma once

namespace blender::gpu {

class GLCompute {
 public:
  static void dispatch(int group_x_len, int group_y_len, int group_z_len);
};

}  // namespace blender::gpu
