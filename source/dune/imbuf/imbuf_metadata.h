#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct IdProp;
struct ImBuf;
struct anim;

/**
 * The metadata is a list of key/value pairs (both char *) that can me
 * saved in the header of several image formats.
 * Apart from some common keys like
 * 'Software' and 'Description' (PNG standard) we'll use keys within the
 * Blender namespace, so should be called 'Dune::StampInfo' or 'Blender::FrameNum'
 * etc...
 *
 * The keys & values are stored in Id props, in the group "metadata".
 */

/**
 * Ensure that the metadata property is a valid #IDProperty object.
 * This is a no-op when *metadata != NULL.
 */
void imbuf_metadata_ensure(struct IdProp **metadata);
void imbuf_metadata_free(struct IdProp *metadata);

/**
 * Read the field from the image info into the field.
 * param metadata: the IdProp that contains the metadata
 * param key: the key of the field
 * param value: the data in the field, first one found with key is returned,
 *                 memory has to be allocated by user.
 * param len: length of value buffer allocated by user.
 * return 1 (true) if metadata is present and value for the key found, 0 (false) otherwise.
 */
bool imbuf_metadata_get_field(struct IdProp *metadata, const char *key, char *value, size_t len);

/**
 * Set user data in the metadata.
 * If the field already exists its value is overwritten, otherwise the field
 * will be added with the given value.
 * param metadata: the IdProp that contains the metadata
 * param key: the key of the field
 * param value: the data to be written to the field. zero terminated string
 */
void imbuf_metadata_set_field(struct IdProp *metadata, const char *key, const char *value);

void imbuf_metadata_copy(struct ImBuf *dimb, struct ImBuf *simb);
struct IdProp *imbuf_anim_load_metadata(struct anim *anim);

/* Invoke callback for every value stored in the metadata. */
typedef void (*ImbufMetadataForeachCb)(const char *field, const char *value, void *userdata);
void imbuf_metadata_foreach(struct ImBuf *ibuf, ImBufMetadataForeachCb cb, void *userdata);

#ifdef __cplusplus
}
#endif
