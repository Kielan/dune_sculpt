void BKE_rigidbody_constraints_collection_validate(Scene *scene, RigidBodyWorld *rbw)
{
  if (rbw->constraints != NULL) {
    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->constraints, object) {
      if (object->rigidbody_constraint != NULL) {
        continue;
      }
      object->rigidbody_constraint = BKE_rigidbody_create_constraint(
          scene, object, RBC_TYPE_FIXED);
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
  }
}

void BKE_rigidbody_main_collection_object_add(Main *bmain, Collection *collection, Object *object)
{
  for (Scene *scene = bmain->scenes.first; scene; scene = scene->id.next) {
    RigidBodyWorld *rbw = scene->rigidbody_world;

    if (rbw == NULL) {
      continue;
    }

    if (rbw->group == collection && object->type == OB_MESH && object->rigidbody_object == NULL) {
      object->rigidbody_object = BKE_rigidbody_create_object(scene, object, RBO_TYPE_ACTIVE);
    }
    if (rbw->constraints == collection && object->rigidbody_constraint == NULL) {
      object->rigidbody_constraint = BKE_rigidbody_create_constraint(
          scene, object, RBC_TYPE_FIXED);
    }
  }
}

/* ************************************** */
/* Utilities API */

RigidBodyWorld *BKE_rigidbody_get_world(Scene *scene)
{
  /* sanity check */
  if (scene == NULL) {
    return NULL;
  }

  return scene->rigidbody_world;
}

static bool rigidbody_add_object_to_scene(Main *bmain, Scene *scene, Object *ob)
{
  /* Add rigid body world and group if they don't exist for convenience */
  RigidBodyWorld *rbw = BKE_rigidbody_get_world(scene);
  if (rbw == NULL) {
    rbw = BKE_rigidbody_create_world(scene);
    if (rbw == NULL) {
      return false;
    }

    BKE_rigidbody_validate_sim_world(scene, rbw, false);
    scene->rigidbody_world = rbw;
  }

  if (rbw->group == NULL) {
    rbw->group = BKE_collection_add(bmain, NULL, "RigidBodyWorld");
    id_fake_user_set(&rbw->group->id);
  }

  /* Add object to rigid body group. */
  BKE_collection_object_add(bmain, rbw->group, ob);
  BKE_rigidbody_cache_reset(rbw);

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&rbw->group->id, ID_RECALC_COPY_ON_WRITE);

  return true;
}

static bool rigidbody_add_constraint_to_scene(Main *bmain, Scene *scene, Object *ob)
{
  /* Add rigid body world and group if they don't exist for convenience */
  RigidBodyWorld *rbw = BKE_rigidbody_get_world(scene);
  if (rbw == NULL) {
    rbw = BKE_rigidbody_create_world(scene);
    if (rbw == NULL) {
      return false;
    }

    BKE_rigidbody_validate_sim_world(scene, rbw, false);
    scene->rigidbody_world = rbw;
  }

  if (rbw->constraints == NULL) {
    rbw->constraints = BKE_collection_add(bmain, NULL, "RigidBodyConstraints");
    id_fake_user_set(&rbw->constraints->id);
  }

  /* Add object to rigid body group. */
  BKE_collection_object_add(bmain, rbw->constraints, ob);
  BKE_rigidbody_cache_reset(rbw);

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&rbw->constraints->id, ID_RECALC_COPY_ON_WRITE);

  return true;
}

void BKE_rigidbody_ensure_local_object(Main *bmain, Object *ob)
{
  if (ob->rigidbody_object != NULL) {
    /* Add newly local object to scene. */
    for (Scene *scene = bmain->scenes.first; scene; scene = scene->id.next) {
      if (BKE_scene_object_find(scene, ob)) {
        rigidbody_add_object_to_scene(bmain, scene, ob);
      }
    }
  }
  if (ob->rigidbody_constraint != NULL) {
    /* Add newly local object to scene. */
    for (Scene *scene = bmain->scenes.first; scene; scene = scene->id.next) {
      if (BKE_scene_object_find(scene, ob)) {
        rigidbody_add_constraint_to_scene(bmain, scene, ob);
      }
    }
  }
}

bool BKE_rigidbody_add_object(Main *bmain, Scene *scene, Object *ob, int type, ReportList *reports)
{
  if (ob->type != OB_MESH) {
    BKE_report(reports, RPT_ERROR, "Can't add Rigid Body to non mesh object");
    return false;
  }

  /* Add object to rigid body world in scene. */
  if (!rigidbody_add_object_to_scene(bmain, scene, ob)) {
    BKE_report(reports, RPT_ERROR, "Can't create Rigid Body world");
    return false;
  }

  /* make rigidbody object settings */
  if (ob->rigidbody_object == NULL) {
    ob->rigidbody_object = BKE_rigidbody_create_object(scene, ob, type);
  }
  ob->rigidbody_object->type = type;
  ob->rigidbody_object->flag |= RBO_FLAG_NEEDS_VALIDATE;

  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);

  return true;
}

