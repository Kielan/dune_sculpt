/* Types Defaults
 * ============
 *
 * This API provides direct access to DNA default structs
 * to avoid duplicating values for initialization, versioning and RNA.
 * This allows types default definitions to be defined in a single header along side the types.
 * So each `types_{name}.h` can have an optional `DNA_{name}_defaults.h` file along side it.
 *
 * Defining the defaults is optional since it doesn't make sense for some structs to have defaults.
 *
 * Adding Defaults
 * ---------------
 *
 * Adding/removing defaults for existing structs can be done by hand.
 * When adding new defaults for larger structs you may want to write-out the in-memory data.
 *
 * To create these defaults there is a GDB script which can be handy to get started:
 * `./source/tools/utils/gdb_struct_repr_c99.py`
 *
 * Magic numbers should be replaced with flags before committing.
 *
 * Public API
 * ----------
 *
 * The main functions to access these are:
 * - types_struct_default_get
 * - types_struct_default_alloc
 *
 * These access the struct table types_default_table using the struct number.
 *
 * note Struct members only define their members (ptrs are left as NULL set).
 *
 * Typical Usage
 * -------------
 *
 * While there is no restriction for using these defaults,
 * it's worth noting where these fns are typically used:
 *
 * - When creating/allocating new data.
 * - api prop defaults, used for "Set Default Value" in the buttons right-click cxt menu.
 *
 * These defaults are not used:
 *
 * - When loading old files that don't contain newly added struct members (these will be zeroed)
 *   to set their values use `versioning_{DUNE_VERSION}.c` source files.
 * - For startup file data, to update these defaults use
 *   loader_update_defaults_startup_blend & #blo_do_versions_userdef. */

#define TYPES_DEPRECATED_ALLOW

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_endian_switch.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_utildefines.h"

#include "imbuf.h"

#include "types_defaults.h"

#include "types_armature.h"
#include "types_asset_types.h"
#include "types_brush_types.h"
#include "types_cachefile_types.h"
#include "types_camera_types.h"
#include "types_cloth_types.h"
#include "types_collection_types.h"
#include "types_curve_types.h"
#include "types_curves_types.h"
#include "types_fluid_types.h"
#include "types_pen_mod_types.h"
#include "types_image_types.h"
#include "types_key_types.h"
#include "types_lattice_types.h"
#include "types_light_types.h"
#include "types_lightprobe_types.h"
#include "types_linestyle_types.h"
#include "types_mask_types.h"
#include "types_material_types.h"
#include "types_mesh.h"
#include "types_meta.h"
#include "types_mod.h"
#include "types_movieclip.h"
#include "types_object.h"
#include "types_particle.h"
#include "types_pointcloud.h"
#include "types_scene.h"
#include "types_simulation.h"
#include "types_space.h"
#include "types_speaker.h"
#include "types_texture.h"
#include "types_volume.h"
#include "types_world.h"

#include "types_armature_defaults.h"
#include "types_asset_defaults.h"
#include "types_brush_defaults.h"
#include "types_cachefile_defaults.h"
#include "DNA_camera_defaults.h"
#include "DNA_collection_defaults.h"
#include "DNA_curve_defaults.h"
#include "DNA_curves_defaults.h"
#include "DNA_fluid_defaults.h"
#include "DNA_pen_mod_defaults.h"
#include "types_image_defaults.h"
#include "types_lattice_defaults.h"
#include "types_light_defaults.h"
#include "types_lightprobe_defaults.h"
#include "types_linestyle_defaults.h"
#include "DNA_material_defaults.h"
#include "DNA_mesh_defaults.h"
#include "DNA_meta_defaults.h"
#include "DNA_modifier_defaults.h"
#include "DNA_movieclip_defaults.h"
#include "DNA_object_defaults.h"
#include "DNA_particle_defaults.h"
#include "DNA_pointcloud_defaults.h"
#include "DNA_scene_defaults.h"
#include "DNA_simulation_defaults.h"
#include "DNA_space_defaults.h"
#include "DNA_speaker_defaults.h"
#include "DNA_texture_defaults.h"
#include "DNA_volume_defaults.h"
#include "DNA_world_defaults.h"

#define SDNA_DEFAULT_DECL_STRUCT(struct_name) \
  static const struct_name DNA_DEFAULT_##struct_name = _DNA_DEFAULT_##struct_name

