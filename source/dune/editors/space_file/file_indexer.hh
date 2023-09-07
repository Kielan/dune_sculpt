#pragma once

#include "ED_file_indexer.hh"

/**
 * Default indexer to use when listing files. The implementation is a no-operation indexing. When
 * set it won't use indexing. It is added to increase the code clarity.
 */
extern const FileIndexerType file_indexer_noop;
