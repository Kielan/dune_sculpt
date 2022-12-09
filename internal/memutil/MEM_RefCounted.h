#ifndef __MEM_REFCOUNTEDC_API_H__
#define __MEM_REFCOUNTEDC_API_H__

/** A pointer to a private object. */
typedef struct MEM_TOpaqueObject *MEM_TObjectPtr;
/** A pointer to a shared object. */
typedef MEM_TObjectPtr MEM_TRefCountedObjectPtr;

/**
 * Returns the reference count of this object.
 * \param shared: The object to query.
 * \return The current reference count.
 */
extern int MEM_RefCountedGetRef(MEM_TRefCountedObjectPtr shared);

/**
 * Increases the reference count of this object.
 * \param shared: The object to query.
 * \return The new reference count.
 */
extern int MEM_RefCountedIncRef(MEM_TRefCountedObjectPtr shared);

/**
 * Decreases the reference count of this object.
 * If the reference count reaches zero, the object self-destructs.
 * \param shared: The object to query.
 * \return The new reference count.
 */
extern int MEM_RefCountedDecRef(MEM_TRefCountedObjectPtr shared);

#endif  // __MEM_REFCOUNTEDC_API_H__