/* DNA_asset_defaults.h */
SDNA_DEFAULT_DECL_STRUCT(AssetMetaData);
SDNA_DEFAULT_DECL_STRUCT(AssetLibraryReference);

/* DNA_armature_defaults.h */
SDNA_DEFAULT_DECL_STRUCT(bArmature);

/* DNA_brush_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Brush);

/* types_cachefile_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(CacheFile);

/* types_camera_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Camera);

/* types_collection_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Collection);

/* types_curve_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Curve);

/* DNA_fluid_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(FluidDomainSettings);
STYPES_DEFAULT_DECL_STRUCT(FluidFlowSettings);
STYPES_DEFAULT_DECL_STRUCT(FluidEffectorSettings);

/* types_image_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Image);

/* types_curves_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Curves);

/* types_lattice_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Lattice);

/* types_light_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Light);

/* types_lightprobe_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(LightProbe);

/* types_linestyle_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(FreestyleLineStyle);

/* types_material_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Material);

/* types_mesh_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Mesh);

/* types_meta_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(MetaBall);

/* type_movieclip_defaults.h */
STYPE_DEFAULT_DECL_STRUCT(MovieClip);
STYPE_DEFAULT_DECL_STRUCT(MovieClipUser);
STYPE_DEFAULT_DECL_STRUCT(MovieClipScopes);

/* types_object_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Object);

/* types_particle_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(ParticleSettings);

/* types_pointcloud_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(PointCloud);

/* types_scene_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Scene);
STYPES_DEFAULT_DECL_STRUCT(ToolSettings);

/* types_simulation_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Simulation);

/* types_space_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(SpaceClip);

/* types_speaker_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Speaker);

/* types_texture_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Tex);

/* types_view3d_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(View3D);

/* types_volume_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(Volume);

/* types_world_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(World);

/* api_mod_defaults.h */
STYPES_DEFAULT_DECL_STRUCT(ArmatureModData);
STYPES_DEFAULT_DECL_STRUCT(ArrayModData);
SDNA_DEFAULT_DECL_STRUCT(BevelModData);
SDNA_DEFAULT_DECL_STRUCT(BooleanModData);
SDNA_DEFAULT_DECL_STRUCT(BuildModData);
SDNA_DEFAULT_DECL_STRUCT(CastModData);
SDNA_DEFAULT_DECL_STRUCT(ClothSimSettings);
SDNA_DEFAULT_DECL_STRUCT(ClothCollSettings);
SDNA_DEFAULT_DECL_STRUCT(ClothModData);
SDNA_DEFAULT_DECL_STRUCT(CollisionModData);
SDNA_DEFAULT_DECL_STRUCT(CorrectiveSmoothModData);
SDNA_DEFAULT_DECL_STRUCT(CurveModData);
// SDNA_DEFAULT_DECL_STRUCT(DataTransferModData);
SDNA_DEFAULT_DECL_STRUCT(DecimateModData);
SDNA_DEFAULT_DECL_STRUCT(DisplaceModData);
SDNA_DEFAULT_DECL_STRUCT(DynamicPaintModData);
SDNA_DEFAULT_DECL_STRUCT(EdgeSplitModData);
SDNA_DEFAULT_DECL_STRUCT(ExplodeModData);
/* Fluid modifier skipped for now. */
SDNA_DEFAULT_DECL_STRUCT(HookModifierData);
SDNA_DEFAULT_DECL_STRUCT(LaplacianDeformModifierData);
SDNA_DEFAULT_DECL_STRUCT(LaplacianSmoothModifierData);
SDNA_DEFAULT_DECL_STRUCT(LatticeModifierData);
SDNA_DEFAULT_DECL_STRUCT(MaskModifierData);
SDNA_DEFAULT_DECL_STRUCT(MeshCacheModifierData);
SDNA_DEFAULT_DECL_STRUCT(MeshDeformModifierData);
SDNA_DEFAULT_DECL_STRUCT(MeshSeqCacheModifierData);
SDNA_DEFAULT_DECL_STRUCT(MirrorModifierData);
SDNA_DEFAULT_DECL_STRUCT(MultiresModifierData);
SDNA_DEFAULT_DECL_STRUCT(NormalEditModifierData);
SDNA_DEFAULT_DECL_STRUCT(OceanModifierData);
SDNA_DEFAULT_DECL_STRUCT(ParticleInstanceModifierData);
SDNA_DEFAULT_DECL_STRUCT(ParticleSystemModifierData);
SDNA_DEFAULT_DECL_STRUCT(RemeshModifierData);
SDNA_DEFAULT_DECL_STRUCT(ScrewModifierData);
/* Shape key modifier has no items. */
SDNA_DEFAULT_DECL_STRUCT(ShrinkwrapModData);
SDNA_DEFAULT_DECL_STRUCT(SimpleDeformModData);
SDNA_DEFAULT_DECL_STRUCT(NodesModData);
SDNA_DEFAULT_DECL_STRUCT(SkinModData);
SDNA_DEFAULT_DECL_STRUCT(SmoothModData);
/* Softbody modifier skipped for now. */
SDNA_DEFAULT_DECL_STRUCT(SolidifyModData);
SDNA_DEFAULT_DECL_STRUCT(SubsurfModData);
SDNA_DEFAULT_DECL_STRUCT(SurfaceModData);
SDNA_DEFAULT_DECL_STRUCT(SurfaceDeformModData);
SDNA_DEFAULT_DECL_STRUCT(TriangulateModData);
SDNA_DEFAULT_DECL_STRUCT(UVProjectModData);
SDNA_DEFAULT_DECL_STRUCT(UVWarpModData);
SDNA_DEFAULT_DECL_STRUCT(WarpModData);
SDNA_DEFAULT_DECL_STRUCT(WaveModData);
SDNA_DEFAULT_DECL_STRUCT(WeightedNormalModData);
SDNA_DEFAULT_DECL_STRUCT(WeightVGEditModData);
SDNA_DEFAULT_DECL_STRUCT(WeightVGMixModData);
SDNA_DEFAULT_DECL_STRUCT(WeightVGProximityModData);
SDNA_DEFAULT_DECL_STRUCT(WeldModData);
SDNA_DEFAULT_DECL_STRUCT(WireframeModData);

