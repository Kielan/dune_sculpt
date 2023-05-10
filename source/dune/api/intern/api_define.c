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
void RNA_def_property_boolean_funcs(PropertyRNA *prop, const char *get, const char *set)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_BOOLEAN: {
      BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;

      if (prop->arraydimension) {
        if (get) {
          bprop->getarray = (PropBooleanArrayGetFunc)get;
        }
        if (set) {
          bprop->setarray = (PropBooleanArraySetFunc)set;
        }
      }
      else {
        if (get) {
          bprop->get = (PropBooleanGetFunc)get;
        }
        if (set) {
          bprop->set = (PropBooleanSetFunc)set;
        }
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not boolean.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_boolean_funcs_runtime(PropertyRNA *prop,
                                            BooleanPropertyGetFunc getfunc,
                                            BooleanPropertySetFunc setfunc)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;

  if (getfunc) {
    bprop->get_ex = getfunc;
  }
  if (setfunc) {
    bprop->set_ex = setfunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_boolean_array_funcs_runtime(PropertyRNA *prop,
                                                  BooleanArrayPropertyGetFunc getfunc,
                                                  BooleanArrayPropertySetFunc setfunc)
{
  BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;

  if (getfunc) {
    bprop->getarray_ex = getfunc;
  }
  if (setfunc) {
    bprop->setarray_ex = setfunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_int_funcs(PropertyRNA *prop,
                                const char *get,
                                const char *set,
                                const char *range)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

      if (prop->arraydimension) {
        if (get) {
          iprop->getarray = (PropIntArrayGetFunc)get;
        }
        if (set) {
          iprop->setarray = (PropIntArraySetFunc)set;
        }
      }
      else {
        if (get) {
          iprop->get = (PropIntGetFunc)get;
        }
        if (set) {
          iprop->set = (PropIntSetFunc)set;
        }
      }
      if (range) {
        iprop->range = (PropIntRangeFunc)range;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not int.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_int_funcs_runtime(PropertyRNA *prop,
                                        IntPropertyGetFunc getfunc,
                                        IntPropertySetFunc setfunc,
                                        IntPropertyRangeFunc rangefunc)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

  if (getfunc) {
    iprop->get_ex = getfunc;
  }
  if (setfunc) {
    iprop->set_ex = setfunc;
  }
  if (rangefunc) {
    iprop->range_ex = rangefunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_int_array_funcs_runtime(PropertyRNA *prop,
                                              IntArrayPropertyGetFunc getfunc,
                                              IntArrayPropertySetFunc setfunc,
                                              IntPropertyRangeFunc rangefunc)
{
  IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

  if (getfunc) {
    iprop->getarray_ex = getfunc;
  }
  if (setfunc) {
    iprop->setarray_ex = setfunc;
  }
  if (rangefunc) {
    iprop->range_ex = rangefunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_float_funcs(PropertyRNA *prop,
                                  const char *get,
                                  const char *set,
                                  const char *range)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

      if (prop->arraydimension) {
        if (get) {
          fprop->getarray = (PropFloatArrayGetFunc)get;
        }
        if (set) {
          fprop->setarray = (PropFloatArraySetFunc)set;
        }
      }
      else {
        if (get) {
          fprop->get = (PropFloatGetFunc)get;
        }
        if (set) {
          fprop->set = (PropFloatSetFunc)set;
        }
      }
      if (range) {
        fprop->range = (PropFloatRangeFunc)range;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not float.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_float_funcs_runtime(PropertyRNA *prop,
                                          FloatPropertyGetFunc getfunc,
                                          FloatPropertySetFunc setfunc,
                                          FloatPropertyRangeFunc rangefunc)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

  if (getfunc) {
    fprop->get_ex = getfunc;
  }
  if (setfunc) {
    fprop->set_ex = setfunc;
  }
  if (rangefunc) {
    fprop->range_ex = rangefunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_float_array_funcs_runtime(PropertyRNA *prop,
                                                FloatArrayPropertyGetFunc getfunc,
                                                FloatArrayPropertySetFunc setfunc,
                                                FloatPropertyRangeFunc rangefunc)
{
  FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

  if (getfunc) {
    fprop->getarray_ex = getfunc;
  }
  if (setfunc) {
    fprop->setarray_ex = setfunc;
  }
  if (rangefunc) {
    fprop->range_ex = rangefunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_enum_funcs(PropertyRNA *prop,
                                 const char *get,
                                 const char *set,
                                 const char *item)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;

      if (get) {
        eprop->get = (PropEnumGetFunc)get;
      }
      if (set) {
        eprop->set = (PropEnumSetFunc)set;
      }
      if (item) {
        eprop->item_fn = (PropEnumItemFunc)item;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not enum.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_enum_funcs_runtime(PropertyRNA *prop,
                                         EnumPropertyGetFunc getfunc,
                                         EnumPropertySetFunc setfunc,
                                         EnumPropertyItemFunc itemfunc)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;

  if (getfunc) {
    eprop->get_ex = getfunc;
  }
  if (setfunc) {
    eprop->set_ex = setfunc;
  }
  if (itemfunc) {
    eprop->item_fn = itemfunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_string_funcs(PropertyRNA *prop,
                                   const char *get,
                                   const char *length,
                                   const char *set)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;

      if (get) {
        sprop->get = (PropStringGetFunc)get;
      }
      if (length) {
        sprop->length = (PropStringLengthFunc)length;
      }
      if (set) {
        sprop->set = (PropStringSetFunc)set;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not string.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_string_funcs_runtime(PropertyRNA *prop,
                                           StringPropertyGetFunc getfunc,
                                           StringPropertyLengthFunc lengthfunc,
                                           StringPropertySetFunc setfunc)
{
  StringPropertyRNA *sprop = (StringPropertyRNA *)prop;

  if (getfunc) {
    sprop->get_ex = getfunc;
  }
  if (lengthfunc) {
    sprop->length_ex = lengthfunc;
  }
  if (setfunc) {
    sprop->set_ex = setfunc;
  }

  if (getfunc || setfunc) {
    /* don't save in id properties */
    prop->flag &= ~PROP_IDPROPERTY;

    if (!setfunc) {
      prop->flag &= ~PROP_EDITABLE;
    }
  }
}

void RNA_def_property_pointer_funcs(
    PropertyRNA *prop, const char *get, const char *set, const char *type_fn, const char *poll)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;

      if (get) {
        pprop->get = (PropPointerGetFunc)get;
      }
      if (set) {
        pprop->set = (PropPointerSetFunc)set;
      }
      if (type_fn) {
        pprop->type_fn = (PropPointerTypeFunc)type_fn;
      }
      if (poll) {
        pprop->poll = (PropPointerPollFunc)poll;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not pointer.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_collection_funcs(PropertyRNA *prop,
                                       const char *begin,
                                       const char *next,
                                       const char *end,
                                       const char *get,
                                       const char *length,
                                       const char *lookupint,
                                       const char *lookupstring,
                                       const char *assignint)
{
  StructRNA *srna = DefRNA.laststruct;

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only during preprocessing.");
    return;
  }

  switch (prop->type) {
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;

      if (begin) {
        cprop->begin = (PropCollectionBeginFunc)begin;
      }
      if (next) {
        cprop->next = (PropCollectionNextFunc)next;
      }
      if (end) {
        cprop->end = (PropCollectionEndFunc)end;
      }
      if (get) {
        cprop->get = (PropCollectionGetFunc)get;
      }
      if (length) {
        cprop->length = (PropCollectionLengthFunc)length;
      }
      if (lookupint) {
        cprop->lookupint = (PropCollectionLookupIntFunc)lookupint;
      }
      if (lookupstring) {
        cprop->lookupstring = (PropCollectionLookupStringFunc)lookupstring;
      }
      if (assignint) {
        cprop->assignint = (PropCollectionAssignIntFunc)assignint;
      }
      break;
    }
    default:
      CLOG_ERROR(&LOG, "\"%s.%s\", type is not collection.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      break;
  }
}

void RNA_def_property_srna(PropertyRNA *prop, const char *type)
{
  char error[512];
  if (rna_validate_identifier(type, error, false) == 0) {
    CLOG_ERROR(&LOG, "struct identifier \"%s\" error - %s", type, error);
    DefRNA.error = true;
    return;
  }

  prop->srna = (StructRNA *)type;
}

void RNA_def_py_data(PropertyRNA *prop, void *py_data)
{
  prop->py_data = py_data;
}

/* Compact definitions */

PropertyRNA *RNA_def_boolean(StructOrFunctionRNA *cont_,
                             const char *identifier,
                             bool default_value,
                             const char *ui_name,
                             const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_default(prop, default_value);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_boolean_array(StructOrFunctionRNA *cont_,
                                   const char *identifier,
                                   int len,
                                   bool *default_value,
                                   const char *ui_name,
                                   const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_BOOLEAN, PROP_NONE);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_boolean_array_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_boolean_layer(StructOrFunctionRNA *cont_,
                                   const char *identifier,
                                   int len,
                                   bool *default_value,
                                   const char *ui_name,
                                   const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_BOOLEAN, PROP_LAYER);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_boolean_array_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_boolean_layer_member(StructOrFunctionRNA *cont_,
                                          const char *identifier,
                                          int len,
                                          bool *default_value,
                                          const char *ui_name,
                                          const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_BOOLEAN, PROP_LAYER_MEMBER);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_boolean_array_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_boolean_vector(StructOrFunctionRNA *cont_,
                                    const char *identifier,
                                    int len,
                                    bool *default_value,
                                    const char *ui_name,
                                    const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_BOOLEAN, PROP_XYZ); /* XXX */
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_boolean_array_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_int(StructOrFunctionRNA *cont_,
                         const char *identifier,
                         int default_value,
                         int hardmin,
                         int hardmax,
                         const char *ui_name,
                         const char *ui_description,
                         int softmin,
                         int softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_INT, PROP_NONE);
  RNA_def_property_int_default(prop, default_value);
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_int_vector(StructOrFunctionRNA *cont_,
                                const char *identifier,
                                int len,
                                const int *default_value,
                                int hardmin,
                                int hardmax,
                                const char *ui_name,
                                const char *ui_description,
                                int softmin,
                                int softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_INT, PROP_XYZ); /* XXX */
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_int_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_int_array(StructOrFunctionRNA *cont_,
                               const char *identifier,
                               int len,
                               const int *default_value,
                               int hardmin,
                               int hardmax,
                               const char *ui_name,
                               const char *ui_description,
                               int softmin,
                               int softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_INT, PROP_NONE);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_int_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_string(StructOrFunctionRNA *cont_,
                            const char *identifier,
                            const char *default_value,
                            int maxlen,
                            const char *ui_name,
                            const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  BLI_assert(default_value == NULL || default_value[0]);

  prop = RNA_def_property(cont, identifier, PROP_STRING, PROP_NONE);
  if (maxlen != 0) {
    RNA_def_property_string_maxlength(prop, maxlen);
  }
  if (default_value) {
    RNA_def_property_string_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_string_file_path(StructOrFunctionRNA *cont_,
                                      const char *identifier,
                                      const char *default_value,
                                      int maxlen,
                                      const char *ui_name,
                                      const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  BLI_assert(default_value == NULL || default_value[0]);

  prop = RNA_def_property(cont, identifier, PROP_STRING, PROP_FILEPATH);
  if (maxlen != 0) {
    RNA_def_property_string_maxlength(prop, maxlen);
  }
  if (default_value) {
    RNA_def_property_string_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_string_dir_path(StructOrFunctionRNA *cont_,
                                     const char *identifier,
                                     const char *default_value,
                                     int maxlen,
                                     const char *ui_name,
                                     const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  BLI_assert(default_value == NULL || default_value[0]);

  prop = RNA_def_property(cont, identifier, PROP_STRING, PROP_DIRPATH);
  if (maxlen != 0) {
    RNA_def_property_string_maxlength(prop, maxlen);
  }
  if (default_value) {
    RNA_def_property_string_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_string_file_name(StructOrFunctionRNA *cont_,
                                      const char *identifier,
                                      const char *default_value,
                                      int maxlen,
                                      const char *ui_name,
                                      const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  BLI_assert(default_value == NULL || default_value[0]);

  prop = RNA_def_property(cont, identifier, PROP_STRING, PROP_FILENAME);
  if (maxlen != 0) {
    RNA_def_property_string_maxlength(prop, maxlen);
  }
  if (default_value) {
    RNA_def_property_string_default(prop, default_value);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_enum(StructOrFunctionRNA *cont_,
                          const char *identifier,
                          const EnumPropertyItem *items,
                          int default_value,
                          const char *ui_name,
                          const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  if (items == NULL) {
    CLOG_ERROR(&LOG, "items not allowed to be NULL.");
    return NULL;
  }

  prop = RNA_def_property(cont, identifier, PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, items);
  RNA_def_property_enum_default(prop, default_value);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_enum_flag(StructOrFunctionRNA *cont_,
                               const char *identifier,
                               const EnumPropertyItem *items,
                               int default_value,
                               const char *ui_name,
                               const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  if (items == NULL) {
    CLOG_ERROR(&LOG, "items not allowed to be NULL.");
    return NULL;
  }

  prop = RNA_def_property(cont, identifier, PROP_ENUM, PROP_NONE);
  RNA_def_property_flag(prop, PROP_ENUM_FLAG); /* important to run before default set */
  RNA_def_property_enum_items(prop, items);
  RNA_def_property_enum_default(prop, default_value);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

void RNA_def_enum_funcs(PropertyRNA *prop, EnumPropertyItemFunc itemfunc)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  eprop->item_fn = itemfunc;
}

PropertyRNA *RNA_def_float(StructOrFunctionRNA *cont_,
                           const char *identifier,
                           float default_value,
                           float hardmin,
                           float hardmax,
                           const char *ui_name,
                           const char *ui_description,
                           float softmin,
                           float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_default(prop, default_value);
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_vector(StructOrFunctionRNA *cont_,
                                  const char *identifier,
                                  int len,
                                  const float *default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_XYZ);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_float_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_vector_xyz(StructOrFunctionRNA *cont_,
                                      const char *identifier,
                                      int len,
                                      const float *default_value,
                                      float hardmin,
                                      float hardmax,
                                      const char *ui_name,
                                      const char *ui_description,
                                      float softmin,
                                      float softmax)
{
  PropertyRNA *prop;

  prop = RNA_def_float_vector(cont_,
                              identifier,
                              len,
                              default_value,
                              hardmin,
                              hardmax,
                              ui_name,
                              ui_description,
                              softmin,
                              softmax);
  prop->subtype = PROP_XYZ_LENGTH;

  return prop;
}

PropertyRNA *RNA_def_float_color(StructOrFunctionRNA *cont_,
                                 const char *identifier,
                                 int len,
                                 const float *default_value,
                                 float hardmin,
                                 float hardmax,
                                 const char *ui_name,
                                 const char *ui_description,
                                 float softmin,
                                 float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_COLOR);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_float_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_matrix(StructOrFunctionRNA *cont_,
                                  const char *identifier,
                                  int rows,
                                  int columns,
                                  const float *default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;
  const int length[2] = {rows, columns};

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_MATRIX);
  RNA_def_property_multi_array(prop, 2, length);
  if (default_value) {
    RNA_def_property_float_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_translation(StructOrFunctionRNA *cont_,
                                       const char *identifier,
                                       int len,
                                       const float *default_value,
                                       float hardmin,
                                       float hardmax,
                                       const char *ui_name,
                                       const char *ui_description,
                                       float softmin,
                                       float softmax)
{
  PropertyRNA *prop;

  prop = RNA_def_float_vector(cont_,
                              identifier,
                              len,
                              default_value,
                              hardmin,
                              hardmax,
                              ui_name,
                              ui_description,
                              softmin,
                              softmax);
  prop->subtype = PROP_TRANSLATION;

  RNA_def_property_ui_range(prop, softmin, softmax, 1, RNA_TRANSLATION_PREC_DEFAULT);

  return prop;
}

PropertyRNA *RNA_def_float_rotation(StructOrFunctionRNA *cont_,
                                    const char *identifier,
                                    int len,
                                    const float *default_value,
                                    float hardmin,
                                    float hardmax,
                                    const char *ui_name,
                                    const char *ui_description,
                                    float softmin,
                                    float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, (len >= 3) ? PROP_EULER : PROP_ANGLE);
  if (len != 0) {
    RNA_def_property_array(prop, len);
    if (default_value) {
      RNA_def_property_float_array_default(prop, default_value);
    }
  }
  else {
    /* RNA_def_property_float_default must be called outside */
    BLI_assert(default_value == NULL);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 10, 3);

  return prop;
}

PropertyRNA *RNA_def_float_distance(StructOrFunctionRNA *cont_,
                                    const char *identifier,
                                    float default_value,
                                    float hardmin,
                                    float hardmax,
                                    const char *ui_name,
                                    const char *ui_description,
                                    float softmin,
                                    float softmax)
{
  PropertyRNA *prop = RNA_def_float(cont_,
                                    identifier,
                                    default_value,
                                    hardmin,
                                    hardmax,
                                    ui_name,
                                    ui_description,
                                    softmin,
                                    softmax);
  RNA_def_property_subtype(prop, PROP_DISTANCE);

  return prop;
}

PropertyRNA *RNA_def_float_array(StructOrFunctionRNA *cont_,
                                 const char *identifier,
                                 int len,
                                 const float *default_value,
                                 float hardmin,
                                 float hardmax,
                                 const char *ui_name,
                                 const char *ui_description,
                                 float softmin,
                                 float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_NONE);
  if (len != 0) {
    RNA_def_property_array(prop, len);
  }
  if (default_value) {
    RNA_def_property_float_array_default(prop, default_value);
  }
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_percentage(StructOrFunctionRNA *cont_,
                                      const char *identifier,
                                      float default_value,
                                      float hardmin,
                                      float hardmax,
                                      const char *ui_name,
                                      const char *ui_description,
                                      float softmin,
                                      float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

#ifdef DEBUG
  /* Properties with PROP_PERCENTAGE should use a range like 0 to 100, unlike PROP_FACTOR. */
  if (hardmax < 2.0f) {
    CLOG_WARN(&LOG,
              "Percentage property with incorrect range: %s.%s",
              CONTAINER_RNA_ID(cont),
              identifier);
  }
#endif

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_PERCENTAGE);
  RNA_def_property_float_default(prop, default_value);
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_float_factor(StructOrFunctionRNA *cont_,
                                  const char *identifier,
                                  float default_value,
                                  float hardmin,
                                  float hardmax,
                                  const char *ui_name,
                                  const char *ui_description,
                                  float softmin,
                                  float softmax)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  ASSERT_SOFT_HARD_LIMITS;

  prop = RNA_def_property(cont, identifier, PROP_FLOAT, PROP_FACTOR);
  RNA_def_property_float_default(prop, default_value);
  if (hardmin != hardmax) {
    RNA_def_property_range(prop, hardmin, hardmax);
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);
  RNA_def_property_ui_range(prop, softmin, softmax, 1, 3);

  return prop;
}

PropertyRNA *RNA_def_pointer(StructOrFunctionRNA *cont_,
                             const char *identifier,
                             const char *type,
                             const char *ui_name,
                             const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, type);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_pointer_runtime(StructOrFunctionRNA *cont_,
                                     const char *identifier,
                                     StructRNA *type,
                                     const char *ui_name,
                                     const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_runtime(cont, prop, type);
  if ((type->flag & STRUCT_ID) != 0) {
    prop->flag |= PROP_EDITABLE;
  }
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_collection(StructOrFunctionRNA *cont_,
                                const char *identifier,
                                const char *type,
                                const char *ui_name,
                                const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_type(prop, type);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

PropertyRNA *RNA_def_collection_runtime(StructOrFunctionRNA *cont_,
                                        const char *identifier,
                                        StructRNA *type,
                                        const char *ui_name,
                                        const char *ui_description)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop;

  prop = RNA_def_property(cont, identifier, PROP_COLLECTION, PROP_NONE);
  RNA_def_property_struct_runtime(cont, prop, type);
  RNA_def_property_ui_text(prop, ui_name, ui_description);

  return prop;
}

/* Function */

static FunctionRNA *rna_def_function(StructRNA *srna, const char *identifier)
{
  FunctionRNA *func;
  StructDefRNA *dsrna;
  FunctionDefRNA *dfunc;

  if (DefRNA.preprocess) {
    char error[512];

    if (rna_validate_identifier(identifier, error, false) == 0) {
      CLOG_ERROR(&LOG, "function identifier \"%s\" - %s", identifier, error);
      DefRNA.error = true;
    }
  }

  func = MEM_callocN(sizeof(FunctionRNA), "FunctionRNA");
  func->identifier = identifier;
  func->description = identifier;

  rna_addtail(&srna->functions, func);

  if (DefRNA.preprocess) {
    dsrna = rna_find_struct_def(srna);
    dfunc = MEM_callocN(sizeof(FunctionDefRNA), "FunctionDefRNA");
    rna_addtail(&dsrna->functions, dfunc);
    dfunc->func = func;
  }
  else {
    func->flag |= FUNC_RUNTIME;
  }

  return func;
}

FunctionRNA *RNA_def_function(StructRNA *srna, const char *identifier, const char *call)
{
  FunctionRNA *func;
  FunctionDefRNA *dfunc;

  if (BLI_findstring_ptr(&srna->functions, identifier, offsetof(FunctionRNA, identifier))) {
    CLOG_ERROR(&LOG, "%s.%s already defined.", srna->identifier, identifier);
    return NULL;
  }

  func = rna_def_function(srna, identifier);

  if (!DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only at preprocess time.");
    return func;
  }

  dfunc = rna_find_function_def(func);
  dfunc->call = call;

  return func;
}

FunctionRNA *RNA_def_function_runtime(StructRNA *srna, const char *identifier, CallFunc call)
{
  FunctionRNA *func;

  func = rna_def_function(srna, identifier);

  if (DefRNA.preprocess) {
    CLOG_ERROR(&LOG, "only at runtime.");
    return func;
  }

  func->call = call;

  return func;
}

void RNA_def_function_return(FunctionRNA *func, PropertyRNA *ret)
{
  if (ret->flag & PROP_DYNAMIC) {
    CLOG_ERROR(&LOG,
               "\"%s.%s\", dynamic values are not allowed as strict returns, "
               "use RNA_def_function_output instead.",
               func->identifier,
               ret->identifier);
    return;
  }
  if (ret->arraydimension) {
    CLOG_ERROR(&LOG,
               "\"%s.%s\", arrays are not allowed as strict returns, "
               "use RNA_def_function_output instead.",
               func->identifier,
               ret->identifier);
    return;
  }

  BLI_assert(func->c_ret == NULL);
  func->c_ret = ret;

  RNA_def_function_output(func, ret);
}

void RNA_def_function_output(FunctionRNA *UNUSED(func), PropertyRNA *ret)
{
  ret->flag_parameter |= PARM_OUTPUT;
}

void RNA_def_function_flag(FunctionRNA *func, int flag)
{
  func->flag |= flag;
}

void RNA_def_function_ui_description(FunctionRNA *func, const char *description)
{
  func->description = description;
}

int rna_parameter_size(PropertyRNA *parm)
{
  PropertyType ptype = parm->type;
  int len = parm->totarraylength;

  /* XXX in other parts is mentioned that strings can be dynamic as well */
  if (parm->flag & PROP_DYNAMIC) {
    return sizeof(ParameterDynAlloc);
  }

  if (len > 0) {
    switch (ptype) {
      case PROP_BOOLEAN:
        return sizeof(bool) * len;
      case PROP_INT:
        return sizeof(int) * len;
      case PROP_FLOAT:
        return sizeof(float) * len;
      default:
        break;
    }
  }
  else {
    switch (ptype) {
      case PROP_BOOLEAN:
        return sizeof(bool);
      case PROP_INT:
      case PROP_ENUM:
        return sizeof(int);
      case PROP_FLOAT:
        return sizeof(float);
      case PROP_STRING:
        /* return values don't store a pointer to the original */
        if (parm->flag & PROP_THICK_WRAP) {
          StringPropertyRNA *sparm = (StringPropertyRNA *)parm;
          return sizeof(char) * sparm->maxlength;
        }
        else {
          return sizeof(char *);
        }
      case PROP_POINTER: {
#ifdef RNA_RUNTIME
        if (parm->flag_parameter & PARM_RNAPTR) {
          if (parm->flag & PROP_THICK_WRAP) {
            return sizeof(PointerRNA);
          }
          else {
            return sizeof(PointerRNA *);
          }
        }
        else {
          return sizeof(void *);
        }
#else
        if (parm->flag_parameter & PARM_RNAPTR) {
          if (parm->flag & PROP_THICK_WRAP) {
            return sizeof(PointerRNA);
          }
          return sizeof(PointerRNA *);
        }
        return sizeof(void *);

#endif
      }
      case PROP_COLLECTION:
        return sizeof(ListBase);
    }
  }

  return sizeof(void *);
}

/* Dynamic Enums */

void RNA_enum_item_add(EnumPropertyItem **items, int *totitem, const EnumPropertyItem *item)
{
  int tot = *totitem;

  if (tot == 0) {
    *items = MEM_callocN(sizeof(EnumPropertyItem[8]), __func__);
    /* Ensure we get crashes on missing calls to 'RNA_enum_item_end', see T74227. */
#ifdef DEBUG
    memset(*items, 0xff, sizeof(EnumPropertyItem[8]));
#endif
  }
  else if (tot >= 8 && (tot & (tot - 1)) == 0) {
    /* power of two > 8 */
    *items = MEM_recallocN_id(*items, sizeof(EnumPropertyItem) * tot * 2, __func__);
#ifdef DEBUG
    memset((*items) + tot, 0xff, sizeof(EnumPropertyItem) * tot);
#endif
  }

  (*items)[tot] = *item;
  *totitem = tot + 1;
}

void RNA_enum_item_add_separator(EnumPropertyItem **items, int *totitem)
{
  static const EnumPropertyItem sepr = {0, "", 0, NULL, NULL};
  RNA_enum_item_add(items, totitem, &sepr);
}

void RNA_enum_items_add(EnumPropertyItem **items, int *totitem, const EnumPropertyItem *item)
{
  for (; item->identifier; item++) {
    RNA_enum_item_add(items, totitem, item);
  }
}

void RNA_enum_items_add_value(EnumPropertyItem **items,
                              int *totitem,
                              const EnumPropertyItem *item,
                              int value)
{
  for (; item->identifier; item++) {
    if (item->value == value) {
      RNA_enum_item_add(items, totitem, item);
      /* break on first match - does this break anything?
       * (is quick hack to get object->parent_type working ok for armature/lattice) */
      break;
    }
  }
}

void RNA_enum_item_end(EnumPropertyItem **items, int *totitem)
{
  static const EnumPropertyItem empty = {0, NULL, 0, NULL, NULL};
  RNA_enum_item_add(items, totitem, &empty);
}

/* Memory management */

#ifdef RNA_RUNTIME
void RNA_def_struct_duplicate_pointers(BlenderRNA *brna, StructRNA *srna)
{
  if (srna->identifier) {
    srna->identifier = BLI_strdup(srna->identifier);
    if (srna->flag & STRUCT_PUBLIC_NAMESPACE) {
      BLI_ghash_replace_key(brna->structs_map, (void *)srna->identifier);
    }
  }
  if (srna->name) {
    srna->name = BLI_strdup(srna->name);
  }
  if (srna->description) {
    srna->description = BLI_strdup(srna->description);
  }

  srna->flag |= STRUCT_FREE_POINTERS;
}

void RNA_def_struct_free_pointers(BlenderRNA *brna, StructRNA *srna)
{
  if (srna->flag & STRUCT_FREE_POINTERS) {
    if (srna->identifier) {
      if (srna->flag & STRUCT_PUBLIC_NAMESPACE) {
        if (brna != NULL) {
          BLI_ghash_remove(brna->structs_map, (void *)srna->identifier, NULL, NULL);
        }
      }
      MEM_freeN((void *)srna->identifier);
    }
    if (srna->name) {
      MEM_freeN((void *)srna->name);
    }
    if (srna->description) {
      MEM_freeN((void *)srna->description);
    }
  }
}

void RNA_def_func_duplicate_pointers(FunctionRNA *func)
{
  if (func->identifier) {
    func->identifier = BLI_strdup(func->identifier);
  }
  if (func->description) {
    func->description = BLI_strdup(func->description);
  }

  func->flag |= FUNC_FREE_POINTERS;
}

void RNA_def_func_free_pointers(FunctionRNA *func)
{
  if (func->flag & FUNC_FREE_POINTERS) {
    if (func->identifier) {
      MEM_freeN((void *)func->identifier);
    }
    if (func->description) {
      MEM_freeN((void *)func->description);
    }
  }
}

void RNA_def_property_duplicate_pointers(StructOrFunctionRNA *cont_, PropertyRNA *prop)
{
  ContainerRNA *cont = cont_;
  int a;

  /* annoying since we just added this to a hash, could make this add the correct key to the hash
   * in the first place */
  if (prop->identifier) {
    if (cont->prophash) {
      prop->identifier = BLI_strdup(prop->identifier);
      BLI_ghash_reinsert(cont->prophash, (void *)prop->identifier, prop, NULL, NULL);
    }
    else {
      prop->identifier = BLI_strdup(prop->identifier);
    }
  }

  if (prop->name) {
    prop->name = BLI_strdup(prop->name);
  }
  if (prop->description) {
    prop->description = BLI_strdup(prop->description);
  }

  switch (prop->type) {
    case PROP_BOOLEAN: {
      BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;

      if (bprop->defaultarray) {
        bool *array = MEM_mallocN(sizeof(bool) * prop->totarraylength, "RNA_def_property_store");
        memcpy(array, bprop->defaultarray, sizeof(bool) * prop->totarraylength);
        bprop->defaultarray = array;
      }
      break;
    }
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

      if (iprop->defaultarray) {
        int *array = MEM_mallocN(sizeof(int) * prop->totarraylength, "RNA_def_property_store");
        memcpy(array, iprop->defaultarray, sizeof(int) * prop->totarraylength);
        iprop->defaultarray = array;
      }
      break;
    }
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;

      if (eprop->item) {
        EnumPropertyItem *array = MEM_mallocN(sizeof(EnumPropertyItem) * (eprop->totitem + 1),
                                              "RNA_def_property_store");
        memcpy(array, eprop->item, sizeof(EnumPropertyItem) * (eprop->totitem + 1));
        eprop->item = array;

        for (a = 0; a < eprop->totitem; a++) {
          if (array[a].identifier) {
            array[a].identifier = BLI_strdup(array[a].identifier);
          }
          if (array[a].name) {
            array[a].name = BLI_strdup(array[a].name);
          }
          if (array[a].description) {
            array[a].description = BLI_strdup(array[a].description);
          }
        }
      }
      break;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

      if (fprop->defaultarray) {
        float *array = MEM_mallocN(sizeof(float) * prop->totarraylength, "RNA_def_property_store");
        memcpy(array, fprop->defaultarray, sizeof(float) * prop->totarraylength);
        fprop->defaultarray = array;
      }
      break;
    }
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      if (sprop->defaultvalue) {
        sprop->defaultvalue = BLI_strdup(sprop->defaultvalue);
      }
      break;
    }
    default:
      break;
  }

  prop->flag_internal |= PROP_INTERN_FREE_POINTERS;
}

static void (*g_py_data_clear_fn)(PropertyRNA *prop) = NULL;

/**
 * Set the callback used to decrement the user count of a property.
 *
 * This function is called when freeing each dynamically defined property.
 */
void RNA_def_property_free_pointers_set_py_data_callback(
    void (*py_data_clear_fn)(PropertyRNA *prop))
{
  g_py_data_clear_fn = py_data_clear_fn;
}

void RNA_def_property_free_pointers(PropertyRNA *prop)
{
  if (prop->flag_internal & PROP_INTERN_FREE_POINTERS) {
    int a;

    if (g_py_data_clear_fn) {
      g_py_data_clear_fn(prop);
    }

    if (prop->identifier) {
      MEM_freeN((void *)prop->identifier);
    }
    if (prop->name) {
      MEM_freeN((void *)prop->name);
    }
    if (prop->description) {
      MEM_freeN((void *)prop->description);
    }
    if (prop->py_data) {
      MEM_freeN(prop->py_data);
    }

    switch (prop->type) {
      case PROP_BOOLEAN: {
        BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;
        if (bprop->defaultarray) {
          MEM_freeN((void *)bprop->defaultarray);
        }
        break;
      }
      case PROP_INT: {
        IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
        if (iprop->defaultarray) {
          MEM_freeN((void *)iprop->defaultarray);
        }
        break;
      }
      case PROP_FLOAT: {
        FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
        if (fprop->defaultarray) {
          MEM_freeN((void *)fprop->defaultarray);
        }
        break;
      }
      case PROP_ENUM: {
        EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;

        for (a = 0; a < eprop->totitem; a++) {
          if (eprop->item[a].identifier) {
            MEM_freeN((void *)eprop->item[a].identifier);
          }
          if (eprop->item[a].name) {
            MEM_freeN((void *)eprop->item[a].name);
          }
          if (eprop->item[a].description) {
            MEM_freeN((void *)eprop->item[a].description);
          }
        }

        if (eprop->item) {
          MEM_freeN((void *)eprop->item);
        }
        break;
      }
      case PROP_STRING: {
        StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
        if (sprop->defaultvalue) {
          MEM_freeN((void *)sprop->defaultvalue);
        }
        break;
      }
      default:
        break;
    }
  }
}

static void rna_def_property_free(StructOrFunctionRNA *cont_, PropertyRNA *prop)
{
  ContainerRNA *cont = cont_;

  if (prop->flag_internal & PROP_INTERN_RUNTIME) {
    if (cont->prophash) {
      BLI_ghash_remove(cont->prophash, prop->identifier, NULL, NULL);
    }

    RNA_def_property_free_pointers(prop);
    rna_freelinkN(&cont->properties, prop);
  }
  else {
    RNA_def_property_free_pointers(prop);
  }
}

static PropertyRNA *rna_def_property_find_py_id(ContainerRNA *cont, const char *identifier)
{
  for (PropertyRNA *prop = cont->properties.first; prop; prop = prop->next) {
    if (STREQ(prop->identifier, identifier)) {
      return prop;
    }
  }
  return NULL;
}

/* NOTE: only intended for removing dynamic props. */
int RNA_def_property_free_identifier(StructOrFunctionRNA *cont_, const char *identifier)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop = rna_def_property_find_py_id(cont, identifier);
  if (prop != NULL) {
    if (prop->flag_internal & PROP_INTERN_RUNTIME) {
      rna_def_property_free(cont, prop);
      return 1;
    }
    else {
      return -1;
    }
  }
  return 0;
}

int RNA_def_property_free_identifier_deferred_prepare(StructOrFunctionRNA *cont_,
                                                      const char *identifier,
                                                      void **r_handle)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop = rna_def_property_find_py_id(cont, identifier);
  if (prop != NULL) {
    if (prop->flag_internal & PROP_INTERN_RUNTIME) {
      *r_handle = prop;
      return 1;
    }
    else {
      return -1;
    }
  }
  return 0;
}

void RNA_def_property_free_identifier_deferred_finish(StructOrFunctionRNA *cont_, void *handle)
{
  ContainerRNA *cont = cont_;
  PropertyRNA *prop = handle;
  BLI_assert(BLI_findindex(&cont->properties, prop) != -1);
  BLI_assert(prop->flag_internal & PROP_INTERN_RUNTIME);
  rna_def_property_free(cont, prop);
}

#endif /* RNA_RUNTIME */

const char *RNA_property_typename(PropertyType type)
{
  switch (type) {
    case PROP_BOOLEAN:
      return "PROP_BOOLEAN";
    case PROP_INT:
      return "PROP_INT";
    case PROP_FLOAT:
      return "PROP_FLOAT";
    case PROP_STRING:
      return "PROP_STRING";
    case PROP_ENUM:
      return "PROP_ENUM";
    case PROP_POINTER:
      return "PROP_POINTER";
    case PROP_COLLECTION:
      return "PROP_COLLECTION";
  }

  return "PROP_UNKNOWN";
}
