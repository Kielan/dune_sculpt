
#include <set>

#include "aud_set.h"

void *aud_create_set()
{
  return new std::set<void *>();
}

void aud_destroy_set(void *set)
{
  delete reinterpret_cast<std::set<void *> *>(set);
}

char aud_remove_set(void *set, void *entry)
{
  if (set)
    return reinterpret_cast<std::set<void *> *>(set)->erase(entry);
  return 0;
}

void AUD_addSet(void *set, void *entry)
{
  if (entry)
    reinterpret_cast<std::set<void *> *>(set)->insert(entry);
}

void *AUD_getSet(void *set)
{
  if (set) {
    std::set<void *> *rset = reinterpret_cast<std::set<void *> *>(set);
    if (!rset->empty()) {
      std::set<void *>::iterator it = rset->begin();
      void *result = *it;
      rset->erase(it);
      return result;
    }
  }

  return (void *)0;
}
