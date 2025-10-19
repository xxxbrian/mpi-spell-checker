#include "sched_single.h"
#include <iostream>
#include "dictionary.h"

Misspelled RunSingle(const char* dict_text, size_t dict_size, const char* word_list_text,
                     size_t word_list_size) {
  using namespace std;

  // Original skeleton
  WordList word_list = BuildWordList(word_list_text, word_list_size);

  cout << "[*] Building dictionary with bk trees" << endl;
  Dictionary dict = Dictionary(dict_text, dict_size);

  cout << "\tDictionary size: " << dict.size << endl;
  cout << "\tWord list size: " << word_list.words.size() << endl;
  cout << "\tRoot len: " << word_list.root_len << endl;

  dict.BuildBKTree(word_list.root_len);

  cout << "[*] Getting misspelled words" << endl;
  Misspelled misspelled = GetMisspelled(word_list, dict);
  return misspelled;
}