void BKE_rigidbody_remove_object(Main *bmain, Scene *scene, Object *ob, const bool free_us)
{
  RigidBodyWorld *rbw = scene->rigidbody_world;
  RigidBodyCon *rbc;
  int i;

  if (rbw) {

    /* remove object from array */
    if (rbw->objects) {
      for (i = 0; i < rbw->numbodies; i++) {
        if (rbw->objects[i] == ob) {
          rbw->objects[i] = NULL;
          break;
        }
      }
    }

    /* remove object from rigid body constraints */
    if (rbw->constraints) {
      FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->constraints, obt) {
        if (obt && obt->rigidbody_constraint) {
          rbc = obt->rigidbody_constraint;
          if (rbc->ob1 == ob) {
            rbc->ob1 = NULL;
            DEG_id_tag_update(&obt->id, ID_RECALC_COPY_ON_WRITE);
          }
          if (rbc->ob2 == ob) {
            rbc->ob2 = NULL;
            DEG_id_tag_update(&obt->id, ID_RECALC_COPY_ON_WRITE);
          }
        }
      }
      FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
    }

    /* Relying on usercount of the object should be OK, and it is much cheaper than looping in all
     * collections to check whether the object is already in another one... */
    if (ID_REAL_USERS(&ob->id) == 1) {
      /* Some users seems to find it funny to use a view-layer instancing collection
       * as RBW collection... Despite this being a bad (ab)use of the system, avoid losing objects
       * when we remove them from RB simulation. */
      BKE_collection_object_add(bmain, scene->master_collection, ob);
    }
    BKE_collection_object_remove(bmain, rbw->group, ob, free_us);

    /* flag cache as outdated */
    BKE_rigidbody_cache_reset(rbw);
    /* Reset cache as the object order probably changed after freeing the object. */
    PTCacheID pid;
    BKE_ptcache_id_from_rigidbody(&pid, NULL, rbw);
    BKE_ptcache_id_reset(scene, &pid, PTCACHE_RESET_OUTDATED);
  }

  /* remove object's settings */
  BKE_rigidbody_free_object(ob, rbw);

  /* Dependency graph update */
  DEG_relations_tag_update(bmain);
  DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
}

void BKE_rigidbody_remove_constraint(Main *bmain, Scene *scene, Object *ob, const bool free_us)
{
  RigidBodyWorld *rbw = scene->rigidbody_world;
  RigidBodyCon *rbc = ob->rigidbody_constraint;

  if (rbw != NULL) {
    /* Remove from RBW constraints collection. */
    if (rbw->constraints != NULL) {
      BKE_collection_object_remove(bmain, rbw->constraints, ob, free_us);
      DEG_id_tag_update(&rbw->constraints->id, ID_RECALC_COPY_ON_WRITE);
    }

    /* remove from rigidbody world, free object won't do this */
    if (rbw->shared->physics_world && rbc->physics_constraint) {
      RB_dworld_remove_constraint(rbw->shared->physics_world, rbc->physics_constraint);
    }
  }

  /* remove object's settings */
  BKE_rigidbody_free_constraint(ob);

  /* flag cache as outdated */
  BKE_rigidbody_cache_reset(rbw);
}

/* ************************************** */
/* Simulation Interface - Bullet */

