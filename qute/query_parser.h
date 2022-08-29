// Copyright 2022 HC Zhu hczhu.github@gmail.com

// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <exception>
#include <memory>
#include <string>

#include "iterator.h"

namespace qute {

class QueryParser {
 public:
  class IteratorFactory {
   public:
    IteratorFactory();
    virtual ~IteratorFactory();
    virtual IteratorPtr getIteratorForTerm(const std::string& term) = 0;
  };

  class Exception : public std::exception {
   public:
    explicit Exception(std::string whatStr);
    const char* what() const noexcept override;

   private:
    std::string whatStr_;
  };

  explicit QueryParser(IteratorFactory& iteratorFactory);

  // Grammar
  // q :-> term with prefix
  // q :-> expr
  // expr :-> (and [tag:t] expr1 [expr2] ...)
  // expr :-> (or [tag:t] expr1 [expr2] ...)
  // expr :-> (diff [tag:t] expr1 expr2)
  IteratorPtr getIterator(const std::string& q);

 private:
  IteratorFactory& iteratorFactory_;
};

} // namespace qute
