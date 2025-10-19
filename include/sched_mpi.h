#pragma once
#include <cstddef>
#include "candidates.h"

Misspelled RunMpi(const char* dict_text, size_t dict_size, const char* word_list_text,
                  size_t word_list_size, bool use_bloom);
