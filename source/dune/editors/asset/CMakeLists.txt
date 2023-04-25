set(INC
  .
  ../include
  ../../dune/kernel
  ../../dune/lib
  ../../dune/loader
  ../../dune/i18n
  ../../dune/types
  ../../dune/api
  ../../windowmanager
  ../../../../intern/clog
  ../../../../intern/guardedalloc
  # api_prototypes.h
  ${CMAKE_BINARY_DIR}/source/dune/api
)

set(INC_SYS
)

set(SRC
  intern/asset_catalog.cc
  intern/asset_filter.cc
  intern/asset_handle.cc
  intern/asset_indexer.cc
  intern/asset_lib_ref.cc
  intern/asset_lib_ref_enum.cc
  intern/asset_list.cc
  intern/asset_mark_clear.cc
  intern/asset_ops.cc
  intern/asset_temp_id_consumer.cc
  intern/asset_type.cc

  ED_asset_catalog.h
  ED_asset_catalog.hh
  ED_asset_filter.h
  ED_asset_handle.h
  ED_asset_indexer.h
  ED_asset_lib.h
  ED_asset_list.h
  ED_asset_list.hh
  ED_asset_mark_clear.h
  ED_asset_temp_id_consumer.h
  ED_asset_type.h
  intern/asset_lib_ref.hh
)

set(LIB
  df_dunekernel
  df_duneloader
)

dune_add_lib(df_editor_asset "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

# api_prototypes.h
add_dependencies(df_editor_asset df_api)
