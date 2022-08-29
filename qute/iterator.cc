
// Copyright 2022 HC Zhu hczhu.github@gmail.com

// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "iterator.h"

#include <algorithm>

#include <glog/logging.h>

namespace qute {

namespace {

template <typename T>
void adjustDownMinHeap(std::vector<T>& heap,
                       size_t pos,
                       bool (&minHeapCmp)(const T& lhs, const T& rhs)) {
  DCHECK_LT(pos, heap.size());
  for (;;) {
    auto minChild = pos * 2 + 1;
    if (minChild >= heap.size()) {
      break;
    }
    if (minChild + 1 < heap.size() &&
        minHeapCmp(heap[minChild], heap[minChild + 1])) {
      ++minChild;
    }
    if (minHeapCmp(heap[pos], heap[minChild])) {
      std::swap(heap[minChild], heap[pos]);
      pos = minChild;
    } else {
      break;
    }
  }
}

} // namespace

Iterator::Iterator() = default;
Iterator::~Iterator() = default;

void Iterator::iterateWith(std::function<void(const DocId)> callback) {
  VLOG(2) << "------------";
  while (valid()) {
    VLOG(2) << "  Iteration on local id: " << value();
    callback(value());
    next();
  }
  VLOG(2) << "------------";
}

DocId Iterator::value() const {
  return valid() ? valueUnsafe() : kInvalidDocId;
}

bool Iterator::operator<(const Iterator& rhs) const {
  return value() < rhs.value();
}

bool Iterator::operator>(const Iterator& rhs) const {
  return value() > rhs.value();
}

std::vector<std::string> Iterator::getTags() const { return {}; }
bool Iterator::hasTag() const { return false; }

IteratorPtr Iterator::getEmptyIterator() {
  return makeIterator<EmptyIterator>();
}
IteratorPtr Iterator::getVectorIterator(
    const std::vector<DocId>& sortedDocIds) {
  return makeIterator<VectorIterator>(sortedDocIds);
}
IteratorPtr Iterator::getVectorIterator(std::vector<DocId>&& sortedDocIds) {
  return makeIterator<VectorIterator>(std::move(sortedDocIds));
}

VectorIterator::VectorIterator(const std::vector<DocId>& sortedDocIds)
    : sortedDocIds_(sortedDocIds) {}
VectorIterator::VectorIterator(std::vector<DocId>&& sortedDocIds)
    : sortedDocIds__(std::move(sortedDocIds)), sortedDocIds_(sortedDocIds__) {}

bool VectorIterator::next() {
  ++nextPos_;
  return valid();
}

bool VectorIterator::skipTo(DocId target) {
  DCHECK_LE(nextPos_, sortedDocIds_.size());
  nextPos_ = std::lower_bound(sortedDocIds_.begin() + nextPos_,
                              sortedDocIds_.end(),
                              target) -
             sortedDocIds_.begin();
  return valid();
}

bool VectorIterator::valid() const { return nextPos_ < sortedDocIds_.size(); }

size_t VectorIterator::remainingDocs() const {
  return sortedDocIds_.size() - nextPos_;
}

DocId VectorIterator::valueUnsafe() const { return sortedDocIds_[nextPos_]; }

AndIterator::AndIterator(std::vector<IteratorPtr>&& children)
    : children_(std::move(children)),
      childrenHaveTags_(
          std::any_of(children_.begin(), children_.end(), [](const auto& itr) {
            return itr->hasTag();
          })) {
  CHECK(!children_.empty());
  const auto maxItr = std::max_element(
      children_.begin(), children_.end(), [](const auto& lhs, const auto& rhs) {
        return *lhs < *rhs;
      });
  std::swap(children_[0], *maxItr);
  nextAgreement();
}

bool AndIterator::next() {
  if (!valid()) {
    return false;
  }
  children_.front()->next();
  return children_.front()->valid() && nextAgreement();
}

bool AndIterator::valid() const { return children_[0]->valid(); }

bool AndIterator::skipTo(DocId target) {
  if (!valid() || !children_[0]->skipTo(target)) {
    return false;
  }
  return nextAgreement();
}

size_t AndIterator::remainingDocs() const {
  if (!valid()) {
    return 0;
  }
  size_t ret = children_[0]->remainingDocs();
  for (const auto& child : children_) {
    ret = std::min(ret, child->remainingDocs());
  }
  return ret;
}

DocId AndIterator::valueUnsafe() const { return children_[0]->value(); }

bool AndIterator::nextAgreement() {
  // Pre-condition: the maximum local id is at children_[0].
  size_t pos = 1;
  while (pos < children_.size() && children_[0]->valid()) {
    const auto candidate = children_[0]->value();
    for (; pos < children_.size(); ++pos) {
      DCHECK_LE(children_[pos]->value(), candidate);
      if (children_[pos]->value() < candidate) {
        children_[pos]->skipTo(candidate);
        DCHECK_GE(children_[pos]->value(), candidate);
        if (children_[pos]->value() > candidate) {
          std::swap(children_[pos], children_[0]);
          pos = 1;
          break;
        }
      }
    }
  }
  return pos == children_.size();
}

std::vector<std::string> AndIterator::getTags() const {
  DCHECK(valid());
  if (!childrenHaveTags_) {
    return {};
  }
  std::vector<std::string> tags;
  for (const auto& itr : children_) {
    auto childTags = itr->getTags();
    tags.insert(tags.end(),
                std::make_move_iterator(childTags.begin()),
                std::make_move_iterator(childTags.end()));
  }
  return tags;
}

bool AndIterator::hasTag() const { return childrenHaveTags_; }

OrIterator::OrIterator(std::vector<IteratorPtr>&& children)
    : children_(std::move(children)),
      childrenHaveTags_(
          std::any_of(children_.begin(), children_.end(), [](const auto& itr) {
            return itr->hasTag();
          })) {
  CHECK(!children_.empty()) << "An OrIterator must have children!";
  std::make_heap(children_.begin(), children_.end(), minHeapCmp);
}

bool OrIterator::next() {
  if (!valid()) {
    return false;
  }
  const auto current = value();
  while (!children_.empty() && children_[0]->value() == current) {
    children_[0]->next();
    if (children_[0]->valid()) {
      adjustDownMinHeap(children_, 0, minHeapCmp);
    } else {
      std::pop_heap(children_.begin(), children_.end(), minHeapCmp);
      children_.pop_back();
    }
  }
  DCHECK(children_.empty() || children_[0]->value() > current);
  return valid();
}

bool OrIterator::valid() const {
  return !children_.empty() && children_[0]->valid();
}

bool OrIterator::skipTo(DocId target) {
  for (size_t i = 0; i < children_.size(); ++i) {
    children_[i]->skipTo(target);
  }
  size_t next = 0;
  for (size_t i = 0; i < children_.size(); ++i) {
    if (children_[i]->valid()) {
      if (i != next) {
        std::swap(children_[i], children_[next]);
      }
      ++next;
    }
  }
  children_.resize(next);
  std::make_heap(children_.begin(), children_.end(), minHeapCmp);
  return valid();
}
size_t OrIterator::remainingDocs() const {
  if (!valid()) {
    return 0;
  }
  size_t ret = children_[0]->remainingDocs();
  for (const auto& child : children_) {
    ret = std::max(ret, child->remainingDocs());
  }
  return ret;
}
DocId OrIterator::valueUnsafe() const { return children_[0]->value(); }

bool OrIterator::minHeapCmp(const IteratorPtr& lhs, const IteratorPtr& rhs) {
  // A min heap with the minimum elelement on the top of the heap.
  return *rhs < *lhs;
}

void OrIterator::getTags(const size_t heapPos,
                         const DocId currentValue,
                         std::vector<std::string>& tags) const {
  if (heapPos >= children_.size() ||
      currentValue != children_[heapPos]->value()) {
    return;
  }
  auto childTags = children_[heapPos]->getTags();
  tags.insert(tags.end(),
              std::make_move_iterator(childTags.begin()),
              std::make_move_iterator(childTags.end()));
  getTags((heapPos << 1) + 1, currentValue, tags);
  getTags((heapPos << 1) + 2, currentValue, tags);
}

std::vector<std::string> OrIterator::getTags() const {
  DCHECK(valid());
  if (!childrenHaveTags_) {
    return {};
  }
  std::vector<std::string> tags;
  getTags(0, valueUnsafe(), tags);
  return tags;
}

bool OrIterator::hasTag() const { return childrenHaveTags_; }

DiffIterator::DiffIterator(IteratorPtr&& lhs, IteratorPtr&& rhs)
    : lhs_(std::move(lhs)), rhs_(std::move(rhs)) {
  nextAgreement();
}

bool DiffIterator::nextAgreement() {
  while (lhs_->valid()) {
    if (!rhs_->skipTo(lhs_->value()) || rhs_->value() > lhs_->value()) {
      return true;
    }
    lhs_->next();
  }
  return false;
}

bool DiffIterator::next() {
  if (!valid() || !lhs_->next()) {
    return false;
  }
  return nextAgreement();
}

bool DiffIterator::valid() const { return lhs_->valid(); }

bool DiffIterator::skipTo(DocId target) {
  if (!lhs_->skipTo(target)) {
    return false;
  }
  return nextAgreement();
}

size_t DiffIterator::remainingDocs() const {
  return lhs_->remainingDocs() > rhs_->remainingDocs()
             ? lhs_->remainingDocs() - rhs_->remainingDocs()
             : 0;
}
DocId DiffIterator::valueUnsafe() const { return lhs_->value(); }

std::vector<std::string> DiffIterator::getTags() const {
  CHECK(valid());
  return lhs_->getTags();
}

bool DiffIterator::hasTag() const { return lhs_->hasTag(); }

} // namespace qute
