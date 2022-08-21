#include "qute/iterator.h"
#include "qute/query_parser.h"

#include <map>

#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

namespace qute {

using Pl = std::vector<DocId>;

struct SimpleIteratorFactory : QueryParser::IteratorFactory {
  std::map<std::string, std::vector<DocId>> invertedIndex;
  IteratorPtr getIteratorForTerm(const std::string& term) override {
    auto mapItr = invertedIndex.find(term);
    return mapItr == invertedIndex.end()
               ? Iterator::getEmptyIterator()
               : Iterator::getVectorIterator(mapItr->second);
  }
};

TEST(CreateQueryParse, Basic) {
  SimpleIteratorFactory sif;
  QueryParser qp(sif);
  EXPECT_TRUE(qp.getIterator("(and a b c)"));
}

class QueryParserTestFixture : public testing::Test {
 protected:
  void SetUp() override { reset(); }
  void reset() {
    VLOG(1) << "There are " << termToPl_.size() << " posting-lists.";
    sif_.invertedIndex = termToPl_;
    query_ = std::make_unique<QueryParser>(sif_);
  }
  void TearDown() override {}

  std::map<std::string, std::vector<DocId>> termToPl_;
  SimpleIteratorFactory sif_;
  std::unique_ptr<QueryParser> query_;
};

TEST_F(QueryParserTestFixture, BasicParse) {
  EXPECT_THROW(query_->getIterator("   "), QueryParser::Exception);
  EXPECT_NO_THROW(query_->getIterator(" t:haha \n  \n"));
  EXPECT_NO_THROW(query_->getIterator("(and t:haha) \n  \n"));
  EXPECT_THROW(query_->getIterator("(diff t:haha) \n  \n"),
               QueryParser::Exception);
  EXPECT_NO_THROW(query_->getIterator(R"(
      (and t:haha
           (or t:haha
               tk:hehe
               tt:dafasd
               (diff t:a t:b)
          )
      )
    )"));

  EXPECT_THROW(query_->getIterator(R"(
      (and t:haha
           (or t:haha
               tk:hehe
               tt:dafasd
               (diff t:a t:b
          )
      )
    )"),
               QueryParser::Exception);

  EXPECT_THROW(query_->getIterator(R"(
      (and t:haha
           (or t:haha
               tk:hehe
               tt:dafasd
               (diff t:a)
          )
      )
    )"),
               QueryParser::Exception);
}

TEST_F(QueryParserTestFixture, EmptyItr) {
  std::string sq = "t:haha";
  for (int i = 0; i < 100; ++i) {
    sq = "(  and " + sq + "   ) \n \n";
  }
  IteratorPtr itr;
  EXPECT_NO_THROW(itr = query_->getIterator(sq));
  EXPECT_TRUE(dynamic_cast<EmptyIterator*>(itr.get()));

  sq = "(diff t:aa " + sq + ")";

  EXPECT_NO_THROW(itr = query_->getIterator(sq));
  EXPECT_FALSE(dynamic_cast<EmptyIterator*>(itr.get()));
  EXPECT_TRUE(dynamic_cast<DiffIterator*>(itr.get()));
}

TEST_F(QueryParserTestFixture, TestIterators) {
  termToPl_.emplace("t:facebook", Pl({0, 3, 5, 8}));
  termToPl_.emplace("c:facebook", Pl({0, 2, 8, 9, 13}));

  termToPl_.emplace("t:google", Pl({2, 3, 6}));
  termToPl_.emplace("c:google", Pl({1, 3, 6, 7}));

  reset();

  auto itr = query_->getIterator(R"(
    ( or (and t:facebook c:facebook)
         (and 
              t:google
              c:google))
  )");

  ASSERT_TRUE(itr->valid());
  EXPECT_EQ(0, itr->value());

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(3, itr->value());

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(6, itr->value());

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(8, itr->value());

  EXPECT_FALSE(itr->next());
}

TEST_F(QueryParserTestFixture, TestIteratorsWithTags) {
  termToPl_.emplace("t:facebook", Pl({0, 3, 5, 8, 99}));
  termToPl_.emplace("c:facebook", Pl({0, 2, 8, 9, 13, 99}));

  termToPl_.emplace("t:google", Pl({2, 3, 6, 99}));
  termToPl_.emplace("c:google", Pl({1, 3, 6, 7, 99}));

  termToPl_.emplace("c:apple", Pl({100}));

  reset();

  auto itr = query_->getIterator(R"(
   (diff
    ( or tag:or (and tag:fb t:facebook c:facebook)
         (and 
              t:google
              c:google tag:goog)

          (or tag:aapl c:apple)
         )
      c:no_pl
    )
  )");

  using Tags = std::vector<std::string>;

  ASSERT_TRUE(itr->valid());
  EXPECT_EQ(0, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"fb", "or"}));

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(3, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"goog", "or"}));

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(6, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"goog", "or"}));

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(8, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"fb", "or"}));

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(99, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"fb", "goog", "or"}));

  ASSERT_TRUE(itr->next());
  EXPECT_EQ(100, itr->value());
  EXPECT_EQ(itr->getTags(), Tags({"aapl", "or"}));

  EXPECT_FALSE(itr->next());
}

TEST_F(QueryParserTestFixture, TestNestedOperators) {
  auto testOne = [this](const std::string& op) {
    std::string q = "term";
    for (int i = 0; i < 200; ++i) {
      q = "(" + op + "\n" + q + "\n)";
    }
    EXPECT_TRUE(dynamic_cast<EmptyIterator*>(query_->getIterator(q).get()));
  };
  SCOPED_TRACE("Test operator: and");
  testOne("and");

  SCOPED_TRACE("Test operator: or");
  testOne("or");
}

TEST_F(QueryParserTestFixture, RandomTermIteratorTest) {
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
  auto getRandomVec = [&](size_t& bitMask) {
    bitMask = dist(rnd);
    return toDocVector(bitMask);
  };

  size_t numExpectedDocs = 0;
  auto runTest = [&, this]() {
    size_t ma, mb, mc, md, me;
    auto a = getRandomVec(ma);
    auto b = getRandomVec(mb);
    auto c = getRandomVec(mc);
    auto d = getRandomVec(md);
    auto e = getRandomVec(me);

    termToPl_["a"] = a;
    termToPl_["b"] = b;
    termToPl_["c"] = c;
    termToPl_["d"] = d;
    termToPl_["e"] = e;

    reset();

    auto itr = query_->getIterator("(diff (and a (or b c) d ) e )");
    auto m = (ma & (mb | mc) & md);
    m ^= (m & me);
    const auto expectedDocs = toDocVector(m);
    numExpectedDocs += expectedDocs.size();
    VLOG(1) << "There are " << expectedDocs.size() << " expected docs.";

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
