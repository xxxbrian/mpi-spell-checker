#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include "dictionary.h"

using namespace std;

TEST(Dictionary, ContainsExactWord) {
  string text =
      "apple\nMelbourne\nmRNA\nmicroMole\neducation\nhello\nworld\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());

  ASSERT_EQ(7, dict.size);
  ASSERT_EQ(20, dict.chars_size);

  ASSERT_TRUE(dict.Contains("apple"));
  ASSERT_TRUE(dict.Contains("Melbourne"));
  ASSERT_TRUE(dict.Contains("mRNA"));
  ASSERT_TRUE(dict.Contains("microMole"));
  ASSERT_TRUE(dict.Contains("education"));
  ASSERT_TRUE(dict.Contains("hello"));
  ASSERT_TRUE(dict.Contains("world"));
}

TEST(Dictionary, NotContainExactWord) {
  string text =
      "apple\nMelbourne\nmRNA\nmicroMole\neducation\nhello\nworld\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());
  ASSERT_FALSE(dict.Contains("banana"));
  ASSERT_FALSE(dict.Contains("Sydney"));
  ASSERT_FALSE(dict.Contains("DNA"));
  ASSERT_FALSE(dict.Contains("Mole"));
}

TEST(Dictionary, ContainsFirstLetterLowercase) {
  string text = "melbourne\nab\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());
  ASSERT_TRUE(dict.Contains("Melbourne"));
  ASSERT_TRUE(dict.Contains("Ab"));
}

TEST(Dictionary, NotContainsFirstLetterLowercase) {
  string text = "melbourne\nab\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());
  ASSERT_FALSE(dict.Contains("Melbuorne"));
  ASSERT_FALSE(dict.Contains("Abc"));
}

TEST(Dictionary, ContainsAllCapitalAnyCapitalization) {
  string text = "apple\nMelbourne\nmRNA\nmicroMole\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());
  ASSERT_TRUE(dict.Contains("APPLE"));
  ASSERT_TRUE(dict.Contains("MELBOURNE"));
  ASSERT_TRUE(dict.Contains("MRNA"));
  ASSERT_TRUE(dict.Contains("MICROMOLE"));
}

TEST(Dictionary, NotContainsAllCapitalAnyCapitalization) {
  string text = "apple\nMelbourne\nmRNA\nmicroMole\n";
  Dictionary dict = Dictionary(text.c_str(), text.size());
  ASSERT_FALSE(dict.Contains("ABC"));
  ASSERT_FALSE(dict.Contains("IEEE"));
}