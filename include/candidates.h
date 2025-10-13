#ifndef CANDIDATES_H
#define CANDIDATES_H
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "bk_tree.h"
#include "dictionary.h"
using namespace std;

struct Line {
  string word;
  vector<string> candidates;
};

using Misspelled = vector<Line>;

Misspelled GetMisspelled(const WordList& word_list, Dictionary& dict);
// the last argument is the file handler
void SaveMisspelled(Misspelled& misspelled, FILE* file);
#endif  // CANDIDATES_H
