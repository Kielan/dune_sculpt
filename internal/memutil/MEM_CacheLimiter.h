#ifndef __MEM_CACHELIMITERC_API_H__
#define __MEM_CACHELIMITERC_API_H__

struct MEM_STRUCT_CacheLimiter;
struct MEM_STRUCT_CacheLimiterHandle;

typedef struct MEM_STRUCT_CacheLimiter MEM_CacheLimiter;
typedef struct MEM_STRUCT_CacheLimiterHandle MEM_CacheLimiterHandle;

/* function used to remove data from memory */
typedef void (*MEM_CacheLimiter_Destruct_Fn)(void *);

/* function used to measure stored data element size */
typedef size_t (*MEM_CacheLimiter_DataSize_Fn)(void *);

/* function used to measure priority of item when freeing memory */
typedef int (*MEM_CacheLimiter_ItemPriority_Fn)(void *, int);

/* function to check whether item could be destroyed */
typedef bool (*MEM_CacheLimiter_ItemDestroyable_Fn)(void *);

#ifndef __MEM_CACHELIMITER_H__
void MEM_CacheLimiter_set_maximum(size_t m);
size_t MEM_CacheLimiter_get_maximum(void);
void MEM_CacheLimiter_set_disabled(bool disabled);
bool MEM_CacheLimiter_is_disabled(void);
#endif /* __MEM_CACHELIMITER_H__ */

/**
 * Create new MEM_CacheLimiter object
 * managed objects are destructed with the data_destructor
 *
 * param data_destructor: TODO.
 * return A new #MEM_CacheLimter object.
 */

MEM_CacheLimiter *new_MEM_CacheLimiter(MEM_CacheLimiter_Destruct_Fn data_destructor,
                                        MEM_CacheLimiter_DataSize_Fn data_size);

/**
 * Delete MEM_CacheLimiter
 *
 * Frees the memory of the CacheLimiter but does not touch managed objects!
 *
 * param This: "This" pointer.
 */

void delete_MEM_CacheLimiter(MEM_CacheLimiter *this);

/**
 * Manage object
 *
 * param this: "this" pointer, data data object to manage.
 * return CacheLimiterHandle to ref, unref, touch the managed object
 */

MEM_CacheLimiterHandle *MEM_CacheLimiter_insert(MEM_CacheLimiter *this, void *data);

/**
 * Free objects until memory constraints are satisfied
 *
 * param this: "this" pointer.
 */

void MEM_CacheLimiter_enforce_limits(MEM_CacheLimiter *this);

/**
 * Unmanage object previously inserted object.
 * Does _not_ delete managed object!
 *
 * param handle: of object.
 */

void MEM_CacheLimiter_unmanage(MEM_CacheLimiterHandle *handle);

/**
 * Raise priority of object (put it at the tail of the deletion chain)
 *
 * param handle: of object.
 */

void MEM_CacheLimiter_touch(MEM_CacheLimiterHandle *handle);

/**
 * Increment reference counter. Objects with reference counter != 0 are _not_
 * deleted.
 *
 * param handle: of object.
 */

void MEM_CacheLimiter_ref(MEM_CacheLimiterHandle *handle);

/**
 * Decrement reference counter. Objects with reference counter != 0 are _not_
 * deleted.
 *
 * param handle: of object.
 */

void MEM_CacheLimiter_unref(MEM_CacheLimiterHandle *handle);

/**
 * Get reference counter.
 *
 * param handle: of object.
 */

int MEM_CacheLimiter_get_refcount(MEM_CacheLimiterHandle *handle);

/**
 * Get pointer to managed object
 *
 * param handle: of object.
 */

void *MEM_CacheLimiter_get(MEM_CacheLimiterHandle *handle);

void MEM_CacheLimiter_ItemPriority_Fn_set(MEM_CacheLimiter *this,
                                            MEM_CacheLimiter_ItemPriority_Fn item_priority_fn);

void MEM_CacheLimiter_ItemDestroyable_Fn_set(
    MEM_CacheLimiter *this, MEM_CacheLimiter_ItemDestroyable_Fn item_destroyable_fn);

size_t MEM_CacheLimiter_get_memory_in_use(MEM_CacheLimiter *this);

#endif  // __MEM_CACHELIMITERC_API_H__
