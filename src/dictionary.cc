#include "dictionary.h"
#include <cctype>
#include <string>
#include <unordered_set>

using namespace std;

void Dictionary::Build(const char* text, size_t text_size) {
  size_t i = 0;
  while (i < text_size) {
    string word;
    while (i < text_size && text[i] != '\n') {
      word.push_back(text[i]);
      chars.insert(text[i]);
      ++i;
    }
    if (word.empty()) {
      ++i;
      continue;
    }
    orig.insert(word);
    size += 1;
    // prepare for bk tree building
    len_to_words[word.size()].push_back(word);
    // prepare for check whether the capitalization is in the dictionary
    string word_cap = word;
    for (char& j : word_cap) {
      j = toupper(j);
    }
    cap.insert(word_cap);
  }
  chars_size = chars.size();
}

void Dictionary::BuildBKTree(size_t root_len) {
  std::string root_word = len_to_words[root_len][0];
  bk_tree = BKTree(root_word);
  for (const auto& word : orig) {
    bk_tree.AddNode(word);
  }
}

bool Dictionary::Contains(const std::string& word) {
  if (orig.find(word) != orig.end()) {
    return true;
  }
  // the word with the first letter converted to lower case is in the dictionary
  if (isupper(word[0])) {
    // if the first letter is not lowercase, then convert it to lowercase
    string lower_word = word;
    lower_word[0] = tolower(lower_word[0]);
    if (orig.find(lower_word) != orig.end()) {
      return true;
    }
  }
  // it is all capital
  bool is_all_capital = true;
  for (auto c : word) {
    if (islower(c)) {
      is_all_capital = false;
      break;
    }
  }
  if (is_all_capital) {
    if (cap.find(word) != cap.end()) {
      // any capitalization is in the dictionary
      return true;
    }
  }
  return false;
}

#define DELETED_OR_REPLACED_OR_ADDED 1

void Dictionary::Candidates(const std::string& word,
                            std::vector<std::string>* result) {
  bk_tree.SearchNode(word, 2, result);
#ifdef DELETED_OR_REPLACED_OR_ADDED
  // if the replaced or deleted character is not in the dictionary, then remove it
  std::vector<std::string> tmp_res;

  unordered_set<char> word_chars;
  for (auto c : word) {
    word_chars.insert(c);
  }
  unordered_set<char>* shorter;
  unordered_set<char>* longer;
  for (auto& w : *result) {
    unordered_set<char> res_word_chars;
    for (auto c : w) {
      res_word_chars.insert(c);
    }
    if (word.size() > w.size()) {
      shorter = &res_word_chars;
      longer = &word_chars;
    } else {
      shorter = &word_chars;
      longer = &res_word_chars;
    }
    // find the different character
    char diff_char;
    bool has_diff_char = false;
    for (auto c : *longer) {
      if (shorter->find(c) == shorter->end()) {
        diff_char = c;
        has_diff_char = true;
        break;
      }
    }
    // dictionary does not contain the diff char; the word is not candidate
    if (has_diff_char) {
      if (chars.find(diff_char) != chars.end()) {
        tmp_res.push_back(w);
      }
    } else {
      tmp_res.push_back(w);
    }
  }
  if (tmp_res.size() == result->size()) {
    return;
  }
  // clear the result and copy the tmp_res to the result
  result->clear();
  for (auto& w : tmp_res) {
    result->push_back(w);
  }
#endif
}

WordList BuildWordList(const char* text, size_t text_size) {
  std::unordered_map<size_t, size_t> lens_count;
  WordList word_list = WordList();
  size_t i = 0;
  size_t max_count = 0;
  while (i < text_size) {
    string word;
    while (i < text_size && text[i] != '\n') {
      word.push_back(text[i]);
      ++i;
    }
    if (word.empty()) {
      ++i;
      continue;
    }
    word_list.words.push_back(word);
    size_t len = word.size();
    if (lens_count.find(len) == lens_count.end()) {
      lens_count[len] = 1;
    } else {
      lens_count[len] += 1;
    }
    if (lens_count[len] > max_count) {
      max_count = lens_count[len];
      word_list.root_len = len;
    }
  }
  return word_list;
}
