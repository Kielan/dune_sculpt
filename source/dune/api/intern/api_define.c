void RNA_def_property_ui_scale_type(PropertyRNA *prop, PropertyScaleType ui_scale_type)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
      iprop->ui_scale_type = ui_scale_type;
      break;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
      fprop->ui_scale_type = ui_scale_type;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", invalid type for scale.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_range(PropertyRNA *prop, double min, double max)
{
  StructRNA *srna = DefRNA.laststruct;

#ifdef DEBUG
  if (min > max) {
    CLOG_ERROR(&LOG, "\"%s.%s\", min > max.", srna->identifier, prop->identifier);
    DefRNA.error = true;
  }
#endif

  switch (prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
      iprop->hardmin = (int)min;
      iprop->hardmax = (int)max;
      iprop->softmin = MAX2((int)min, iprop->hardmin);
      iprop->softmax = MIN2((int)max, iprop->hardmax);
      break;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
      fprop->hardmin = (float)min;
      fprop->hardmax = (float)max;
      fprop->softmin = MAX2((float)min, fprop->hardmin);
      fprop->softmax = MIN2((float)max, fprop->hardmax);
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", invalid type for range.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_struct_type(PropertyRNA *prop, const char *type)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    fprintf(stderr, "\"%s.%s\": only during preprocessing.", srna->identifier, prop->identifier);
    return;
  }

  switch (prop->type) {
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
      pprop->type = (StructRNA *)type;
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
      cprop->item_type = (StructRNA *)type;
      break;
    }
    default:
      CLOG_ERROR(
          &LOG, "\"%s.%s\", invalid type for struct type.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_struct_runtime(StructOrFunctionRNA *cont, PropertyRNA *prop, StructRNA *type)
{
  /* Never valid when defined from python. */
  StructRNA *srna = DefRNA.laststruct;

  if (DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only at runtime.");
    return;
  }

  const bool is_id_type = (type->flag & STRUCT_ID) != 0;

  switch (prop->type) {
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
      pprop->type = type;

      /* Check between `cont` and `srna` is mandatory, since when defined from python
       * `DefRNA.laststruct` is not valid.
       * This is not an issue as bpy code already checks for this case on its own. */
      if (cont == srna && (srna->flag & STRUCT_NO_DATABLOCK_IDPROPERTIES) != 0 && is_id_type) {
        CLOG_ERROR(&LOG,
                   "\"%s.%s\", this struct type (probably an Operator, Keymap or UserPreference) "
                   "does not accept ID pointer properties.",
                   CONTAINER_RNA_ID(cont),
                   prop->identifier);
        DefRNA.error = true;
        return;
      }

      if (type && (type->flag & STRUCT_ID_REFCOUNT)) {
        prop->flag |= PROP_ID_REFCOUNT;
      }

      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
      cprop->item_type = type;
      break;
    }
    default:
      CLOG_ERROR(&LOG,
                 "\"%s.%s\", invalid type for struct type.",
                 CONTAINER_RNA_ID(cont),
                 prop->identifier);
      DefRNA.error = true;
      return;
  }

  if (is_id_type) {
    prop->flag |= PROP_PTR_NO_OWNERSHIP;
  }
}

