#include "query_parser.h"

#include <cctype>
#include <functional>
#include <iterator>
#include <sstream>
#include <stack>
#include <string_view>
#include <unordered_map>

#include <glog/logging.h>

namespace qute {

namespace {

using Iterators = std::vector<IteratorPtr>;
const std::string kTagPrefix = "tag:";

template <typename Arg>
void toStrImpl(std::ostringstream& oss, const Arg& field) {
  oss << field;
}

template <typename Arg1, typename... Args>
void toStrImpl(std::ostringstream& oss,
               const Arg1& field,
               const Args&... fields) {
  oss << field;
  toStrImpl(oss, fields...);
}

template <typename... Args>
std::string toStr(const Args&... fields) {
  std::ostringstream oss;
  toStrImpl(oss, fields...);
  return oss.str();
}

std::vector<std::string_view> split(std::string_view q) {
  std::vector<std::string_view> tokens;
  auto isParenthesis = [](char c) { return c == '(' || c == ')'; };
  auto skipSpaces = [&] {
    while (!q.empty() && isspace(q[0])) {
      q.remove_prefix(1);
    }
    return !q.empty();
  };
  auto getToken = [&] {
    DCHECK(!q.empty());
    size_t e = 0;
    if (isParenthesis(q[0])) {
      e = 1;
    } else {
      while (e < q.size() && !isspace(q[e]) && !isParenthesis(q[e])) {
        ++e;
      }
    }
    auto token = std::string_view(q.begin(), q.begin() + e);
    q.remove_prefix(e);
    return token;
  };
  while (skipSpaces()) {
    tokens.push_back(getToken());
  }
  return tokens;
}

std::string escapeStr(std::string_view sv) {
  std::string s = std::string(sv);
  for (auto& ch : s) {
    if (ch == '\n' || ch == '\t') {
      ch = ' ';
    }
  }
  return s;
}

void logAndThrow(size_t pos,
                 const std::string& msg,
                 const std::string_view qsv) {
  constexpr size_t kContextLength = 23;
  constexpr char kDoubleQuote = '"';
  QueryParser::Exception exp(toStr(
      "Invalid query: ",
      msg,
      " At position (0-based) ",
      pos,
      " with query text ",
      kDoubleQuote,
      escapeStr(qsv.substr(pos, kContextLength)),
      kDoubleQuote,
      " and preceding query text ",
      kDoubleQuote,
      escapeStr(qsv.substr(pos > kContextLength ? pos - kContextLength : 0,
                           std::min(pos, kContextLength))),
      kDoubleQuote,
      "."));
  LOG(ERROR) << "Failed to parse the query with exception: " << exp.what();
  throw exp;
}

enum class OpType {
  AND = 0,
  OR = 1,
  DIFF = 2,
  ROOT = 3,
};

} // namespace

QueryParser::IteratorFactory::IteratorFactory() = default;
QueryParser::IteratorFactory::~IteratorFactory() = default;

QueryParser::Exception::Exception(std::string whatStr)
    : whatStr_(std::move(whatStr)) {}

const char* QueryParser::Exception::what() const noexcept {
  return whatStr_.empty() ? "Bad search query." : whatStr_.c_str();
}

QueryParser::QueryParser(IteratorFactory& iteratorFactory)
    : iteratorFactory_(iteratorFactory) {}

namespace {

struct PartialState {
  size_t startPos = 0;
  OpType op = OpType::ROOT;
  Iterators itrs;
  std::string tag;

