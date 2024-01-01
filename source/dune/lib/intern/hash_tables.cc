#include "lib_hash_tables.hh"
#include "lib_string.h"

#include <iostream>

void dune::HashTableStats::print(const char *name) const
{
  std::cout << "Hash Table Stats: " << name << "\n";
  std::cout << "  Address: " << address_ << "\n";
  std::cout << "  Total Slots: " << capacity_ << "\n";
  std::cout << "  Occupied Slots:  " << size_ << " (" << load_factor_ * 100.0f << " %)\n";
  std::cout << "  Removed Slots: " << removed_amount_ << " (" << removed_load_factor_ * 100.0f
            << " %)\n";

  char mem_size_str[LIB_STR_FORMAT_INT64_BYTE_UNIT_SIZE];
  lib_str_format_byte_unit(mem_size_str, size_in_bytes_, true);
  std::cout << "  Size: ~" << mem_size_str << "\n";
  std::cout << "  Size per Slot: " << size_per_element_ << " bytes\n";

  std::cout << "  Avg Collisions: " << avg_collisions_ << "\n";
  for (int64_t collision_count : keys_by_collision_count_.index_range()) {
    std::cout << "  " << collision_count
              << " Collisions: " << keys_by_collision_count_[collision_count] << "\n";
  }
}
