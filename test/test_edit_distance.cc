#include <gtest/gtest.h>
#include "edit_distance.h"
#include "string"

using namespace std;

TEST(EditDistance, Empty) {
  ASSERT_EQ(0, EditDistance("", "", true));
  ASSERT_EQ(3, EditDistance("any", "", true));
  ASSERT_EQ(8, EditDistance("", "anything", true));
}

TEST(EditDistance, Zero) {
  ASSERT_EQ(0, EditDistance("apple", "apple", true));
  ASSERT_EQ(0, EditDistance("", "", true));
  ASSERT_EQ(0, EditDistance("hello", "hello", true));
}

TEST(EditDistance, OneSubstitution) {
  ASSERT_EQ(1, EditDistance("apple", "bpple", true));
  ASSERT_EQ(1, EditDistance("apple", "abple", true));
  ASSERT_EQ(1, EditDistance("apple", "apcle", true));
  ASSERT_EQ(1, EditDistance("apple", "appce", true));
  ASSERT_EQ(1, EditDistance("apple", "appld", true));
  ASSERT_EQ(1, EditDistance("hello", "hallo", true));
  ASSERT_EQ(1, EditDistance("world", "wdrld", true));
  ASSERT_EQ(1, EditDistance("test", "best", true));
}

TEST(EditDistance, OneDeletion) {
  ASSERT_EQ(1, EditDistance("apple", "pple", true));
  ASSERT_EQ(1, EditDistance("apple", "aple", true));
  ASSERT_EQ(1, EditDistance("apple", "aple", true));
  ASSERT_EQ(1, EditDistance("apple", "appe", true));
  ASSERT_EQ(1, EditDistance("apple", "appl", true));
  ASSERT_EQ(1, EditDistance("hello", "hllo", true));
  ASSERT_EQ(1, EditDistance("world", "wrld", true));
  ASSERT_EQ(1, EditDistance("test", "est", true));
}

TEST(EditDistance, OneAddition) {
  ASSERT_EQ(1, EditDistance("apple", "bapple", true));
  ASSERT_EQ(1, EditDistance("apple", "acpple", true));
  ASSERT_EQ(1, EditDistance("apple", "apdple", true));
  ASSERT_EQ(1, EditDistance("apple", "appele", true));
  ASSERT_EQ(1, EditDistance("apple", "applfe", true));
  ASSERT_EQ(1, EditDistance("apple", "applex", true));
  ASSERT_EQ(1, EditDistance("hello", "hellox", true));
}

TEST(EditDistance, OneAdjacentSwap) {
  ASSERT_EQ(1, EditDistance("apple", "aplpe", true));
  ASSERT_EQ(1, EditDistance("apple", "appel", true));
  ASSERT_EQ(1, EditDistance("appel", "apple", true));
  ASSERT_EQ(1, EditDistance("apple", "paple", true));
  ASSERT_EQ(1, EditDistance("hello", "ehllo", true));
}

TEST(EditDistance, TwoSubstitution) {
  ASSERT_EQ(2, EditDistance("apple", "aqqle", true));
  ASSERT_EQ(2, EditDistance("apple", "eppla", true));
  ASSERT_EQ(2, EditDistance("apple", "appid", true));
  ASSERT_EQ(2, EditDistance("hello", "haxlo", true));
}

TEST(EditDistance, TwoDeletion) {
  ASSERT_EQ(2, EditDistance("apple", "ple", true));
  ASSERT_EQ(2, EditDistance("apple", "ale", true));
  ASSERT_EQ(2, EditDistance("apple", "ape", true));
  ASSERT_EQ(2, EditDistance("apple", "app", true));

  ASSERT_EQ(2, EditDistance("apple", "ppl", true));
  ASSERT_EQ(2, EditDistance("apple", "ape", true));
  ASSERT_EQ(2, EditDistance("apple", "ppe", true));

  ASSERT_EQ(3, EditDistance("hello", "ho", true));
  ASSERT_EQ(2, EditDistance("hello", "llo", true));
}

TEST(EditDistance, TwoAddition) {
  ASSERT_EQ(2, EditDistance("apple", "adpplea", true));
  ASSERT_EQ(2, EditDistance("apple", "abppble", true));
  ASSERT_EQ(2, EditDistance("apple", "aprplre", true));
  ASSERT_EQ(2, EditDistance("apple", "applrre", true));
  ASSERT_EQ(2, EditDistance("apple", "applerr", true));
  ASSERT_EQ(2, EditDistance("hello", "xhellox", true));
  ASSERT_EQ(2, EditDistance("world", "wxorldx", true));
}

TEST(EditDistance, ThreeSubstitution) {
  ASSERT_EQ(3, EditDistance("apple", "epaie", true));
  ASSERT_EQ(3, EditDistance("apple", "dqqle", true));
  ASSERT_EQ(3, EditDistance("apple", "abbpe", true));
  ASSERT_EQ(3, EditDistance("hello", "xyzlo", true));
}

TEST(EditDistance, ThreeDeletion) {
  ASSERT_EQ(3, EditDistance("apple", "ap", true));
  ASSERT_EQ(3, EditDistance("apple", "pe", true));
  ASSERT_EQ(3, EditDistance("apple", "pl", true));
  ASSERT_EQ(3, EditDistance("apple", "pp", true));
  ASSERT_EQ(4, EditDistance("beautiful", "beaut", true));
}

TEST(EditDistance, ThreeAddition) {
  ASSERT_EQ(9, EditDistance("world", "beautifulworld", true));
  ASSERT_EQ(8, EditDistance("test", "testworldxyz", true));
}