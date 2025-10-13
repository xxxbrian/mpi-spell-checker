#pragma once
#include <cstddef>
#include "candidates.h"

// BuildWordList + Dictionary + BKTree + GetMisspelled
Misspelled RunSingle(const char* dict_text, size_t dict_size,
                     const char* word_list_text, size_t word_list_size);
