#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct CacheFile;
struct CacheFileLayer;
struct CacheReader;
struct Graph;
struct Main;
struct Object;
struct Scene;

void dune_cachefiles_init(void);
void dune_cachefiles_exit(void);

void *dune_cachefile_add(struct Main *main, const char *name);

void dune_cachefile_reload(struct Graph *graph, struct CacheFile *cache_file);

void dune_cachefile_eval(struct Main *main,
                        struct Graph *graph,
                        struct CacheFile *cache_file);

bool dune_cachefile_filepath_get(const struct Main *bmain,
                                const struct Depsgraph *depsgrah,
                                const struct CacheFile *cache_file,
                                char r_filename[1024]);

float dune_cachefile_time_offset(const struct CacheFile *cache_file, float time, float fps);

/* Mods and constraints open and free readers through these. */
void dune_cachefile_reader_open(struct CacheFile *cache_file,
                               struct CacheReader **reader,
                               struct Object *object,
                               const char *object_path);
void dune_cachefile_reader_free(struct CacheFile *cache_file, struct CacheReader **reader);

/* Determine whether the CacheFile should use a render engine procedural. If so, data is not read
 * from the file and bounding boxes are used to represent the objects in the Scene.
 * Render engines will receive the bounding box as a placeholder but can instead
 * load the data directly if they support it. */
bool dune_cache_file_uses_render_procedural(const struct CacheFile *cache_file,
                                           struct Scene *scene,
                                           int dag_eval_mode);

/* Add a layer to the cache_file. Return NULL if the filename is already that of an existing layer
 * or if the number of layers exceeds the maximum allowed layer count. */
struct CacheFileLayer *dune_cachefile_add_layer(struct CacheFile *cache_file,
                                                const char filename[1024]);

struct CacheFileLayer *dune_cachefile_get_active_layer(struct CacheFile *cache_file);

void dune_cachefile_remove_layer(struct CacheFile *cache_file, struct CacheFileLayer *layer);

#ifdef __cplusplus
}
#endif
