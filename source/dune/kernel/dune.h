#pragma once

/* Duner utils */

#ifdef __cplusplus
extern "C" {
#endif

struct UserDef;

/* Only to be called on exit Dune. */
void dune_free(void);

void dune_globals_init(void);
void dune_globals_clear(void);

void dune_userdef_data_swap(struct UserDef *userdef_a, struct UserDef *userdef_b);
void dune_userdef_data_set(struct UserDef *userdef);
void dune_userdef_data_set_and_free(struct UserDef *userdef);

/* Write U from userdef.
 * This fn defines which settings a template will override for the user prefs. */
void dune_userdef_app_template_data_swap(struct UserDef *userdef_a,
                                         struct UserDef *userdef_b);
void dune_userdef_app_template_data_set(struct UserDef *userdef);
void dune_userdef_app_template_data_set_and_free(struct UserDef *userdef);

/* When loading a new userdef from file,
 * or when exiting Dune. */
void dune_userdef_data_free(struct UserDef *userdef, bool clear_fonts);

/* Dunes' own atexit (avoids leaking) */
void dune_atexit_register(void (*fn)(void *user_data), void *user_data);
void dune_atexit_unregister(void (*fn)(void *user_data), const void *user_data);
void dune_atexit(void);

#ifdef __cplusplus
}
#endif
