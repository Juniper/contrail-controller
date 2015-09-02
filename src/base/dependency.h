/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BASE_DEPENDENCY_H__
#define __BASE_DEPENDENCY_H__

#include <boost/intrusive/list.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include "base/util.h"

template <typename NodeType, typename ObjectType>
class DependencyList;

template <typename NodeType, typename ObjectType>
class DependencyRef {
public:
    explicit DependencyRef(NodeType *self) : self_(self), ptr_(NULL) { }
    DependencyRef(NodeType *self, ObjectType *ptr)
            :  self_(self), ptr_(ptr) {
        if (ptr_ != NULL) {
            ptr_->DependencyAdd(this);
        }
    }

    ~DependencyRef() {
        if (ptr_ != NULL)  {
            ptr_->DependencyRemove(this);
        }
    }

    void reset(ObjectType *ptr) {
        if (ptr_ != NULL)  {
            ptr_->DependencyRemove(this);
        }
        ptr_ = ptr;
        if (ptr_ != NULL) {
            ptr_->DependencyAdd(this);
        }
    }

    void clear() {
        if (ptr_ != NULL)  {
            ptr_->DependencyRemove(this);
        }
        ptr_ = NULL;
    }

    ObjectType *get() const {
        return ptr_;
    }

    ObjectType *operator->() const {
        return ptr_;
    }

private:
    friend class DependencyList<NodeType, ObjectType>;
    boost::intrusive::list_member_hook<> node_;
    NodeType *self_;
    ObjectType *ptr_;
    DISALLOW_COPY_AND_ASSIGN(DependencyRef);
};

template <typename NodeType, typename ObjectType>
class DependencyList {
public:
    typedef boost::intrusive::member_hook<
            DependencyRef<NodeType, ObjectType>,
            boost::intrusive::list_member_hook<>,
            &DependencyRef<NodeType, ObjectType>::node_> MemberHook;
    typedef boost::intrusive::list<
            DependencyRef<NodeType, ObjectType>, MemberHook> List;

    template <typename ValueType, typename IteratorType>
    class IteratorBase : public boost::iterator_facade<
            IteratorBase<ValueType, IteratorType>, ValueType,
            boost::forward_traversal_tag> {
    public:
        explicit IteratorBase(const IteratorType &iter) : iter_(iter) { }
    private:
        void increment() { ++iter_; }
        bool equal(const IteratorBase &rhs) const {
            return iter_ == rhs.iter_;
        }
        ValueType &dereference() const {
            return *(iter_->self_);
        }
        friend class boost::iterator_core_access;
        IteratorType iter_;
    };
    typedef IteratorBase<NodeType, typename List::iterator> iterator;
    typedef IteratorBase<const NodeType,
            typename List::const_iterator> const_iterator;
    DependencyList() { }
    ~DependencyList() { clear(); }

    void Add(DependencyRef<NodeType, ObjectType> *node) {
        list_.push_back(*node);
    }

    void Remove(DependencyRef<NodeType, ObjectType> *node) {
        list_.erase(list_.iterator_to(*node));
    }

    void clear() {
        while (!list_.empty()) {
            DependencyRef<NodeType, ObjectType> *node = &list_.front();
            node->clear();
        }
    }

    const List &list() const { return list_; }

    iterator begin() { return iterator(list_.begin()); }
    iterator end() { return iterator(list_.end()); }
    const_iterator begin() const {
        return const_iterator(list_.begin());
    }
    const_iterator end() const {
        return const_iterator(list_.end());
    }

    bool empty() const { return list_.empty(); }

private:
    List list_;
    DISALLOW_COPY_AND_ASSIGN(DependencyList);
};

#define DEPENDENCY_LIST(NodeType, ObjectType, _Member)                  \
    friend class DependencyRef<NodeType, ObjectType>;                   \
    void DependencyAdd(DependencyRef<NodeType, ObjectType> *node) {     \
        _Member.Add(node);                                              \
    }                                                                   \
    void DependencyRemove(DependencyRef<NodeType, ObjectType> *node) {  \
        _Member.Remove(node);                                           \
    }                                                                   \
    DependencyList<NodeType, ObjectType> _Member

#endif
