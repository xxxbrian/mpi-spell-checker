#ifndef CANDIDATES_H
#define CANDIDATES_H
#include <cstdio>
#include <string>
#include <vector>
#include "bk_tree.h"
#include "dictionary.h"

struct Line {
  std::string word;
  std::vector<std::string> candidates;
};

using Misspelled = std::vector<Line>;

Misspelled GetMisspelled(const WordList& word_list, Dictionary& dict);
// the last argument is the file handler
void SaveMisspelled(Misspelled& misspelled, FILE* file);
#endif  // CANDIDATES_H