/* Update object array and rigid body count so they're in sync with the rigid body group */
static void rigidbody_update_ob_array(RigidBodyWorld *rbw)
{
  if (rbw->group == NULL) {
    rbw->numbodies = 0;
    rbw->objects = realloc(rbw->objects, 0);
    return;
  }

  int n = 0;
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, object) {
    (void)object;
    /* Ignore if this object is the direct child of an object with a compound shape */
    if (object->parent == NULL || object->parent->rigidbody_object == NULL ||
        object->parent->rigidbody_object->shape != RB_SHAPE_COMPOUND) {
      n++;
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

  if (rbw->numbodies != n) {
    rbw->numbodies = n;
    rbw->objects = realloc(rbw->objects, sizeof(Object *) * rbw->numbodies);
  }

  int i = 0;
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, object) {
    /* Ignore if this object is the direct child of an object with a compound shape */
    if (object->parent == NULL || object->parent->rigidbody_object == NULL ||
        object->parent->rigidbody_object->shape != RB_SHAPE_COMPOUND) {
      rbw->objects[i] = object;
      i++;
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

static void rigidbody_update_sim_world(Scene *scene, RigidBodyWorld *rbw)
{
  float adj_gravity[3];

  /* adjust gravity to take effector weights into account */
  if (scene->physics_settings.flag & PHYS_GLOBAL_GRAVITY) {
    copy_v3_v3(adj_gravity, scene->physics_settings.gravity);
    mul_v3_fl(adj_gravity,
              rbw->effector_weights->global_gravity * rbw->effector_weights->weight[0]);
  }
  else {
    zero_v3(adj_gravity);
  }

  /* update gravity, since this RNA setting is not part of RigidBody settings */
  RB_dworld_set_gravity(rbw->shared->physics_world, adj_gravity);

  /* update object array in case there are changes */
  rigidbody_update_ob_array(rbw);
}

static void rigidbody_update_sim_ob(
    Depsgraph *depsgraph, Scene *scene, RigidBodyWorld *rbw, Object *ob, RigidBodyOb *rbo)
{
  /* only update if rigid body exists */
  if (rbo->shared->physics_object == NULL) {
    return;
  }

  ViewLayer *view_layer = DEG_get_input_view_layer(depsgraph);
  Base *base = BKE_view_layer_base_find(view_layer, ob);
  const bool is_selected = base ? (base->flag & BASE_SELECTED) != 0 : false;

  if (rbo->shape == RB_SHAPE_TRIMESH && rbo->flag & RBO_FLAG_USE_DEFORM) {
    Mesh *mesh = ob->runtime.mesh_deform_eval;
    if (mesh) {
      MVert *mvert = mesh->mvert;
      int totvert = mesh->totvert;
      BoundBox *bb = BKE_object_boundbox_get(ob);

      RB_shape_trimesh_update(rbo->shared->physics_shape,
                              (float *)mvert,
                              totvert,
                              sizeof(MVert),
                              bb->vec[0],
                              bb->vec[6]);
    }
  }

  if (!(rbo->flag & RBO_FLAG_KINEMATIC)) {
    /* update scale for all non kinematic objects */
    float new_scale[3], old_scale[3];
    mat4_to_size(new_scale, ob->obmat);
    RB_body_get_scale(rbo->shared->physics_object, old_scale);

    /* Avoid updating collision shape AABBs if scale didn't change. */
    if (!compare_size_v3v3(old_scale, new_scale, 0.001f)) {
      RB_body_set_scale(rbo->shared->physics_object, new_scale);
      /* compensate for embedded convex hull collision margin */
      if (!(rbo->flag & RBO_FLAG_USE_MARGIN) && rbo->shape == RB_SHAPE_CONVEXH) {
        RB_shape_set_margin(rbo->shared->physics_shape,
                            RBO_GET_MARGIN(rbo) * MIN3(new_scale[0], new_scale[1], new_scale[2]));
      }
    }
  }

  /* Make transformed objects temporarily kinmatic
   * so that they can be moved by the user during simulation. */
  if (is_selected && (G.moving & G_TRANSFORM_OBJ)) {
    RB_body_set_kinematic_state(rbo->shared->physics_object, true);
    RB_body_set_mass(rbo->shared->physics_object, 0.0f);
  }

  /* update influence of effectors - but don't do it on an effector */
  /* only dynamic bodies need effector update */
  else if (rbo->type == RBO_TYPE_ACTIVE &&
           ((ob->pd == NULL) || (ob->pd->forcefield == PFIELD_NULL))) {
    EffectorWeights *effector_weights = rbw->effector_weights;
    EffectedPoint epoint;
    ListBase *effectors;

    /* get effectors present in the group specified by effector_weights */
    effectors = BKE_effectors_create(depsgraph, ob, NULL, effector_weights, false);
    if (effectors) {
      float eff_force[3] = {0.0f, 0.0f, 0.0f};
      float eff_loc[3], eff_vel[3];

      /* create dummy 'point' which represents last known position of object as result of sim */
      /* XXX: this can create some inaccuracies with sim position,
       * but is probably better than using un-simulated values? */
      RB_body_get_position(rbo->shared->physics_object, eff_loc);
      RB_body_get_linear_velocity(rbo->shared->physics_object, eff_vel);

      pd_point_from_loc(scene, eff_loc, eff_vel, 0, &epoint);

      /* Calculate net force of effectors, and apply to sim object:
       * - we use 'central force' since apply force requires a "relative position"
       *   which we don't have... */
      BKE_effectors_apply(effectors, NULL, effector_weights, &epoint, eff_force, NULL, NULL);
      if (G.f & G_DEBUG) {
        printf("\tapplying force (%f,%f,%f) to '%s'\n",
               eff_force[0],
               eff_force[1],
               eff_force[2],
               ob->id.name + 2);
      }
      /* activate object in case it is deactivated */
      if (!is_zero_v3(eff_force)) {
        RB_body_activate(rbo->shared->physics_object);
      }
      RB_body_apply_central_force(rbo->shared->physics_object, eff_force);
    }
    else if (G.f & G_DEBUG) {
      printf("\tno forces to apply to '%s'\n", ob->id.name + 2);
    }

    /* cleanup */
    BKE_effectors_free(effectors);
  }
  /* NOTE: passive objects don't need to be updated since they don't move */

  /* NOTE: no other settings need to be explicitly updated here,
   * since RNA setters take care of the rest :)
   */
}

/**
 * Updates and validates world, bodies and shapes.
 *
 * \param rebuild: Rebuild entire simulation
 */
static void rigidbody_update_simulation(Depsgraph *depsgraph,
                                        Scene *scene,
                                        RigidBodyWorld *rbw,
                                        bool rebuild)
{
  /* update world */
  /* Note physics_world can get NULL when undoing the deletion of the last object in it (see
   * T70667). */
  if (rebuild || rbw->shared->physics_world == NULL) {
    BKE_rigidbody_validate_sim_world(scene, rbw, rebuild);
    /* We have rebuilt the world so we need to make sure the rest is rebuilt as well. */
    rebuild = true;
  }

  rigidbody_update_sim_world(scene, rbw);

  /* XXX TODO: For rebuild: remove all constraints first.
   * Otherwise we can end up deleting objects that are still
   * referenced by constraints, corrupting bullet's internal list.
   *
   * Memory management needs redesign here, this is just a dirty workaround.
   */
  if (rebuild && rbw->constraints) {
    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->constraints, ob) {
      RigidBodyCon *rbc = ob->rigidbody_constraint;
      if (rbc && rbc->physics_constraint) {
        RB_dworld_remove_constraint(rbw->shared->physics_world, rbc->physics_constraint);
        RB_constraint_delete(rbc->physics_constraint);
        rbc->physics_constraint = NULL;
      }
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
  }

  /* update objects */
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, ob) {
    if (ob->type == OB_MESH) {
      /* validate that we've got valid object set up here... */
      RigidBodyOb *rbo = ob->rigidbody_object;

      /* TODO: remove this whole block once we are sure we never get NULL rbo here anymore. */
      /* This cannot be done in CoW evaluation context anymore... */
      if (rbo == NULL) {
        BLI_assert_msg(0,
                       "CoW object part of RBW object collection without RB object data, "
                       "should not happen.\n");
        /* Since this object is included in the sim group but doesn't have
         * rigid body settings (perhaps it was added manually), add!
         * - assume object to be active? That is the default for newly added settings...
         */
        ob->rigidbody_object = BKE_rigidbody_create_object(scene, ob, RBO_TYPE_ACTIVE);
        rigidbody_validate_sim_object(rbw, ob, true);

        rbo = ob->rigidbody_object;
      }
      else {
        /* perform simulation data updates as tagged */
        /* refresh object... */
        if (rebuild) {
          /* World has been rebuilt so rebuild object */
          /* TODO(Sybren): rigidbody_validate_sim_object() can call rigidbody_validate_sim_shape(),
           * but neither resets the RBO_FLAG_NEEDS_RESHAPE flag nor
           * calls RB_body_set_collision_shape().
           * This results in the collision shape being created twice, which is unnecessary. */
          rigidbody_validate_sim_object(rbw, ob, true);
        }
        else if (rbo->flag & RBO_FLAG_NEEDS_VALIDATE) {
          rigidbody_validate_sim_object(rbw, ob, false);
        }
        /* refresh shape... */
        if (rbo->flag & RBO_FLAG_NEEDS_RESHAPE) {
          /* mesh/shape data changed, so force shape refresh */
          rigidbody_validate_sim_shape(rbw, ob, true);
          /* now tell RB sim about it */
          /* XXX: we assume that this can only get applied for active/passive shapes
           * that will be included as rigidbodies. */
          if (rbo->shared->physics_object != NULL && rbo->shared->physics_shape != NULL) {
            RB_body_set_collision_shape(rbo->shared->physics_object, rbo->shared->physics_shape);
          }
        }
      }
      rbo->flag &= ~(RBO_FLAG_NEEDS_VALIDATE | RBO_FLAG_NEEDS_RESHAPE);

      /* update simulation object... */
      rigidbody_update_sim_ob(depsgraph, scene, rbw, ob, rbo);
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

  /* update constraints */
  if (rbw->constraints == NULL) { /* no constraints, move on */
    return;
  }

  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->constraints, ob) {
    /* validate that we've got valid object set up here... */
    RigidBodyCon *rbc = ob->rigidbody_constraint;

    /* TODO: remove this whole block once we are sure we never get NULL rbo here anymore. */
    /* This cannot be done in CoW evaluation context anymore... */
    if (rbc == NULL) {
      BLI_assert_msg(0,
                     "CoW object part of RBW constraints collection without RB constraint data, "
                     "should not happen.\n");
      /* Since this object is included in the group but doesn't have
       * constraint settings (perhaps it was added manually), add!
       */
      ob->rigidbody_constraint = BKE_rigidbody_create_constraint(scene, ob, RBC_TYPE_FIXED);
      rigidbody_validate_sim_constraint(rbw, ob, true);

      rbc = ob->rigidbody_constraint;
    }
    else {
      /* perform simulation data updates as tagged */
      if (rebuild) {
        /* World has been rebuilt so rebuild constraint */
        rigidbody_validate_sim_constraint(rbw, ob, true);
      }
      else if (rbc->flag & RBC_FLAG_NEEDS_VALIDATE) {
        rigidbody_validate_sim_constraint(rbw, ob, false);
      }
    }
    rbc->flag &= ~RBC_FLAG_NEEDS_VALIDATE;
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

typedef struct KinematicSubstepData {
  RigidBodyOb *rbo;
  float old_pos[3];
  float new_pos[3];
  float old_rot[4];
  float new_rot[4];
  bool scale_changed;
  float old_scale[3];
  float new_scale[3];
} KinematicSubstepData;

static ListBase rigidbody_create_substep_data(RigidBodyWorld *rbw)
{
  /* Objects that we want to update substep location/rotation for. */
  ListBase substep_targets = {NULL, NULL};

  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, ob) {
    RigidBodyOb *rbo = ob->rigidbody_object;
    /* only update if rigid body exists */
    if (!rbo || rbo->shared->physics_object == NULL) {
      continue;
    }

    if (rbo->flag & RBO_FLAG_KINEMATIC) {
      float loc[3], rot[4], scale[3];

      KinematicSubstepData *data = MEM_callocN(sizeof(KinematicSubstepData),
                                               "RigidBody Substep data");

      data->rbo = rbo;

      RB_body_get_position(rbo->shared->physics_object, loc);
      RB_body_get_orientation(rbo->shared->physics_object, rot);
      RB_body_get_scale(rbo->shared->physics_object, scale);

      copy_v3_v3(data->old_pos, loc);
      copy_v4_v4(data->old_rot, rot);
      copy_v3_v3(data->old_scale, scale);

      mat4_decompose(loc, rot, scale, ob->obmat);

      copy_v3_v3(data->new_pos, loc);
      copy_v4_v4(data->new_rot, rot);
      copy_v3_v3(data->new_scale, scale);

      data->scale_changed = !compare_size_v3v3(data->old_scale, data->new_scale, 0.001f);

      LinkData *ob_link = BLI_genericNodeN(data);
      BLI_addtail(&substep_targets, ob_link);
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

  return substep_targets;
}

static void rigidbody_update_kinematic_obj_substep(ListBase *substep_targets, float interp_fac)
{
  LISTBASE_FOREACH (LinkData *, link, substep_targets) {
    KinematicSubstepData *data = link->data;
    RigidBodyOb *rbo = data->rbo;

    float loc[3], rot[4];

    interp_v3_v3v3(loc, data->old_pos, data->new_pos, interp_fac);
    interp_qt_qtqt(rot, data->old_rot, data->new_rot, interp_fac);

    RB_body_activate(rbo->shared->physics_object);
    RB_body_set_loc_rot(rbo->shared->physics_object, loc, rot);

    if (!data->scale_changed) {
      /* Avoid having to rebuild the collision shape AABBs if scale didn't change. */
      continue;
    }

    float scale[3];

    interp_v3_v3v3(scale, data->old_scale, data->new_scale, interp_fac);

    RB_body_set_scale(rbo->shared->physics_object, scale);

    /* compensate for embedded convex hull collision margin */
    if (!(rbo->flag & RBO_FLAG_USE_MARGIN) && rbo->shape == RB_SHAPE_CONVEXH) {
      RB_shape_set_margin(rbo->shared->physics_shape,
                          RBO_GET_MARGIN(rbo) * MIN3(scale[0], scale[1], scale[2]));
    }
  }
}

static void rigidbody_free_substep_data(ListBase *substep_targets)
{
  LISTBASE_FOREACH (LinkData *, link, substep_targets) {
    KinematicSubstepData *data = link->data;
    MEM_freeN(data);
  }

  BLI_freelistN(substep_targets);
}
static void rigidbody_update_simulation_post_step(Depsgraph *depsgraph, RigidBodyWorld *rbw)
{
  ViewLayer *view_layer = DEG_get_input_view_layer(depsgraph);

  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, ob) {
    Base *base = BKE_view_layer_base_find(view_layer, ob);
    RigidBodyOb *rbo = ob->rigidbody_object;
    /* Reset kinematic state for transformed objects. */
    if (rbo && base && (base->flag & BASE_SELECTED) && (G.moving & G_TRANSFORM_OBJ) &&
        rbo->shared->physics_object) {
      RB_body_set_kinematic_state(rbo->shared->physics_object,
                                  rbo->flag & RBO_FLAG_KINEMATIC || rbo->flag & RBO_FLAG_DISABLED);
      RB_body_set_mass(rbo->shared->physics_object, RBO_GET_MASS(rbo));
      /* Deactivate passive objects so they don't interfere with deactivation of active objects. */
      if (rbo->type == RBO_TYPE_PASSIVE) {
        RB_body_deactivate(rbo->shared->physics_object);
      }
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
}

bool BKE_rigidbody_check_sim_running(RigidBodyWorld *rbw, float ctime)
{
  return (rbw && (rbw->flag & RBW_FLAG_MUTED) == 0 && ctime > rbw->shared->pointcache->startframe);
}

void BKE_rigidbody_sync_transforms(RigidBodyWorld *rbw, Object *ob, float ctime)
{
  if (!BKE_rigidbody_is_affected_by_simulation(ob)) {
    /* Don't sync transforms for objects that are not affected/changed by the simulation. */
    return;
  }

  RigidBodyOb *rbo = ob->rigidbody_object;

  /* use rigid body transform after cache start frame if objects is not being transformed */
  if (BKE_rigidbody_check_sim_running(rbw, ctime) &&
      !(ob->base_flag & BASE_SELECTED && G.moving & G_TRANSFORM_OBJ)) {
    float mat[4][4], size_mat[4][4], size[3];

    normalize_qt(rbo->orn); /* RB_TODO investigate why quaternion isn't normalized at this point */
    quat_to_mat4(mat, rbo->orn);
    copy_v3_v3(mat[3], rbo->pos);

    mat4_to_size(size, ob->obmat);
    size_to_mat4(size_mat, size);
    mul_m4_m4m4(mat, mat, size_mat);

    copy_m4_m4(ob->obmat, mat);
  }
  /* otherwise set rigid body transform to current obmat */
  else {
    mat4_to_loc_quat(rbo->pos, rbo->orn, ob->obmat);
  }
}

void BKE_rigidbody_aftertrans_update(
    Object *ob, float loc[3], float rot[3], float quat[4], float rotAxis[3], float rotAngle)
{
  bool correct_delta = BKE_rigidbody_is_affected_by_simulation(ob);
  RigidBodyOb *rbo = ob->rigidbody_object;

  /* return rigid body and object to their initial states */
  copy_v3_v3(rbo->pos, ob->loc);
  copy_v3_v3(ob->loc, loc);

  if (correct_delta) {
    add_v3_v3(rbo->pos, ob->dloc);
  }

  if (ob->rotmode > 0) {
    float qt[4];
    eulO_to_quat(qt, ob->rot, ob->rotmode);

    if (correct_delta) {
      float dquat[4];
      eulO_to_quat(dquat, ob->drot, ob->rotmode);

      mul_qt_qtqt(rbo->orn, dquat, qt);
    }
    else {
      copy_qt_qt(rbo->orn, qt);
    }

    copy_v3_v3(ob->rot, rot);
  }
  else if (ob->rotmode == ROT_MODE_AXISANGLE) {
    float qt[4];
    axis_angle_to_quat(qt, ob->rotAxis, ob->rotAngle);

    if (correct_delta) {
      float dquat[4];
      axis_angle_to_quat(dquat, ob->drotAxis, ob->drotAngle);

      mul_qt_qtqt(rbo->orn, dquat, qt);
    }
    else {
      copy_qt_qt(rbo->orn, qt);
    }

    copy_v3_v3(ob->rotAxis, rotAxis);
    ob->rotAngle = rotAngle;
  }
  else {
    if (correct_delta) {
      mul_qt_qtqt(rbo->orn, ob->dquat, ob->quat);
    }
    else {
      copy_qt_qt(rbo->orn, ob->quat);
    }

    copy_qt_qt(ob->quat, quat);
  }

  if (rbo->shared->physics_object) {
    /* allow passive objects to return to original transform */
    if (rbo->type == RBO_TYPE_PASSIVE) {
      RB_body_set_kinematic_state(rbo->shared->physics_object, true);
    }
    RB_body_set_loc_rot(rbo->shared->physics_object, rbo->pos, rbo->orn);
  }
  /* RB_TODO update rigid body physics object's loc/rot for dynamic objects here as well
   * (needs to be done outside bullet's update loop). */
}

void BKE_rigidbody_cache_reset(RigidBodyWorld *rbw)
{
  if (rbw) {
    rbw->shared->pointcache->flag |= PTCACHE_OUTDATED;
  }
}

/* ------------------ */

void BKE_rigidbody_rebuild_world(Depsgraph *depsgraph, Scene *scene, float ctime)
{
  RigidBodyWorld *rbw = scene->rigidbody_world;
  PointCache *cache;
  PTCacheID pid;
  int startframe, endframe;

  BKE_ptcache_id_from_rigidbody(&pid, NULL, rbw);
  BKE_ptcache_id_time(&pid, scene, ctime, &startframe, &endframe, NULL);
  cache = rbw->shared->pointcache;

  /* Flag cache as outdated if we don't have a world or number of objects
   * in the simulation has changed. */
  int n = 0;
  FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (rbw->group, object) {
    (void)object;
    /* Ignore if this object is the direct child of an object with a compound shape */
    if (object->parent == NULL || object->parent->rigidbody_object == NULL ||
        object->parent->rigidbody_object->shape != RB_SHAPE_COMPOUND) {
      n++;
    }
  }
  FOREACH_COLLECTION_OBJECT_RECURSIVE_END;

  if (rbw->shared->physics_world == NULL || rbw->numbodies != n) {
    cache->flag |= PTCACHE_OUTDATED;
  }

  if (ctime == startframe + 1 && rbw->ltime == startframe) {
    if (cache->flag & PTCACHE_OUTDATED) {
      BKE_ptcache_id_reset(scene, &pid, PTCACHE_RESET_OUTDATED);
      rigidbody_update_simulation(depsgraph, scene, rbw, true);
      BKE_ptcache_validate(cache, (int)ctime);
      cache->last_exact = 0;
      cache->flag &= ~PTCACHE_REDO_NEEDED;
    }
  }
}

void BKE_rigidbody_do_simulation(Depsgraph *depsgraph, Scene *scene, float ctime)
{
  RigidBodyWorld *rbw = scene->rigidbody_world;
  PointCache *cache;
  PTCacheID pid;
  int startframe, endframe;

  BKE_ptcache_id_from_rigidbody(&pid, NULL, rbw);
  BKE_ptcache_id_time(&pid, scene, ctime, &startframe, &endframe, NULL);
  cache = rbw->shared->pointcache;

  if (ctime <= startframe) {
    rbw->ltime = startframe;
    return;
  }
  /* make sure we don't go out of cache frame range */
  if (ctime > endframe) {
    ctime = endframe;
  }

  /* don't try to run the simulation if we don't have a world yet but allow reading baked cache */
  if (rbw->shared->physics_world == NULL && !(cache->flag & PTCACHE_BAKED)) {
    return;
  }
  if (rbw->objects == NULL) {
    rigidbody_update_ob_array(rbw);
  }

  /* try to read from cache */
  /* RB_TODO deal with interpolated, old and baked results */
  bool can_simulate = (ctime == rbw->ltime + 1) && !(cache->flag & PTCACHE_BAKED);

  if (BKE_ptcache_read(&pid, ctime, can_simulate) == PTCACHE_READ_EXACT) {
    BKE_ptcache_validate(cache, (int)ctime);
    rbw->ltime = ctime;
    return;
  }

  if (!DEG_is_active(depsgraph)) {
    /* When the depsgraph is inactive we should neither write to the cache
     * nor run the simulation. */
    return;
  }

  /* advance simulation, we can only step one frame forward */
  if (compare_ff_relative(ctime, rbw->ltime + 1, FLT_EPSILON, 64)) {
    /* write cache for first frame when on second frame */
    if (rbw->ltime == startframe && (cache->flag & PTCACHE_OUTDATED || cache->last_exact == 0)) {
      BKE_ptcache_write(&pid, startframe);
    }

    /* update and validate simulation */
    rigidbody_update_simulation(depsgraph, scene, rbw, false);

    const float frame_diff = ctime - rbw->ltime;
    /* calculate how much time elapsed since last step in seconds */
    const float timestep = 1.0f / (float)FPS * frame_diff * rbw->time_scale;

    const float substep = timestep / rbw->substeps_per_frame;

    ListBase substep_targets = rigidbody_create_substep_data(rbw);

    const float interp_step = 1.0f / rbw->substeps_per_frame;
    float cur_interp_val = interp_step;

    for (int i = 0; i < rbw->substeps_per_frame; i++) {
      rigidbody_update_kinematic_obj_substep(&substep_targets, cur_interp_val);
      RB_dworld_step_simulation(rbw->shared->physics_world, substep, 0, substep);
      cur_interp_val += interp_step;
    }
    rigidbody_free_substep_data(&substep_targets);

    rigidbody_update_simulation_post_step(depsgraph, rbw);

    /* write cache for current frame */
    BKE_ptcache_validate(cache, (int)ctime);
    BKE_ptcache_write(&pid, (unsigned int)ctime);

    rbw->ltime = ctime;
  }
}
/* ************************************** */

#else /* WITH_BULLET */

/* stubs */
#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wunused-parameter"
#  endif

void BKE_rigidbody_object_copy(Main *bmain, Object *ob_dst, const Object *ob_src, const int flag)
{
}
void BKE_rigidbody_validate_sim_world(Scene *scene, RigidBodyWorld *rbw, bool rebuild)
{
}

void BKE_rigidbody_calc_volume(Object *ob, float *r_vol)
{
  if (r_vol) {
    *r_vol = 0.0f;
  }
}
void BKE_rigidbody_calc_center_of_mass(Object *ob, float r_center[3])
{
  zero_v3(r_center);
}
struct RigidBodyWorld *BKE_rigidbody_create_world(Scene *scene)
{
  return NULL;
}
struct RigidBodyWorld *BKE_rigidbody_world_copy(RigidBodyWorld *rbw, const int flag)
{
  return NULL;
}
void BKE_rigidbody_world_groups_relink(struct RigidBodyWorld *rbw)
{
}
void BKE_rigidbody_world_id_loop(struct RigidBodyWorld *rbw,
                                 RigidbodyWorldIDFunc func,
                                 void *userdata)
{
}
struct RigidBodyOb *BKE_rigidbody_create_object(Scene *scene, Object *ob, short type)
{
  return NULL;
}
struct RigidBodyCon *BKE_rigidbody_create_constraint(Scene *scene, Object *ob, short type)
{
  return NULL;
}
struct RigidBodyWorld *BKE_rigidbody_get_world(Scene *scene)
{
  return NULL;
}

void BKE_rigidbody_ensure_local_object(Main *bmain, Object *ob)
{
}

bool BKE_rigidbody_add_object(Main *bmain, Scene *scene, Object *ob, int type, ReportList *reports)
{
  BKE_report(reports, RPT_ERROR, "Compiled without Bullet physics engine");
  return false;
}

void BKE_rigidbody_remove_object(struct Main *bmain, Scene *scene, Object *ob, const bool free_us)
{
}
void BKE_rigidbody_remove_constraint(Main *bmain, Scene *scene, Object *ob, const bool free_us)
{
}
void BKE_rigidbody_sync_transforms(RigidBodyWorld *rbw, Object *ob, float ctime)
{
}
void BKE_rigidbody_aftertrans_update(
    Object *ob, float loc[3], float rot[3], float quat[4], float rotAxis[3], float rotAngle)
{
}
bool BKE_rigidbody_check_sim_running(RigidBodyWorld *rbw, float ctime)
{
  return false;
}
void BKE_rigidbody_cache_reset(RigidBodyWorld *rbw)
{
}
void BKE_rigidbody_rebuild_world(Depsgraph *depsgraph, Scene *scene, float ctime)
{
}
void BKE_rigidbody_do_simulation(Depsgraph *depsgraph, Scene *scene, float ctime)
{
}
void BKE_rigidbody_objects_collection_validate(Scene *scene, RigidBodyWorld *rbw)
{
}
void BKE_rigidbody_constraints_collection_validate(Scene *scene, RigidBodyWorld *rbw)
{
}
void BKE_rigidbody_main_collection_object_add(Main *bmain, Collection *collection, Object *object)
{
}

#  if defined(__GNUC__) || defined(__clang__)
#    pragma GCC diagnostic pop
#  endif

#endif /* WITH_BULLET */

/* -------------------- */
/* Depsgraph evaluation */

void BKE_rigidbody_rebuild_sim(Depsgraph *depsgraph, Scene *scene)
{
  float ctime = DEG_get_ctime(depsgraph);
  DEG_debug_print_eval_time(depsgraph, __func__, scene->id.name, scene, ctime);
  /* rebuild sim data (i.e. after resetting to start of timeline) */
  if (BKE_scene_check_rigidbody_active(scene)) {
    BKE_rigidbody_rebuild_world(depsgraph, scene, ctime);
  }
}

void BKE_rigidbody_eval_simulation(Depsgraph *depsgraph, Scene *scene)
{
  float ctime = DEG_get_ctime(depsgraph);
  DEG_debug_print_eval_time(depsgraph, __func__, scene->id.name, scene, ctime);

  /* evaluate rigidbody sim */
  if (!BKE_scene_check_rigidbody_active(scene)) {
    return;
  }
  BKE_rigidbody_do_simulation(depsgraph, scene, ctime);
}

void BKE_rigidbody_object_sync_transforms(Depsgraph *depsgraph, Scene *scene, Object *ob)
{
  RigidBodyWorld *rbw = scene->rigidbody_world;
  float ctime = DEG_get_ctime(depsgraph);
  DEG_debug_print_eval_time(depsgraph, __func__, ob->id.name, ob, ctime);
  /* read values pushed into RBO from sim/cache... */
  BKE_rigidbody_sync_transforms(rbw, ob, ctime);
}