void RNA_def_property_enum_native_type(PropertyRNA *prop, const char *native_enum_type)
{
  StructRNA *srna = DefRNA.laststruct;
  switch (prop->type) {
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      eprop->native_enum_type = native_enum_type;
      break;
    }
    default:
      CLOG_ERROR(
          &LOG, "\"%s.%s\", invalid type for struct type.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_enum_items(PropertyRNA *prop, const EnumPropertyItem *item)
{
  StructRNA *srna = DefRNA.laststruct;
  int i, defaultfound = 0;

  switch (prop->type) {
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      eprop->item = (EnumPropertyItem *)item;
      eprop->totitem = 0;
      for (i = 0; item[i].identifier; i++) {
        eprop->totitem++;

        if (item[i].identifier[0]) {
          /* Don't allow spaces in internal enum items (it's fine for Python ones). */
          if (DefRNA.preprocess && strstr(item[i].identifier, " ")) {
            CLOG_ERROR(&LOG,
                       "\"%s.%s\", enum identifiers must not contain spaces.",
                       srna->identifier,
                       prop->identifier);
            DefRNA.error = true;
            break;
          }
          if (item[i].value == eprop->defaultvalue) {
            defaultfound = 1;
          }
        }
      }

      if (!defaultfound) {
        for (i = 0; item[i].identifier; i++) {
          if (item[i].identifier[0]) {
            eprop->defaultvalue = item[i].value;
            break;
          }
        }
      }

      break;
    }
    default:
      CLOG_ERROR(
          &LOG, "\"%s.%s\", invalid type for struct type.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_string_maxlength(PropertyRNA *prop, int maxlength)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      sprop->maxlength = maxlength;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not string.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_boolean_default(PropertyRNA *prop, bool value)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_BOOLEAN: {
      BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
      BLI_assert(ELEM(value, false, true));
#ifndef RNA_RUNTIME
      /* Default may be set from items. */
      if (bprop->defaultvalue) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      bprop->defaultvalue = value;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not boolean.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_boolean_array_default(PropertyRNA *prop, const bool *array)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_BOOLEAN: {
      BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
      bprop->defaultarray = array;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not boolean.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_int_default(PropertyRNA *prop, int value)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
#ifndef RNA_RUNTIME
      if (iprop->defaultvalue != 0) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      iprop->defaultvalue = value;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not int.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_int_array_default(PropertyRNA *prop, const int *array)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
#ifndef RNA_RUNTIME
      if (iprop->defaultarray != NULL) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      iprop->defaultarray = array;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not int.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_float_default(PropertyRNA *prop, float value)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
#ifndef RNA_RUNTIME
      if (fprop->defaultvalue != 0) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      fprop->defaultvalue = value;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not float.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}
void RNA_def_property_float_array_default(PropertyRNA *prop, const float *array)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
#ifndef RNA_RUNTIME
      if (fprop->defaultarray != NULL) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      fprop->defaultarray = array; /* WARNING, this array must not come from the stack and lost */
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not float.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_string_default(PropertyRNA *prop, const char *value)
{
  StructRNA *srna = DefRNA.laststruct;

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;

      if (value == NULL) {
        CLOG_ERROR(&LOG,
                   "\"%s.%s\", NULL string passed (don't call in this case).",
                   srna->identifier,
                   prop->identifier);
        DefRNA.error = true;
        break;
      }

      if (!value[0]) {
        CLOG_ERROR(&LOG,
                   "\"%s.%s\", empty string passed (don't call in this case).",
                   srna->identifier,
                   prop->identifier);
        DefRNA.error = true;
        // BLI_assert(0);
        break;
      }
#ifndef RNA_RUNTIME
      if (sprop->defaultvalue != NULL && sprop->defaultvalue[0]) {
        CLOG_ERROR(&LOG, "\"%s.%s\", set from DNA.", srna->identifier, prop->identifier);
      }
#endif
      sprop->defaultvalue = value;
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not string.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_enum_default(PropertyRNA *prop, int value)
{
  StructRNA *srna = DefRNA.laststruct;
  int i, defaultfound = 0;

  switch (prop->type) {
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      eprop->defaultvalue = value;

      if (prop->flag & PROP_ENUM_FLAG) {
        /* check all bits are accounted for */
        int totflag = 0;
        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].identifier[0]) {
            totflag |= eprop->item[i].value;
          }
        }

        if (eprop->defaultvalue & ~totflag) {
          CLOG_ERROR(&LOG,
                     "\"%s.%s\", default includes unused bits (%d).",
                     srna->identifier,
                     prop->identifier,
                     eprop->defaultvalue & ~totflag);
          DefRNA.error = true;
        }
      }
      else {
        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].identifier[0] && eprop->item[i].value == eprop->defaultvalue) {
            defaultfound = 1;
          }
        }

        if (!defaultfound && eprop->totitem) {
          if (value == 0) {
            eprop->defaultvalue = eprop->item[0].value;
          }
          else {
            CLOG_ERROR(
                &LOG, "\"%s.%s\", default is not in items.", srna->identifier, prop->identifier);
            DefRNA.error = true;
          }
        }
      }

      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not enum.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

/* SDNA */

static PropertyDefRNA *rna_def_property_sdna(PropertyRNA *prop,
                                             const char *structname,
                                             const char *propname)
{
  DNAStructMember smember;
  StructDefRNA *ds;
  PropertyDefRNA *dp;

  dp = rna_find_struct_property_def(DefRNA.laststruct, prop);
  if (dp == NULL) {
    return NULL;
  }

  ds = rna_find_struct_def((StructRNA *)dp->cont);

  if (!structname) {
    structname = ds->dnaname;
  }
  if (!propname) {
    propname = prop->identifier;
  }

  int dnaoffset = 0;
  if (!rna_find_sdna_member(DefRNA.sdna, structname, propname, &smember, &dnaoffset)) {
    if (DefRNA.silent) {
      return NULL;
    }
    if (!DefRNA.verify) {
      /* some basic values to survive even with sdna info */
      dp->dnastructname = structname;
      dp->dnaname = propname;
      if (prop->type == PROP_BOOLEAN) {
        dp->dnaarraylength = 1;
      }
      if (prop->type == PROP_POINTER) {
        dp->dnapointerlevel = 1;
      }
      dp->dnaoffset = smember.offset;
      return dp;
    }
    CLOG_ERROR(&LOG,
               "\"%s.%s\" (identifier \"%s\") not found. Struct must be in DNA.",
               structname,
               propname,
               prop->identifier);
    DefRNA.error = true;
    return NULL;
  }

  if (smember.arraylength > 1) {
    prop->arraylength[0] = smember.arraylength;
    prop->totarraylength = smember.arraylength;
    prop->arraydimension = 1;
  }
  else {
    prop->arraydimension = 0;
    prop->totarraylength = 0;
  }

  dp->dnastructname = structname;
  dp->dnastructfromname = ds->dnafromname;
  dp->dnastructfromprop = ds->dnafromprop;
  dp->dnaname = propname;
  dp->dnatype = smember.type;
  dp->dnaarraylength = smember.arraylength;
  dp->dnapointerlevel = smember.pointerlevel;
  dp->dnaoffset = smember.offset;
  dp->dnasize = smember.size;

  return dp;
}

void RNA_def_property_boolean_sdna(PropertyRNA *prop,
                                   const char *structname,
                                   const char *propname,
                                   int64_t bit)
{
  PropertyDefRNA *dp;
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_BOOLEAN) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not boolean.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {

    if (!DefRNA.silent) {
      /* error check to ensure floats are not wrapped as ints/bools */
      if (dp->dnatype && *dp->dnatype && IS_DNATYPE_BOOLEAN_COMPAT(dp->dnatype) == 0) {
        CLOG_ERROR(&LOG,
                   "%s.%s is a '%s' but wrapped as type '%s'.",
                   srna->identifier,
                   prop->identifier,
                   dp->dnatype,
                   RNA_property_typename(prop->type));
        DefRNA.error = true;
        return;
      }
    }

    dp->booleanbit = bit;

#ifndef RNA_RUNTIME
    /* Set the default if possible. */
    if (dp->dnaoffset != -1) {
      int SDNAnr = DNA_struct_find_nr_wrapper(DefRNA.sdna, dp->dnastructname);
      if (SDNAnr != -1) {
        const void *default_data = DNA_default_table[SDNAnr];
        if (default_data) {
          default_data = POINTER_OFFSET(default_data, dp->dnaoffset);
          bool has_default = true;
          if (prop->totarraylength > 0) {
            has_default = false;
            if (debugSRNA_defaults) {
              fprintf(stderr, "%s default: unsupported boolean array default\n", __func__);
            }
          }
          else {
            if (STREQ(dp->dnatype, "char")) {
              bprop->defaultvalue = *(const char *)default_data & bit;
            }
            else if (STREQ(dp->dnatype, "short")) {
              bprop->defaultvalue = *(const short *)default_data & bit;
            }
            else if (STREQ(dp->dnatype, "int")) {
              bprop->defaultvalue = *(const int *)default_data & bit;
            }
            else {
              has_default = false;
              if (debugSRNA_defaults) {
                fprintf(
                    stderr, "%s default: unsupported boolean type (%s)\n", __func__, dp->dnatype);
              }
            }

            if (has_default) {
              if (dp->booleannegative) {
                bprop->defaultvalue = !bprop->defaultvalue;
              }

              if (debugSRNA_defaults) {
                fprintf(stderr, "value=%d, ", bprop->defaultvalue);
                print_default_info(dp);
              }
            }
          }
        }
      }
    }
#else
    UNUSED_VARS(bprop);
#endif
  }
}

