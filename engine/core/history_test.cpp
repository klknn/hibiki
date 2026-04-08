#include "engine/core/history.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace hibiki {

TEST(HistoryTest, BasicUndoRedo) {
  hibiki::HistoryManager history;
  std::vector<uint8_t> s1 = {1, 2, 3};
  std::vector<uint8_t> s2 = {4, 5, 6};
  std::vector<uint8_t> current = s2;
  std::vector<uint8_t> out;

  history.pushState(s1);

  // Undo
  EXPECT_TRUE(history.undo(current, out));
  EXPECT_EQ(out, s1);

  // Redo
  EXPECT_TRUE(history.redo(s1, out));
  EXPECT_EQ(out, s2);
}

TEST(HistoryTest, UndoLimit) {
  hibiki::HistoryManager history;
  const int limit = 50;
  for (int i = 0; i < limit + 10; ++i) {
    history.pushState({(uint8_t)i});
  }

  std::vector<uint8_t> current = {100};
  std::vector<uint8_t> out;

  int undo_count = 0;
  while (history.undo(current, out)) {
    current = out;
    undo_count++;
  }
  EXPECT_EQ(undo_count, limit);
}

TEST(HistoryTest, RedoStackClearedOnPush) {
  hibiki::HistoryManager history;
  std::vector<uint8_t> s1 = {1};
  std::vector<uint8_t> s2 = {2};
  std::vector<uint8_t> s3 = {3};
  std::vector<uint8_t> out;

  history.pushState(s1);

  // Undo once
  std::vector<uint8_t> current = s2;
  EXPECT_TRUE(history.undo(current, out));

  // Push new state
  history.pushState(s3);

  // Redo should be impossible
  EXPECT_FALSE(history.redo(s3, out));
}

}  // namespace hibiki
