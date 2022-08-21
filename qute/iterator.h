#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace qute {

using DocId = uint32_t;
constexpr DocId kInvalidDocId = std::numeric_limits<DocId>::max();

class Iterator;
using IteratorPtr = std::unique_ptr<Iterator>;

// Iterator is not thread safe.
class Iterator {
 public:
  Iterator();
  virtual ~Iterator();
  // Return true, iff the iterator is still valid after this call.
  virtual bool next() = 0;
  // Return the current value of the iterator.
  // Return kInvalidDocId if the iterator is not valid.
  virtual DocId value() const;
  // Skip to the closest value such that the value >= target
  // Return true, iff the iterator is still valid after this call.
  virtual bool skipTo(DocId target) = 0;
  // Return true, iff the current iterator is valid.
  virtual bool valid() const = 0;
  // How many estimated remaining docs in this iterator? May not be accurate.
  virtual size_t remainingDocs() const { return 0; }
  // Iterate on all the remaining values and call 'callback' on each value.
  void iterateWith(std::function<void(const DocId)> callback);

  bool operator<(const Iterator& rhs) const;
  bool operator>(const Iterator& rhs) const;

  virtual std::vector<std::string> getTags() const;
  // Whether itself has a tag or any descendant has a tag.
  virtual bool hasTag() const;

  static IteratorPtr getEmptyIterator();
  static IteratorPtr getVectorIterator(const std::vector<DocId>& sortedValues);
  static IteratorPtr getVectorIterator(std::vector<DocId>&& sortedValues);

  // Return the current value of the iterator.
  // May have undefined behavior if the iterator is not valid.
  virtual DocId valueUnsafe() const = 0;
};

class EmptyIterator : public Iterator {
 public:
  EmptyIterator() = default;
  ~EmptyIterator() = default;

  bool next() override { return false; }
  bool skipTo(DocId target) override { return false; }
  bool valid() const override { return false; }
  size_t remainingDocs() const override { return 0; }

 protected:
  DocId valueUnsafe() const override { return kInvalidDocId; }
};

// A simple iterator based on an input std::vector of DocId.
class VectorIterator : public Iterator {
 public:
  VectorIterator(const std::vector<DocId>& sortedDocIds);
  VectorIterator(std::vector<DocId>&& sortedDocIds);
  ~VectorIterator() = default;

  bool next() override;
  bool skipTo(DocId target) override;
  bool valid() const override;
  size_t remainingDocs() const override;

 protected:
  DocId valueUnsafe() const override;

 private:
  const std::vector<DocId> sortedDocIds__;
  const std::vector<DocId>& sortedDocIds_;
  size_t nextPos_ = 0;
};

// The following 3 iterators are compound iterators.
class AndIterator : public Iterator {
 public:
  explicit AndIterator(std::vector<IteratorPtr>&& children);

  bool next() override;

  bool valid() const override;

  bool skipTo(DocId target) override;
  size_t remainingDocs() const override;

  std::vector<std::string> getTags() const override;
  bool hasTag() const override;

 private:
  DocId valueUnsafe() const override;
  bool nextAgreement();

  std::vector<IteratorPtr> children_;
  const bool childrenHaveTags_ = false;
};

class OrIterator : public Iterator {
 public:
  explicit OrIterator(std::vector<IteratorPtr>&& children);

  bool next() override;
  bool valid() const override;

  bool skipTo(DocId target) override;
  size_t remainingDocs() const override;

  std::vector<std::string> getTags() const override;
  bool hasTag() const override;

 private:
  static bool minHeapCmp(const IteratorPtr& lhs, const IteratorPtr& rhs);
  DocId valueUnsafe() const override;
  void getTags(const size_t heapPos,
               const DocId currentValue,
               std::vector<std::string>& tags) const;

  std::vector<IteratorPtr> children_;
  const bool childrenHaveTags_ = false;
};

class DiffIterator : public Iterator {
 public:
  DiffIterator(IteratorPtr&& lhs, IteratorPtr&& rhs);

  bool next() override;
  bool valid() const override;

  bool skipTo(DocId target) override;
  size_t remainingDocs() const override;

  std::vector<std::string> getTags() const override;
  bool hasTag() const override;

 private:
  DocId valueUnsafe() const override;
  bool nextAgreement();

  IteratorPtr lhs_;
  IteratorPtr rhs_;
};

template <typename BaseItr,
          std::enable_if_t<std::is_convertible_v<BaseItr*, Iterator*>, int> = 0>
class IteratorWithTag : public BaseItr {
 public:
  template <typename... Args>
  IteratorWithTag(std::string tag, Args&&... args)
      : BaseItr(std::forward<Args>(args)...), tag_(tag) {}

  std::vector<std::string> getTags() const override {
    if (BaseItr::hasTag()) {
      auto tags = BaseItr::getTags();
      tags.push_back(tag_);
      return tags;
    }
    return {tag_};
  }

  bool hasTag() const override { return true; }

 private:
  const std::string tag_;
};

namespace {

template <typename Itr>
void toVectorImp(std::vector<IteratorPtr>& output, Itr&& itr) {
  output.push_back(std::move(itr));
}

template <typename Itr1, typename... Itrs>
void toVectorImp(std::vector<IteratorPtr>& output,
                 Itr1&& itr1,
                 Itrs&&... itrs) {
  output.push_back(std::move(itr1));
  toVectorImp(output, std::move(itrs)...);
}

} // namespace

template <typename... Itrs>
std::vector<IteratorPtr> toVector(Itrs&&... itrs) {
  std::vector<IteratorPtr> res;
  toVectorImp(res, std::move(itrs)...);
  return res;
}

template <class Itr, typename... Args>
std::enable_if_t<std::is_convertible_v<Itr*, Iterator*>, IteratorPtr>
makeIterator(std::string tag, Args&&... args) {
  if (tag.empty()) {
    return std::make_unique<Itr>(std::forward<Args>(args)...);
  }
  return std::make_unique<IteratorWithTag<Itr>>(std::move(tag),
                                                std::forward<Args>(args)...);
}

template <class Itr, typename... Args>
std::enable_if_t<std::is_convertible_v<Itr*, Iterator*>, IteratorPtr>
makeIterator(Args&&... args) {
  return std::make_unique<Itr>(std::forward<Args>(args)...);
}

} // namespace qute