void RNA_def_property_boolean_negative_sdna(PropertyRNA *prop,
                                            const char *structname,
                                            const char *propname,
                                            int64_t booleanbit)
{
  PropertyDefRNA *dp;

  RNA_def_property_boolean_sdna(prop, structname, propname, booleanbit);

  dp = rna_find_struct_property_def(DefRNA.laststruct, prop);

  if (dp) {
    dp->booleannegative = true;
  }
}

void RNA_def_property_int_sdna(PropertyRNA *prop, const char *structname, const char *propname)
{
  PropertyDefRNA *dp;
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_INT) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not int.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {

    /* error check to ensure floats are not wrapped as ints/bools */
    if (!DefRNA.silent) {
      if (dp->dnatype && *dp->dnatype && IS_DNATYPE_INT_COMPAT(dp->dnatype) == 0) {
        CLOG_ERROR(&LOG,
                   "%s.%s is a '%s' but wrapped as type '%s'.",
                   srna->identifier,
                   prop->identifier,
                   dp->dnatype,
                   RNA_property_typename(prop->type));
        DefRNA.error = true;
        return;
      }
    }

    /* SDNA doesn't pass us unsigned unfortunately. */
    if (dp->dnatype && STREQ(dp->dnatype, "char")) {
      iprop->hardmin = iprop->softmin = CHAR_MIN;
      iprop->hardmax = iprop->softmax = CHAR_MAX;
    }
    else if (dp->dnatype && STREQ(dp->dnatype, "short")) {
      iprop->hardmin = iprop->softmin = SHRT_MIN;
      iprop->hardmax = iprop->softmax = SHRT_MAX;
    }
    else if (dp->dnatype && STREQ(dp->dnatype, "int")) {
      iprop->hardmin = INT_MIN;
      iprop->hardmax = INT_MAX;

      iprop->softmin = -10000; /* rather arbitrary. */
      iprop->softmax = 10000;
    }
    else if (dp->dnatype && STREQ(dp->dnatype, "int8_t")) {
      iprop->hardmin = iprop->softmin = INT8_MIN;
      iprop->hardmax = iprop->softmax = INT8_MAX;
    }

    if (ELEM(prop->subtype, PROP_UNSIGNED, PROP_PERCENTAGE, PROP_FACTOR)) {
      iprop->hardmin = iprop->softmin = 0;
    }

#ifndef RNA_RUNTIME
    /* Set the default if possible. */
    if (dp->dnaoffset != -1) {
      int SDNAnr = DNA_struct_find_nr_wrapper(DefRNA.sdna, dp->dnastructname);
      if (SDNAnr != -1) {
        const void *default_data = DNA_default_table[SDNAnr];
        if (default_data) {
          default_data = POINTER_OFFSET(default_data, dp->dnaoffset);
          /* NOTE: Currently doesn't store sign, assume chars are unsigned because
           * we build with this enabled, otherwise check 'PROP_UNSIGNED'. */
          bool has_default = true;
          if (prop->totarraylength > 0) {
            const void *default_data_end = POINTER_OFFSET(default_data, dp->dnasize);
            const int size_final = sizeof(int) * prop->totarraylength;
            if (STREQ(dp->dnatype, "char")) {
              int *defaultarray = rna_calloc(size_final);
              for (int i = 0; i < prop->totarraylength && default_data < default_data_end; i++) {
                defaultarray[i] = *(const char *)default_data;
                default_data = POINTER_OFFSET(default_data, sizeof(char));
              }
              iprop->defaultarray = defaultarray;
            }
            else if (STREQ(dp->dnatype, "short")) {

              int *defaultarray = rna_calloc(size_final);
              for (int i = 0; i < prop->totarraylength && default_data < default_data_end; i++) {
                defaultarray[i] = (prop->subtype != PROP_UNSIGNED) ? *(const short *)default_data :
                                                                     *(const ushort *)default_data;
                default_data = POINTER_OFFSET(default_data, sizeof(short));
              }
              iprop->defaultarray = defaultarray;
            }
            else if (STREQ(dp->dnatype, "int")) {
              int *defaultarray = rna_calloc(size_final);
              memcpy(defaultarray, default_data, MIN2(size_final, dp->dnasize));
              iprop->defaultarray = defaultarray;
            }
            else {
              has_default = false;
              if (debugSRNA_defaults) {
                fprintf(stderr,
                        "%s default: unsupported int array type (%s)\n",
                        __func__,
                        dp->dnatype);
              }
            }

            if (has_default) {
              if (debugSRNA_defaults) {
                fprintf(stderr, "value=(");
                for (int i = 0; i < prop->totarraylength; i++) {
                  fprintf(stderr, "%d, ", iprop->defaultarray[i]);
                }
                fprintf(stderr, "), ");
                print_default_info(dp);
              }
            }
          }
          else {
            if (STREQ(dp->dnatype, "char")) {
              iprop->defaultvalue = *(const char *)default_data;
            }
            else if (STREQ(dp->dnatype, "short")) {
              iprop->defaultvalue = (prop->subtype != PROP_UNSIGNED) ?
                                        *(const short *)default_data :
                                        *(const ushort *)default_data;
            }
            else if (STREQ(dp->dnatype, "int")) {
              iprop->defaultvalue = (prop->subtype != PROP_UNSIGNED) ? *(const int *)default_data :
                                                                       *(const uint *)default_data;
            }
            else {
              has_default = false;
              if (debugSRNA_defaults) {
                fprintf(stderr, "%s default: unsupported int type (%s)\n", __func__, dp->dnatype);
              }
            }

            if (has_default) {
              if (debugSRNA_defaults) {
                fprintf(stderr, "value=%d, ", iprop->defaultvalue);
                print_default_info(dp);
              }
            }
          }
        }
      }
    }
#endif
  }
}