/* DNA_gpencil_modifier_defaults.h */
SDNA_DEFAULT_DECL_STRUCT(ArmaturePenModData);
SDNA_DEFAULT_DECL_STRUCT(ArrayPenModData);
SDNA_DEFAULT_DECL_STRUCT(BuildPenModData);
SDNA_DEFAULT_DECL_STRUCT(ColorPenModData);
SDNA_DEFAULT_DECL_STRUCT(HookPenModData);
SDNA_DEFAULT_DECL_STRUCT(LatticePenModData);
SDNA_DEFAULT_DECL_STRUCT(MirrorPenModData);
SDNA_DEFAULT_DECL_STRUCT(MultiplyGpenModData);
SDNA_DEFAULT_DECL_STRUCT(NoisePenModData);
SDNA_DEFAULT_DECL_STRUCT(OffsetPenModData);
SDNA_DEFAULT_DECL_STRUCT(OpacityGpenModifierData);
SDNA_DEFAULT_DECL_STRUCT(SimplifyGpenModifierData);
SDNA_DEFAULT_DECL_STRUCT(SmoothGpencilModifierData);
SDNA_DEFAULT_DECL_STRUCT(SubdivGpencilModifierData);
SDNA_DEFAULT_DECL_STRUCT(TexturePenModData);
SDNA_DEFAULT_DECL_STRUCT(ThickPenModData);
SDNA_DEFAULT_DECL_STRUCT(TimePenModData);
SDNA_DEFAULT_DECL_STRUCT(TintPenModData);
SDNA_DEFAULT_DECL_STRUCT(WeightProxPenModData);
SDNA_DEFAULT_DECL_STRUCT(WeightAnglePenModData);
SDNA_DEFAULT_DECL_STRUCT(LineartPenModData);
SDNA_DEFAULT_DECL_STRUCT(LengthPenModData);
SDNA_DEFAULT_DECL_STRUCT(DashPenModData);
SDNA_DEFAULT_DECL_STRUCT(DashPenModSegment);
SDNA_DEFAULT_DECL_STRUCT(ShrinkwrapGpencilModData);

#undef STYPES_DEFAULT_DECL_STRUCT

/* Reuse existing definitions. */
extern const struct UserDef U_default;
#define TYPES_DEFAULT_UserDef U_default

