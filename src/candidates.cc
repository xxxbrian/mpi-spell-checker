#include "candidates.h"
#include <omp.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include "dictionary.h"
using namespace std;

Misspelled GetMisspelled(const WordList& word_list, Dictionary& dict) {
  Misspelled misspelled;
  int num_threads = omp_get_max_threads();
  omp_set_num_threads(num_threads);
  cout << "[*] Number of threads: " << num_threads << endl;

#pragma omp parallel
  {
    Misspelled misspelled_local;

#pragma omp for schedule(dynamic)
    for (size_t i = 0; i < word_list.words.size(); ++i) {
      const auto& word = word_list.words[i];

      if (dict.Contains(word)) {
        continue;
      }

      Line line;
      line.word = word;
      dict.Candidates(word, &line.candidates);

      misspelled_local.push_back(line);
    }

#pragma omp critical
    {
      misspelled.insert(misspelled.end(), misspelled_local.begin(),
                        misspelled_local.end());
    }
  }

  return misspelled;
}

void SaveMisspelled(Misspelled& misspelled, FILE* file) {
  sort(misspelled.begin(), misspelled.end(), [](const Line& a, const Line& b) {
    if (a.candidates.size() == b.candidates.size()) {
      return strcmp(a.word.c_str(), b.word.c_str()) < 0;
    }
    return a.candidates.size() < b.candidates.size();
  });
  for (auto& line : misspelled) {
    vector<string> words;
    // words should be sorted in ascending order using strcmp.
    sort(line.candidates.begin(), line.candidates.end(),
         [](const string& a, const string& b) {
           return strcmp(a.c_str(), b.c_str()) < 0;
         });
    string words_str;
    for (auto& word : line.candidates) {
      words_str += " " + word;
    }
    words_str += "\n";
    string line_str;
    line_str = line.word + ":" + words_str;
    fwrite(line_str.c_str(), sizeof(char), line_str.size(), file);
  }
}