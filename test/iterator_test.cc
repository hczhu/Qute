#include "qute/iterator.h"

#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace qute {

TEST(TestEmptyIterator, Basic) {
  auto itr = Iterator::getEmptyIterator();

  EXPECT_FALSE(itr->next());
  EXPECT_FALSE(itr->skipTo(1));
  EXPECT_FALSE(itr->valid());
  EXPECT_EQ(itr->remainingDocs(), 0);
}

TEST(TestVectorIterator, Basic) {
  std::vector<DocId> pl = {1, 2, 4, 7, 8, 10, 100};
  auto itr = Iterator::getVectorIterator(pl);

  std::vector<DocId> result;
  itr->iterateWith([&result](const DocId id) { result.push_back(id); });
  EXPECT_EQ(result, pl);

  itr = Iterator::getVectorIterator(pl);

  EXPECT_EQ(itr->value(), 1);
  EXPECT_TRUE(itr->next());
  EXPECT_EQ(itr->value(), 2);
  EXPECT_TRUE(itr->skipTo(2));
  EXPECT_EQ(itr->value(), 2);
  EXPECT_EQ(itr->remainingDocs(), 6);

  EXPECT_TRUE(itr->skipTo(11));
  EXPECT_EQ(itr->value(), 100);
  EXPECT_EQ(itr->remainingDocs(), 1);

  EXPECT_FALSE(itr->next());
  EXPECT_FALSE(itr->valid());

  itr = Iterator::getVectorIterator(pl);
  EXPECT_TRUE(itr->skipTo(5));
  EXPECT_EQ(itr->value(), 7);
  EXPECT_TRUE(itr->skipTo(8));
  EXPECT_EQ(itr->value(), 8);
  EXPECT_TRUE(itr->skipTo(9));
  EXPECT_EQ(itr->value(), 10);
  EXPECT_TRUE(itr->skipTo(10));
  EXPECT_EQ(itr->value(), 10);
  EXPECT_TRUE(itr->skipTo(99));
  EXPECT_EQ(itr->value(), 100);
  EXPECT_FALSE(itr->skipTo(101));
}

TEST(TestAndIterator, Basic) {
  auto getItr = []() {
    return makeIterator<AndIterator>(
        toVector(Iterator::getVectorIterator({0, 3, 8, 11, 20, 21}),
                 Iterator::getVectorIterator({0, 4, 8, 21, 31}),
                 Iterator::getVectorIterator({0, 8, 21, 22, 31, 41})));
  };

  auto itr = getItr();
  const std::vector<DocId> expectedDocs = {0, 8, 21};
  std::vector<DocId> realDocs;
  itr->iterateWith([&realDocs](auto id) { realDocs.push_back(id); });
  EXPECT_EQ(expectedDocs, realDocs);
  EXPECT_FALSE(itr->valid());

  itr = getItr();
  EXPECT_EQ(itr->value(), 0);
  EXPECT_TRUE(itr->skipTo(9));
  EXPECT_EQ(itr->value(), 21);
  EXPECT_FALSE(itr->next());
}

TEST(TestOrIterator, Basic) {
  auto getItr = []() {
    return makeIterator<OrIterator>(
        toVector(Iterator::getVectorIterator({0, 8, 20, 21}),
                 Iterator::getVectorIterator({0, 4, 8, 21}),
                 Iterator::getVectorIterator({0, 8, 22, 31, 41})));
  };

  auto itr = getItr();
  const std::vector<DocId> expectedDocs = {0, 4, 8, 20, 21, 22, 31, 41};
  std::vector<DocId> realDocs;
  itr->iterateWith([&realDocs](auto id) { realDocs.push_back(id); });
  EXPECT_EQ(expectedDocs, realDocs);
  EXPECT_FALSE(itr->valid());

  itr = getItr();
  EXPECT_EQ(itr->value(), 0);
  EXPECT_TRUE(itr->skipTo(9));
  EXPECT_EQ(itr->value(), 20);
  EXPECT_TRUE(itr->skipTo(20));
  EXPECT_EQ(itr->value(), 20);
  EXPECT_TRUE(itr->skipTo(32));
  EXPECT_EQ(itr->value(), 41);
  EXPECT_FALSE(itr->next());
}

