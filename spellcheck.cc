#include <chrono>
#include <iostream>
#include <string>
#include <unordered_set>
#include "include/candidates.h"
#include "include/dictionary.h"
#include "include/load_files.h"

using namespace std;
using namespace std::chrono;

int main(int argc, char** argv) {
  if (argc != 3) {
    cout << "Usage: " << argv[0] << " <dictionary> <word_list>" << endl;
    return 1;
  }

  auto start = high_resolution_clock::now();  // Start timing

  // Load dictionary and word list
  char* filename_dict = argv[1];
  char* filename_word_list = argv[2];

  cout << "[*] Using dictionary: " << filename_dict << endl;
  cout << "[*] Using word list: " << filename_word_list << endl;

  size_t dict_size, word_list_size;
  char* dict_text = LoadFile(filename_dict, &dict_size);
  char* word_list_text = LoadFile(filename_word_list, &word_list_size);
  WordList word_list = BuildWordList(word_list_text, word_list_size);

  // build BK tree
  cout << "[*] Building dictionary with bk trees" << endl;
  Dictionary dict = Dictionary(dict_text, dict_size);

  cout << "\tDictionary size: " << dict.size << endl;
  cout << "\tDict Chars size " << dict.chars_size << endl;
  cout << "\tWord list size: " << word_list.words.size() << endl;
  cout << "\tRoot len: " << word_list.root_len << endl;

  dict.BuildBKTree(word_list.root_len);

  // Get misspelled words
  cout << "[*] Getting misspelled words" << endl;

  Misspelled misspelled = GetMisspelled(word_list, dict);
  // Format and save misspelled words

  const char* out_filename = "results/word_list_misspelled.txt";
  FILE* outfile = fopen(out_filename, "w");
  SaveMisspelled(misspelled, outfile);
  fclose(outfile);

  free(dict_text);
  free(word_list_text);

  // Show wall time
  auto end = high_resolution_clock::now();  // End timing
  auto duration = duration_cast<milliseconds>(end - start).count();
  // Convert duration to minutes, seconds, and milliseconds
  auto minutes = duration / 60000;
  auto seconds = (duration % 60000) / 1000;
  auto milliseconds = duration % 1000;
  cout << "[*] Output: " << out_filename << endl;
  cout << "[*] Wall time: " << minutes << " mins " << seconds << " secs "
       << milliseconds << " ms" << "; " << duration << endl;
  return 0;
}
