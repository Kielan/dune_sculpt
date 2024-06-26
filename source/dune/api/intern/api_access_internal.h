#pragma once

#include "BLI_utildefines.h"

#include "rna_internal_types.h"

struct IDProperty;
struct PropertyRNAOrID;

/**
 * This function initializes a #PropertyRNAOrID with all required info, from a given #PropertyRNA
 * and #PointerRNA data. It deals properly with the three cases
 * (static RNA, runtime RNA, and #IDProperty).
 * \warning given `ptr` #PointerRNA is assumed to be a valid data one here, calling code is
 * responsible to ensure that.
 */
void rna_property_rna_or_id_get(PropertyRNA *prop,
                                PointerRNA *ptr,
                                PropertyRNAOrID *r_prop_rna_or_id);

void rna_idproperty_touch(struct IDProperty *idprop);
struct IDProperty *rna_idproperty_find(PointerRNA *ptr, const char *name);