void RNA_def_property_float_sdna(PropertyRNA *prop, const char *structname, const char *propname)
{
  PropertyDefRNA *dp;
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_FLOAT) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not float.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {
    /* silent is for internal use */
    if (!DefRNA.silent) {
      if (dp->dnatype && *dp->dnatype && IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0) {
        /* Colors are an exception. these get translated. */
        if (prop->subtype != PROP_COLOR_GAMMA) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     srna->identifier,
                     prop->identifier,
                     dp->dnatype,
                     RNA_property_typename(prop->type));
          DefRNA.error = true;
          return;
        }
      }
    }

    if (dp->dnatype && STREQ(dp->dnatype, "char")) {
      fprop->hardmin = fprop->softmin = 0.0f;
      fprop->hardmax = fprop->softmax = 1.0f;
    }

#ifndef RNA_RUNTIME
    /* Set the default if possible. */
    if (dp->dnaoffset != -1) {
      int SDNAnr = DNA_struct_find_nr_wrapper(DefRNA.sdna, dp->dnastructname);
      if (SDNAnr != -1) {
        const void *default_data = DNA_default_table[SDNAnr];
        if (default_data) {
          default_data = POINTER_OFFSET(default_data, dp->dnaoffset);
          bool has_default = true;
          if (prop->totarraylength > 0) {
            if (STREQ(dp->dnatype, "float")) {
              const int size_final = sizeof(float) * prop->totarraylength;
              float *defaultarray = rna_calloc(size_final);
              memcpy(defaultarray, default_data, MIN2(size_final, dp->dnasize));
              fprop->defaultarray = defaultarray;
            }
            else {
              has_default = false;
              if (debugSRNA_defaults) {
                fprintf(stderr,
                        "%s default: unsupported float array type (%s)\n",
                        __func__,
                        dp->dnatype);
              }
            }

            if (has_default) {
              if (debugSRNA_defaults) {
                fprintf(stderr, "value=(");
                for (int i = 0; i < prop->totarraylength; i++) {
                  fprintf(stderr, "%g, ", fprop->defaultarray[i]);
                }
                fprintf(stderr, "), ");
                print_default_info(dp);
              }
            }
          }
          else {
            if (STREQ(dp->dnatype, "float")) {
              fprop->defaultvalue = *(const float *)default_data;
            }
            else if (STREQ(dp->dnatype, "char")) {
              fprop->defaultvalue = (float)*(const char *)default_data * (1.0f / 255.0f);
            }
            else {
              has_default = false;
              if (debugSRNA_defaults) {
                fprintf(
                    stderr, "%s default: unsupported float type (%s)\n", __func__, dp->dnatype);
              }
            }

            if (has_default) {
              if (debugSRNA_defaults) {
                fprintf(stderr, "value=%g, ", fprop->defaultvalue);
                print_default_info(dp);
              }
            }
          }
        }
      }
    }
