#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "candidates.h"
#include "dictionary.h"

using namespace std;
using ::testing::_;
using ::testing::Matcher;

TEST(Candidates, GetMisspelled) {
  string words_text = "stabX\nappel\ntim\nstap\nrandpm\nbananaa\n";
  WordList word_list = BuildWordList(words_text.c_str(), words_text.size());

  string dict_text =
      "appeal\napple\nrappel\nTim\ntime\nvim\nsap\nsnap\nstab"
      "\nstep\nstop\nstrap\ntap\nhello\n";
  Dictionary dict = Dictionary(dict_text.c_str(), dict_text.size());
  dict.BuildBKTree(word_list.root_len);

  Misspelled misspelled = GetMisspelled(word_list, dict);
  for (auto& line : misspelled) {
    if (line.word == "appel") {
      EXPECT_EQ(line.candidates.size(), 3);
    } else if (line.word == "tim") {
      EXPECT_EQ(line.candidates.size(), 3);
    } else if (line.word == "stap") {
      EXPECT_EQ(line.candidates.size(), 7);
    } else if (line.word == "randpm") {
      EXPECT_EQ(line.candidates.size(), 0);
    } else if (line.word == "bananaa") {
      EXPECT_EQ(line.candidates.size(), 0);
    } else if (line.word == "stabX") {
#define DELETED_OR_REPLACED_OR_ADDED 1
#ifdef DELETED_OR_REPLACED_OR_ADDED
      EXPECT_EQ(line.candidates.size(), 0);
#else
      EXPECT_EQ(line.candidates.size(), 1);
#endif
    } else {
      cout << "Unexpected word: " << line.word << endl;
      EXPECT_EQ(0, 1);
    }
  }
}

// Test fixture for file I/O tests
class FileIOTest : public ::testing::Test {
 protected:
  void SetUp() override {}

  void TearDown() override { remove("test_word_list_misspelled.txt"); }
};

namespace {
using ::testing::_;
using ::testing::Return;

class FileMock {
 public:
  MOCK_METHOD(int, fputc, (int, FILE*));
  MOCK_METHOD(size_t, fwrite, (const void*, size_t, size_t, FILE*));

  static FileMock* instance;
};

FileMock* FileMock::instance = nullptr;

// Mocked fputc
extern "C" int fputc(int character, FILE* stream) {
  return FileMock::instance->fputc(character, stream);
}

// Mocked fwrite
extern "C" size_t fwrite(const void* ptr, size_t size, size_t count,
                         FILE* stream) {
  return FileMock::instance->fwrite(ptr, size, count, stream);
}
}  // namespace

MATCHER_P3(MemcmpMatcher, expected, size, count, "") {
  return std::memcmp(arg, expected, size * count) == 0;
}

TEST(FileIOTest, SaveMisspelled) {
  FileMock file_mock;
  FileMock::instance = &file_mock;

  FILE* file = fopen("test_word_list_misspelled.txt", "w");
  ASSERT_NE(file, nullptr);
  testing::Sequence s;

  // No BOM for ASCII output
  EXPECT_CALL(file_mock,
              fwrite(MemcmpMatcher("bananaa:\n", sizeof(char), 9),
                     sizeof(char), 9, file))
      .Times(1);
  EXPECT_CALL(file_mock, fwrite(MemcmpMatcher("randpm:\n", sizeof(char), 8),
                                sizeof(char), 8, file))
      .Times(1);
  EXPECT_CALL(file_mock, fwrite(MemcmpMatcher("appel: appeal apple rappel\n",
                                              sizeof(char), 27),
                                sizeof(char), 27, file))
      .Times(1);
  EXPECT_CALL(file_mock,
              fwrite(MemcmpMatcher("tim: Tim time vim\n", sizeof(char), 18),
                     sizeof(char), 18, file))
      .Times(1);
  EXPECT_CALL(file_mock,
              fwrite(MemcmpMatcher("stap: sap snap stab step stop strap tap\n",
                                   sizeof(char), 40),
                     sizeof(char), 40, file))
      .Times(1);

  string words_text = "appel\ntim\nstap\nrandpm\nbananaa\n";
  WordList word_list = BuildWordList(words_text.c_str(), words_text.size());

  string dict_text =
      "appeal\napple\nrappel\nTim\ntime\nvim\nsap\nsnap\nstab"
      "\nstep\nstop\nstrap\ntap\nhello\n";
  Dictionary dict = Dictionary(dict_text.c_str(), dict_text.size());
  dict.BuildBKTree(word_list.root_len);
  Misspelled misspelled = GetMisspelled(word_list, dict);
  SaveMisspelled(misspelled, file);

  fclose(file);
  FileMock::instance = nullptr;
}
