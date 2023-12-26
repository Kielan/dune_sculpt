#include "lib_sess_uuid.h"

#include "lib_utildefines.h"

#include "atomic_ops.h"

/* Special val which indicates the UUID has not been assigned yet. */
#define lib_SESS_UUID_NONE 0

static const SessionUUID global_session_uuid_none = {LIB_SESS_UUID_NONE};

/* Denotes last used UUID.
 * It might eventually overflow, and easiest is to add more bits to it. */
static SessionUUID global_session_uuid = {LIB_SESS_UUID_NONE};

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

bool lib_sess_uuid_is_generated(const SessionUUID *uuid)
{
  return !lib_sess_uuid_is_equal(uuid, &global_session_uuid_none);
}

bool lib_sess_uuid_is_equal(const SessionUUID *lhs, const SessionUUID *rhs)
{
  return lhs->uuid_ == rhs->uuid_;
}

uint64_t lib_sess_uuid_hash_uint64(const SessionUUID *uuid)
{
  return uuid->uuid_;
}

uint lib_sess_uuid_ghash_hash(const void *uuid_v)
{
  const SessionUUID *uuid = (const SessionUUID *)uuid_v;
  return uuid->uuid_ & 0xffffffff;
}

bool lib_sess_uuid_ghash_compare(const void *lhs_v, const void *rhs_v)
{
  const SessionUUID *lhs = (const SessionUUID *)lhs_v;
  const SessionUUID *rhs = (const SessionUUID *)rhs_v;
  return !lib_sess_uuid_is_equal(lhs, rhs);
}