extern const Theme U_theme_default;
#define TYPES_DEFAULT_Theme U_theme_default

/* Prevent assigning the wrong struct types since all elements in types_default_table are `void *`. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#  define SDNA_TYPE_CHECKED(v, t) (&(v) + (_Generic((v), t : 0)))
#else
#  define SDNA_TYPE_CHECKED(v, t) (&(v))
#endif

#define SDNA_DEFAULT_DECL(struct_name) \
  [SDNA_TYPE_FROM_STRUCT(struct_name)] = SDNA_TYPE_CHECKED(TYPES_DEFAULT_##struct_name, struct_name)

#define SDNA_DEFAULT_DECL_EX(struct_name, struct_path) \
  [SDNA_TYPE_FROM_STRUCT(struct_name)] = SDNA_TYPE_CHECKED(TYPES_DEFAULT_##struct_path, struct_name)

/** Keep headers sorted. */
const void *DNA_default_table[SDNA_TYPE_MAX] = {

    /* DNA_asset_defaults.h */
    SDNA_DEFAULT_DECL(AssetMetaData),
    SDNA_DEFAULT_DECL(AssetLibraryReference),

    /* DNA_armature_defaults.h */
    SDNA_DEFAULT_DECL(bArmature),

    /* DNA_brush_defaults.h */
    SDNA_DEFAULT_DECL(Brush),

    /* DNA_cachefile_defaults.h */
    SDNA_DEFAULT_DECL(CacheFile),

    /* DNA_camera_defaults.h */
    SDNA_DEFAULT_DECL(Camera),
    SDNA_DEFAULT_DECL_EX(CameraDOFSettings, Camera.dof),
    SDNA_DEFAULT_DECL_EX(CameraStereoSettings, Camera.stereo),

    /* types_collection_defaults.h */
    STYPE_DEFAULT_DECL(Collection),

    /* types_curve_defaults.h */
    STYPE_DEFAULT_DECL(Curve),

    /* DNA_fluid_defaults.h */
    SDNA_DEFAULT_DECL(FluidDomainSettings),
    STYPE_DEFAULT_DECL(FluidFlowSettings),
    STYPE_DEFAULT_DECL(FluidEffectorSettings),

    /* types_image_defaults.h */
    STYPE_DEFAULT_DECL(Image),

    /* types_curves_defaults.h */
    STYPE_DEFAULT_DECL(Curves),

    /* types_lattice_defaults.h */
    STYPE_DEFAULT_DECL(Lattice),

    /* DNA_light_defaults.h */
    SDNA_DEFAULT_DECL(Light),

    /* DNA_lightprobe_defaults.h */
    SDNA_DEFAULT_DECL(LightProbe),

    /* DNA_linestyle_defaults.h */
    SDNA_DEFAULT_DECL(FreestyleLineStyle),

    /* DNA_material_defaults.h */
    SDNA_DEFAULT_DECL(Material),

    /* DNA_mesh_defaults.h */
    SDNA_DEFAULT_DECL(Mesh),

    /* DNA_space_defaults.h */
    SDNA_DEFAULT_DECL(SpaceClip),
    SDNA_DEFAULT_DECL_EX(MaskSpaceInfo, SpaceClip.mask_info),

    /* DNA_meta_defaults.h */
    SDNA_DEFAULT_DECL(MetaBall),

    /* DNA_movieclip_defaults.h */
    SDNA_DEFAULT_DECL(MovieClip),
    SDNA_DEFAULT_DECL(MovieClipUser),
    SDNA_DEFAULT_DECL(MovieClipScopes),
    SDNA_DEFAULT_DECL_EX(MovieTrackingMarker, MovieClipScopes.undist_marker),

    /* DNA_object_defaults.h */
    SDNA_DEFAULT_DECL(Object),

    /* DNA_particle_defaults.h */
    SDNA_DEFAULT_DECL(ParticleSettings),

    /* DNA_pointcloud_defaults.h */
    SDNA_DEFAULT_DECL(PointCloud),

    /* DNA_scene_defaults.h */
    SDNA_DEFAULT_DECL(Scene),
    SDNA_DEFAULT_DECL_EX(RenderData, Scene.r),
    SDNA_DEFAULT_DECL_EX(ImageFormatData, Scene.r.im_format),
    SDNA_DEFAULT_DECL_EX(BakeData, Scene.r.bake),
    SDNA_DEFAULT_DECL_EX(FFMpegCodecData, Scene.r.ffcodecdata),
    SDNA_DEFAULT_DECL_EX(DisplaySafeAreas, Scene.safe_areas),
    SDNA_DEFAULT_DECL_EX(AudioData, Scene.audio),
    SDNA_DEFAULT_DECL_EX(PhysicsSettings, Scene.physics_settings),
    SDNA_DEFAULT_DECL_EX(SceneDisplay, Scene.display),
    SDNA_DEFAULT_DECL_EX(SceneEEVEE, Scene.eevee),

    SDNA_DEFAULT_DECL(ToolSettings),
    SDNA_DEFAULT_DECL_EX(CurvePaintSettings, ToolSettings.curve_paint_settings),
    SDNA_DEFAULT_DECL_EX(ImagePaintSettings, ToolSettings.imapaint),
    SDNA_DEFAULT_DECL_EX(UnifiedPaintSettings, ToolSettings.unified_paint_settings),
    SDNA_DEFAULT_DECL_EX(ParticleEditSettings, ToolSettings.particle),
    SDNA_DEFAULT_DECL_EX(ParticleBrushData, ToolSettings.particle.brush[0]),
    SDNA_DEFAULT_DECL_EX(MeshStatVis, ToolSettings.statvis),
    SDNA_DEFAULT_DECL_EX(Pen_Sculpt_Settings, ToolSettings.gp_sculpt),
    SDNA_DEFAULT_DECL_EX(Pen_Sculpt_Guide, ToolSettings.gp_sculpt.guide),

    /* DNA_simulation_defaults.h */
    SDNA_DEFAULT_DECL(Simulation),

    /* DNA_speaker_defaults.h */
    SDNA_DEFAULT_DECL(Speaker),

    /* DNA_texture_defaults.h */
    SDNA_DEFAULT_DECL(Tex),
    SDNA_DEFAULT_DECL_EX(MTex, Brush.mtex),

    /* DNA_userdef_types.h */
    SDNA_DEFAULT_DECL(UserDef),
    SDNA_DEFAULT_DECL(Theme),
    SDNA_DEFAULT_DECL_EX(UserDef_SpaceData, UserDef.space_data),
    SDNA_DEFAULT_DECL_EX(UserDef_FileSpaceData, UserDef.file_space_data),
    SDNA_DEFAULT_DECL_EX(WalkNavigation, UserDef.walk_navigation),

    /* DNA_view3d_defaults.h */
    SDNA_DEFAULT_DECL(View3D),
    SDNA_DEFAULT_DECL_EX(View3DOverlay, View3D.overlay),
    SDNA_DEFAULT_DECL_EX(View3DShading, View3D.shading),
    SDNA_DEFAULT_DECL_EX(View3DCursor, Scene.cursor),

    /* DNA_volume_defaults.h */
    SDNA_DEFAULT_DECL(Volume),

    /* DNA_world_defaults.h */
    SDNA_DEFAULT_DECL(World),

    /* DNA_modifier_defaults.h */
    SDNA_DEFAULT_DECL(ArmatureModifierData),
    SDNA_DEFAULT_DECL(ArrayModifierData),
    SDNA_DEFAULT_DECL(BevelModifierData),
    SDNA_DEFAULT_DECL(BooleanModifierData),
    SDNA_DEFAULT_DECL(BuildModifierData),
    SDNA_DEFAULT_DECL(CastModifierData),
    SDNA_DEFAULT_DECL(ClothSimSettings),
    SDNA_DEFAULT_DECL(ClothCollSettings),
    SDNA_DEFAULT_DECL(ClothModifierData),
    SDNA_DEFAULT_DECL(CollisionModifierData),
    SDNA_DEFAULT_DECL(CorrectiveSmoothModifierData),
    SDNA_DEFAULT_DECL(CurveModData),
    // SDNA_DEFAULT_DECL(DataTransferModData),
    SDNA_DEFAULT_DECL(DecimateModData),
    SDNA_DEFAULT_DECL(DisplaceModData),
    SDNA_DEFAULT_DECL(DynamicPaintModData),
    SDNA_DEFAULT_DECL(EdgeSplitModData),
    SDNA_DEFAULT_DECL(ExplodeModData),
    /* Fluid modifier skipped for now. */
    SDNA_DEFAULT_DECL(HookModData),
    SDNA_DEFAULT_DECL(LaplacianDeformModData),
    SDNA_DEFAULT_DECL(LaplacianSmoothModData),
    SDNA_DEFAULT_DECL(LatticeModData),
    SDNA_DEFAULT_DECL(MaskModData),
    SDNA_DEFAULT_DECL(MeshCacheModData),
    SDNA_DEFAULT_DECL(MeshDeformModData),
    SDNA_DEFAULT_DECL(MeshSeqCacheModData),
    SDNA_DEFAULT_DECL(MirrorModData),
    SDNA_DEFAULT_DECL(MultiresModData),
    SDNA_DEFAULT_DECL(NormalEditModData),
    SDNA_DEFAULT_DECL(OceanModData),
    SDNA_DEFAULT_DECL(ParticleInstanceModData),
    SDNA_DEFAULT_DECL(ParticleSystemModData),
    SDNA_DEFAULT_DECL(RemeshModData),
    SDNA_DEFAULT_DECL(ScrewModData),
    /* Shape key modifier has no items. */
    SDNA_DEFAULT_DECL(ShrinkwrapModData),
    SDNA_DEFAULT_DECL(SimpleDeformModData),
    SDNA_DEFAULT_DECL(NodesModData),
    SDNA_DEFAULT_DECL(SkinModData),
    SDNA_DEFAULT_DECL(SmoothModData),
    /* Softbody modifier skipped for now. */
    SDNA_DEFAULT_DECL(SolidifyModData),
    SDNA_DEFAULT_DECL(SubsurfModData),
    SDNA_DEFAULT_DECL(SurfaceModData),
    SDNA_DEFAULT_DECL(SurfaceDeformModData),
    SDNA_DEFAULT_DECL(TriangulateModData),
    SDNA_DEFAULT_DECL(UVProjectModData),
    SDNA_DEFAULT_DECL(UVWarpModData),
    SDNA_DEFAULT_DECL(WarpModData),
    SDNA_DEFAULT_DECL(WaveModData),
    SDNA_DEFAULT_DECL(WeightedNormalModData),
    SDNA_DEFAULT_DECL(WeightVGEditModData),
    SDNA_DEFAULT_DECL(WeightVGMixModData),
    SDNA_DEFAULT_DECL(WeightVGProximityModData),
    SDNA_DEFAULT_DECL(WeldModData),
    SDNA_DEFAULT_DECL(WireframeModData),

    /* DNA_gpencil_modifier_defaults.h */
    SDNA_DEFAULT_DECL(ArmaturePenModData),
    SDNA_DEFAULT_DECL(ArrayPenModData),
    SDNA_DEFAULT_DECL(BuildPenModData),
    SDNA_DEFAULT_DECL(ColorPenModData),
    SDNA_DEFAULT_DECL(HookPenModData),
    SDNA_DEFAULT_DECL(LatticePenModData),
    SDNA_DEFAULT_DECL(MirrorPenModData),
    SDNA_DEFAULT_DECL(MultiplyPenModData),
    SDNA_DEFAULT_DECL(NoisePenModData),
    SDNA_DEFAULT_DECL(OffsetPenModData),
    SDNA_DEFAULT_DECL(OpacityPenModData),
    SDNA_DEFAULT_DECL(SimplifyPenModData),
    SDNA_DEFAULT_DECL(SmoothPenModData),
    SDNA_DEFAULT_DECL(SubdivPenModData),
    SDNA_DEFAULT_DECL(TexturePenModData),
    SDNA_DEFAULT_DECL(ThickPenModData),
    SDNA_DEFAULT_DECL(TimePenModData),
    SDNA_DEFAULT_DECL(TintPenModData),
    SDNA_DEFAULT_DECL(WeightAnglePenModData),
    SDNA_DEFAULT_DECL(WeightProxPenModData),
    SDNA_DEFAULT_DECL(LineartPenModData),
    SDNA_DEFAULT_DECL(LengthPenModData),
    SDNA_DEFAULT_DECL(DashPenModData),
    SDNA_DEFAULT_DECL(DashPenModSegment),
    SDNA_DEFAULT_DECL(ShrinkwrapPenModData),
};
#undef SDNA_DEFAULT_DECL
#undef SDNA_DEFAULT_DECL_EX
