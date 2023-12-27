#include "lib_sess_uuid.h"

#include "lib_utildefines.h"

#include "atomic_ops.h"

/* Special val which indicates the UUID has not been assigned yet. */
#define lib_SESS_UUID_NONE 0

static const SessUUID global_sess_uuid_none = {LIB_SESS_UUID_NONE};

/* Denotes last used UUID.
 * It might eventually overflow, and easiest is to add more bits to it. */
static SessUUID global_sess_uuid = {LIB_SESS_UUID_NONE};

SessnUUID lib_sess_uuid_generate(void)
{
  SessUUID result;
  result.uuid_ = atomic_add_and_fetch_uint64(&global_sess_uuid.uuid_, 1);
  if (!lib_sess_uuid_is_generated(&result)) {
    /* Happens when the UUID overflows.
     * Just request the UUID once again, hoping that there are not a lot of high-priority threads
     * which will overflow the counter once again between the prev call and this one.
     * It is possible to have collisions after such overflow. */
    result.uuid_ = atomic_add_and_fetch_uint64(&global_sess_uuid.uuid_, 1);
  }
  return result;
}

bool lib_sess_uuid_is_generated(const SessUUID *uuid)
{
  return !lib_sess_uuid_is_equal(uuid, &global_sess_uuid_none);
}

bool lib_sess_uuid_is_equal(const SessUUID *lhs, const SessUUID *rhs)
{
  return lhs->uuid_ == rhs->uuid_;
}

uint64_t lib_sess_uuid_hash_uint64(const SessUUID *uuid)
{
  return uuid->uuid_;
}

uint lib_sess_uuid_ghash_hash(const void *uuid_v)
{
  const SessUUID *uuid = (const SessUUID *)uuid_v;
  return uuid->uuid_ & 0xffffffff;
}

bool lib_sess_uuid_ghash_compare(const void *lhs_v, const void *rhs_v)
{
  const SessUUID *lhs = (const SessUUID *)lhs_v;
  const SessUUID *rhs = (const SessUUID *)rhs_v;
  return !lib_sess_uuid_is_equal(lhs, rhs);
}
