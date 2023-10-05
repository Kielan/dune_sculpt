#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Graph;
struct Main;
struct World;

struct World *world_add(struct Main *main, const char *name);
void world_eval(struct Graph *graph, struct World *world);

#ifdef __cplusplus
}
#endif