#endif
  }

  rna_def_property_sdna(prop, structname, propname);
}

void RNA_def_property_enum_sdna(PropertyRNA *prop, const char *structname, const char *propname)
{
  PropertyDefRNA *dp;
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_ENUM) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not enum.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {
    if (prop->arraydimension) {
      prop->arraydimension = 0;
      prop->totarraylength = 0;

      if (!DefRNA.silent) {
        CLOG_ERROR(&LOG, "\"%s.%s\", array not supported for enum type.", structname, propname);
        DefRNA.error = true;
      }
    }

#ifndef RNA_RUNTIME
    /* Set the default if possible. */
    if (dp->dnaoffset != -1) {
      int SDNAnr = DNA_struct_find_nr_wrapper(DefRNA.sdna, dp->dnastructname);
      if (SDNAnr != -1) {
        const void *default_data = DNA_default_table[SDNAnr];
        if (default_data) {
          default_data = POINTER_OFFSET(default_data, dp->dnaoffset);
          bool has_default = true;
          if (STREQ(dp->dnatype, "char")) {
            eprop->defaultvalue = *(const char *)default_data;
          }
          else if (STREQ(dp->dnatype, "short")) {
            eprop->defaultvalue = *(const short *)default_data;
          }
          else if (STREQ(dp->dnatype, "int")) {
            eprop->defaultvalue = *(const int *)default_data;
          }
          else {
            has_default = false;
            if (debugSRNA_defaults) {
              fprintf(stderr, "%s default: unsupported enum type (%s)\n", __func__, dp->dnatype);
            }
          }

          if (has_default) {
            if (debugSRNA_defaults) {
              fprintf(stderr, "value=%d, ", eprop->defaultvalue);
              print_default_info(dp);
            }
          }
        }
      }
    }
#else
    UNUSED_VARS(eprop);
#endif
  }
}