  IteratorPtr close(std::string_view endToken, const std::string_view& qsv) && {
    const auto endPos = std::distance(qsv.begin(), endToken.begin());
    if (itrs.empty()) {
      logAndThrow(endPos, "An operator doesn't have any sub-expression.", qsv);
    }
    switch (op) {
    case OpType::DIFF:
      if (itrs.size() != 2) {
        logAndThrow(endPos,
                    toStr("The diff operator requires exactly 2 "
                          "sub-expressions. Instead, ",
                          itrs.size(),
                          " ones are provided."),
                    qsv);
      }
      return makeIterator<DiffIterator>(
          std::move(tag), std::move(itrs[0]), std::move(itrs[1]));
      break;
    case OpType::AND:
      if (itrs.size() == 1 && tag.empty()) {
        return std::move(itrs.front());
      }
      return makeIterator<AndIterator>(std::move(tag), std::move(itrs));
      break;
    case OpType::OR:
      if (itrs.size() == 1 && tag.empty()) {
        return std::move(itrs.front());
      }
      return makeIterator<OrIterator>(std::move(tag), std::move(itrs));
      break;
    case OpType::ROOT:
      if (itrs.size() != 1) {
        logAndThrow(endPos, "There are multiple queries.", qsv);
      }
      if (!tag.empty()) {
        logAndThrow(endPos, "The top level has a tag.", qsv);
      }
      return std::move(itrs[0]);
      break;
    }
    return nullptr;
  }
};

} // namespace

IteratorPtr QueryParser::getIterator(const std::string& q) {
  const std::string_view qsv = q;
  auto getTokenStartingPos = [&qsv](const std::string_view& token) {
    return std::distance(qsv.begin(), token.begin());
  };

  std::stack<PartialState> stack;
  stack.push(PartialState{});

  auto tokens = split(q);
  auto expectNextOperator = [&](size_t nextTokenIdx) {
    DCHECK_GT(nextTokenIdx, 0U);
    PartialState state;
    // Calculate the previou token's starting position on the original query
    // string.
    state.startPos = getTokenStartingPos(tokens[nextTokenIdx - 1]);

    if (nextTokenIdx == tokens.size()) {
      logAndThrow(state.startPos,
                  "Expecting an operator after a left parenthesis '('.",
                  qsv);
    }
    static const std::unordered_map<std::string_view, OpType> kStrToOpMap = {
        {std::string_view("and"), OpType::AND},
        {std::string_view("or"), OpType::OR},
        {std::string_view("diff"), OpType::DIFF},
    };
    auto token = tokens[nextTokenIdx];
    auto mapItr = kStrToOpMap.find(token);
    if (mapItr == kStrToOpMap.end()) {
      logAndThrow(
          state.startPos,
          toStr("Unrecognizable operator after a left parenthesis '(': ",
                token),
          qsv);
    }
    state.op = mapItr->second;
    return state;
  };

  for (size_t tokenIdx = 0; tokenIdx < tokens.size(); ++tokenIdx) {
    auto& token = tokens[tokenIdx];
    const size_t currentPos = getTokenStartingPos(token);
    if (token == "(") {
      // '++' is to skip next token which is supposed to be an operator.
      stack.push(expectNextOperator(++tokenIdx));
    } else if (token == ")") {
      if (stack.empty() || stack.top().op == OpType::ROOT) {
        logAndThrow(currentPos, "Unmatched right parenthesis ')'.", qsv);
      }
      auto itr = std::move(stack.top()).close(token, qsv);
      stack.pop();
      // The stack can't be empty at this moment, because at least 'ROOT' is in
      // the stack.
      stack.top().itrs.push_back(std::move(itr));
    } else if (token.starts_with(kTagPrefix)) {
      auto& topOp = stack.top();
      if (!topOp.tag.empty()) {
        logAndThrow(currentPos,
                    "Multiple tags for one operator. Can only set one tag.",
                    qsv);
      }
      if (topOp.op == OpType::ROOT) {
        logAndThrow(currentPos, "The top level can't have a tag.", qsv);
      }
      token.remove_prefix(kTagPrefix.size());
      topOp.tag = std::string(token);
    } else {
      // should be a term.
      stack.top().itrs.push_back(
          iteratorFactory_.getIteratorForTerm(std::string(token)));
    }
  }
  if (stack.size() > 1) {
    logAndThrow(stack.top().startPos, "Unmatched left parenthesis '('.", qsv);
  }
  DCHECK(!stack.empty());
  return std::move(stack.top()).close(qsv.substr(qsv.size()), qsv);
}

} // namespace qute
