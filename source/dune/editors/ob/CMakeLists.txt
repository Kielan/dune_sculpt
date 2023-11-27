set(INC
  ../include
  ../../asset_system
  ../../dune
  ../../lang
  ../../mesh
  ../../fns
  ../../pen_mods_legacy
  ../../gpu
  ../../ikplugin
  ../../api
  ../../mods
  ../../render
  ../../shader_fx
  ../../windowmanager

  # RNA_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(INC_SYS
)

set(SRC
  add_mod_assets.cc
  ob_add.cc
  ob_bake.cc
  ob_bake_api.cc
  ob_bake_simulation.cc
  ob_collection.cc
  ob_constraint.cc
  ob_data_transfer.cc
  ob_data_transform.cc
  ob_edit.cc
  ob_pen_mod.cc
  ob_hook.cc
  ob_light_linking_ops.cc
  ob_modes.cc
  ob_mod.cc
  ob_ops.cc
  ob_random.cc
  ob_relations.cc
  ob_remesh.cc
  ob_sel.cc
  ob_shader_fx.cc
  ob_shapekey.cc
  ob_transform.cc
  ob_utils.cc
  ob_vgroup.cc
  ob_volume.cc
  ob_warp.cc

  ob_intern.h
)