TEST(TestDiffIterator, Basic) {
  auto getItr = []() {
    return makeIterator<DiffIterator>(
        Iterator::getVectorIterator({0, 3, 8, 19, 20, 21}),
        Iterator::getVectorIterator({0, 4, 8, 9, 10, 21, 32}));
  };

  auto itr = getItr();
  const std::vector<DocId> expectedDocs = {3, 19, 20};
  std::vector<DocId> realDocs;
  itr->iterateWith([&realDocs](auto id) { realDocs.push_back(id); });
  EXPECT_EQ(expectedDocs, realDocs);
  EXPECT_FALSE(itr->valid());

  itr = getItr();
  EXPECT_EQ(itr->value(), 3);
  EXPECT_TRUE(itr->skipTo(19));
  EXPECT_EQ(itr->value(), 19);
  EXPECT_TRUE(itr->skipTo(20));
  EXPECT_EQ(itr->value(), 20);
  EXPECT_FALSE(itr->next());
}

TEST(TestCompoundIterator, Basic) {
  auto getItr = []() {
    auto a = Iterator::getVectorIterator({0, 3, 4, 7, 8, 19, 20, 21, 22});
    auto b = Iterator::getVectorIterator({0, 19, 20, 21, 41, 100});
    auto c = Iterator::getVectorIterator({3, 8, 19, 21, 31});
    auto d = Iterator::getVectorIterator({0, 4, 5, 8, 10, 19, 21, 33});
    auto e = Iterator::getVectorIterator({0, 21});

    // (diff (and a (or b c) d) e)
    auto bc = makeIterator<OrIterator>(toVector(std::move(b), std::move(c)));
    auto abcd = makeIterator<AndIterator>(
        toVector(std::move(a), std::move(bc), std::move(d)));
    return makeIterator<DiffIterator>(std::move(abcd), std::move(e));
  };

  auto itr = getItr();
  const std::vector<DocId> expectedDocs = {8, 19};
  std::vector<DocId> realDocs;
  itr->iterateWith([&realDocs](auto id) { realDocs.push_back(id); });
  EXPECT_EQ(expectedDocs, realDocs);
  EXPECT_FALSE(itr->valid());

  itr = getItr();
  EXPECT_EQ(itr->value(), 8);
  EXPECT_TRUE(itr->skipTo(9));
  EXPECT_EQ(itr->value(), 19);
  EXPECT_TRUE(itr->skipTo(19));
  EXPECT_EQ(itr->value(), 19);
  EXPECT_FALSE(itr->next());
}

TEST(TestCompoundIterator, RandomTest) {
  auto toDocVector = [](size_t bitMask) {
    std::vector<DocId> vec;
    for (DocId i = 0; i < 64; ++i, bitMask >>= 1) {
      if (bitMask & 1) {
        vec.push_back(i);
      }
    }
    return vec;
  };

  std::mt19937 rnd(std::time(nullptr));
  std::uniform_int_distribution<size_t> dist;
  auto getRandomItr = [&](size_t& bitMask) {
    bitMask = dist(rnd);
    return Iterator::getVectorIterator(toDocVector(bitMask));
  };
  size_t numExpectedDocs = 0;
  auto runTest = [&]() {
    size_t ma, mb, mc, md, me;
    auto a = getRandomItr(ma);
    auto b = getRandomItr(mb);
    auto c = getRandomItr(mc);
    auto d = getRandomItr(md);
    auto e = getRandomItr(me);

    // (diff (and a (or b c) d) e)
    auto m = (ma & (mb | mc) & md);
    m ^= (m & me);
    const auto expectedDocs = toDocVector(m);
    numExpectedDocs += expectedDocs.size();
    VLOG(1) << "There are " << expectedDocs.size() << " expected docs.";

    auto bc = makeIterator<OrIterator>(toVector(std::move(b), std::move(c)));
    auto abcd = makeIterator<AndIterator>(
        toVector(std::move(a), std::move(bc), std::move(d)));
    auto itr = makeIterator<DiffIterator>(std::move(abcd), std::move(e));

    std::vector<DocId> realDocs;
    itr->iterateWith([&realDocs](auto id) { realDocs.push_back(id); });

    EXPECT_EQ(expectedDocs, realDocs);
  };
  size_t numRuns = 1000;
  for (size_t i = 0; i < numRuns; ++i) {
    runTest();
  }
  LOG(INFO) << "On average there are " << numExpectedDocs / numRuns
            << " expected Docs";
}

} // namespace qute