void RNA_def_property_enum_bitflag_sdna(PropertyRNA *prop,
                                        const char *structname,
                                        const char *propname)
{
  PropertyDefRNA *dp;

  RNA_def_property_enum_sdna(prop, structname, propname);

  dp = rna_find_struct_property_def(DefRNA.laststruct, prop);

  if (dp) {
    dp->enumbitflags = 1;

#ifndef RNA_RUNTIME
    int defaultvalue_mask = 0;
    EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
    for (int i = 0; i < eprop->totitem; i++) {
      if (eprop->item[i].identifier[0]) {
        defaultvalue_mask |= eprop->defaultvalue & eprop->item[i].value;
      }
    }
    eprop->defaultvalue = defaultvalue_mask;
#endif
  }
}

void RNA_def_property_string_sdna(PropertyRNA *prop, const char *structname, const char *propname)
{
  PropertyDefRNA *dp;
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_STRING) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not string.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {
    if (prop->arraydimension) {
      sprop->maxlength = prop->totarraylength;
      prop->arraydimension = 0;
      prop->totarraylength = 0;
    }

#ifndef RNA_RUNTIME
    /* Set the default if possible. */
    if ((dp->dnaoffset != -1) && (dp->dnapointerlevel != 0)) {
      int SDNAnr = DNA_struct_find_nr_wrapper(DefRNA.sdna, dp->dnastructname);
      if (SDNAnr != -1) {
        const void *default_data = DNA_default_table[SDNAnr];
        if (default_data) {
          default_data = POINTER_OFFSET(default_data, dp->dnaoffset);
          sprop->defaultvalue = default_data;

          if (debugSRNA_defaults) {
            fprintf(stderr, "value=\"%s\", ", sprop->defaultvalue);
            print_default_info(dp);
          }
        }
      }
    }
#endif
  }
}

void RNA_def_property_pointer_sdna(PropertyRNA *prop, const char *structname, const char *propname)
{
  /* PropertyDefRNA *dp; */
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_POINTER) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not pointer.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((/* dp= */ rna_def_property_sdna(prop, structname, propname))) {
    if (prop->arraydimension) {
      prop->arraydimension = 0;
      prop->totarraylength = 0;

      if (!DefRNA.silent) {
        CLOG_ERROR(&LOG, "\"%s.%s\", array not supported for pointer type.", structname, propname);
        DefRNA.error = true;
      }
    }
  }
}

