#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "bk_tree.h"
#include "edit_distance.h"

using namespace std;
using namespace testing;

TEST(BKTree, GetCandidates) {
  BKTree bk_tree("appeal");
  bk_tree.AddNode("apple");
  bk_tree.AddNode("rappel");
  bk_tree.AddNode("Tim");
  bk_tree.AddNode("time");
  bk_tree.AddNode("vim");
  bk_tree.AddNode("sap");
  bk_tree.AddNode("snap");
  bk_tree.AddNode("stab");
  bk_tree.AddNode("step");
  bk_tree.AddNode("stop");
  bk_tree.AddNode("strap");
  bk_tree.AddNode("tap");

  bk_tree.AddNode("random");
  bk_tree.AddNode("wont");
  bk_tree.AddNode("be");
  bk_tree.AddNode("used");

  vector<string> res_a;
  bk_tree.SearchNode("appel", 2, &res_a);

  EXPECT_EQ(res_a.size(), 3);
  EXPECT_THAT(res_a, UnorderedElementsAre("appeal", "apple", "rappel"));

  vector<string> res_b;
  bk_tree.SearchNode("tim", 2, &res_b);
  EXPECT_EQ(res_b.size(), 3);
  EXPECT_THAT(res_b, UnorderedElementsAre("Tim", "time", "vim"));

  vector<string> res_c;
  bk_tree.SearchNode("stap", 2, &res_c);
  EXPECT_EQ(res_c.size(), 7);
  EXPECT_THAT(res_c, UnorderedElementsAre("sap", "snap", "stab", "step",
                                          "stop", "strap", "tap"));
}