void RNA_def_property_collection_sdna(PropertyRNA *prop,
                                      const char *structname,
                                      const char *propname,
                                      const char *lengthpropname)
{
  PropertyDefRNA *dp;
  CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (prop->type != PROP_COLLECTION) {
    CLOG_ERROR(&LOG, "\"%s.%s\", type is not collection.", srna->identifier, prop->identifier);
    DefRNA.error = true;
    return;
  }

  if ((dp = rna_def_property_sdna(prop, structname, propname))) {
    if (prop->arraydimension && !lengthpropname) {
      prop->arraydimension = 0;
      prop->totarraylength = 0;

      if (!DefRNA.silent) {
        CLOG_ERROR(&LOG, "\"%s.%s\", array of collections not supported.", structname, propname);
        DefRNA.error = true;
      }
    }

    if (dp->dnatype && STREQ(dp->dnatype, "ListBase")) {
      cprop->next = (PropCollectionNextFunc) "rna_iterator_listbase_next";
      cprop->get = (PropCollectionGetFunc) "rna_iterator_listbase_get";
      cprop->end = (PropCollectionEndFunc) "rna_iterator_listbase_end";
    }
  }

  if (dp && lengthpropname) {
    DNAStructMember smember;
    StructDefRNA *ds = rna_find_struct_def((StructRNA *)dp->cont);

    if (!structname) {
      structname = ds->dnaname;
    }

    int dnaoffset = 0;
    if (lengthpropname[0] == 0 ||
        rna_find_sdna_member(DefRNA.sdna, structname, lengthpropname, &smember, &dnaoffset)) {
      if (lengthpropname[0] == 0) {
        dp->dnalengthfixed = prop->totarraylength;
        prop->arraydimension = 0;
        prop->totarraylength = 0;
      }
      else {
        dp->dnalengthstructname = structname;
        dp->dnalengthname = lengthpropname;
        prop->totarraylength = 0;
      }

      cprop->next = (PropCollectionNextFunc) "rna_iterator_array_next";
      cprop->end = (PropCollectionEndFunc) "rna_iterator_array_end";

      if (dp->dnapointerlevel >= 2) {
        cprop->get = (PropCollectionGetFunc) "rna_iterator_array_dereference_get";
      }
      else {
        cprop->get = (PropCollectionGetFunc) "rna_iterator_array_get";
      }
    }
    else {
      if (!DefRNA.silent) {
        CLOG_ERROR(&LOG, "\"%s.%s\" not found.", structname, lengthpropname);
        DefRNA.error = true;
      }
    }
  }
}

void RNA_def_property_translation_context(PropertyRNA *prop, const char *context)
{
  prop->translation_context = context ? context : BLT_I18NCONTEXT_DEFAULT_BPYRNA;
}

/* Functions */

void RNA_def_property_editable_func(PropertyRNA *prop, const char *editable)
{
  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (editable) {
    prop->editable = (EditableFunc)editable;
  }
}

void RNA_def_property_editable_array_func(PropertyRNA *prop, const char *editable)
{
  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (editable) {
    prop->itemeditable = (ItemEditableFunc)editable;
  }
}

void RNA_def_property_override_funcs(PropertyRNA *prop,
                                     const char *diff,
                                     const char *store,
                                     const char *apply)
{
  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (diff) {
    prop->override_diff = (RNAPropOverrideDiff)diff;
  }
  if (store) {
    prop->override_store = (RNAPropOverrideStore)store;
  }
  if (apply) {
    prop->override_apply = (RNAPropOverrideApply)apply;
  }
}

void RNA_def_property_update(PropertyRNA *prop, int noteflag, const char *func)
{
  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  prop->noteflag = noteflag;
  prop->update = (UpdateFunc)func;
}

void RNA_def_property_update_runtime(PropertyRNA *prop, const void *func)
{
  prop->update = (void *)func;
}

void RNA_def_property_poll_runtime(PropertyRNA *prop, const void *func)
{
  if (prop->type == PROP_POINTER) {
    ((PointerPropertyRNA *)prop)->poll = (void *)func;
  }
  else {
    CLOG_ERROR(&LOG, "%s is not a Pointer Property.", prop->identifier);
  }
}

void RNA_def_property_dynamic_array_funcs(PropertyRNA *prop, const char *getlength)
{
  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  if (!(prop->flag & PROP_DYNAMIC)) {
    CLOG_ERROR(&LOG, "property is a not dynamic array.");
    DefRNA.error = true;
    return;
  }

  if (getlength) {
    prop->getlength = (PropArrayLengthGetFunc)getlength;
  }
}
