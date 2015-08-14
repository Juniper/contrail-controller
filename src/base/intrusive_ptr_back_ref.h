/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BASE_INTRUSIVE_PTR_BACK_REF_H_
#define SRC_BASE_INTRUSIVE_PTR_BACK_REF_H_

#include <boost/intrusive_ptr.hpp>

// IntrusivePtrRef Class is an extension to boost::intrusive_ptr
// to provide additional functionality of making back reference
// pointer available with callbacks intrusive_ptr_add_back_ref
// and intrusive_ptr_del_back_ref, using which application using
// IntrusivePtrRef can keep track of pointers who holds references
// to the object

// referer, pair of base pointer and member pointer
typedef std::pair<void*, void*> IntrusiveReferer;

template <class D>
class IntrusivePtrRef : public boost::intrusive_ptr<D> {
public:
    IntrusivePtrRef() : boost::intrusive_ptr<D>(), referer_(NULL) {
    }

    IntrusivePtrRef(D *data) : boost::intrusive_ptr<D>(data), referer_(NULL) {
        if (data) {
            intrusive_ptr_add_back_ref(IntrusiveReferer(referer_, this), data);
        }
    }

    IntrusivePtrRef(D *data, void *referer) : boost::intrusive_ptr<D>(data),
                                              referer_(referer) {
        if (data) {
            intrusive_ptr_add_back_ref(IntrusiveReferer(referer, this), data);
        }
    }

    IntrusivePtrRef(IntrusivePtrRef const &rhs, void *referer)
        : boost::intrusive_ptr<D>(rhs), referer_(referer) {
        if (this->get()) {
            intrusive_ptr_add_back_ref(IntrusiveReferer(referer, this),
                                       this->get());
        }
    }

    virtual ~IntrusivePtrRef() {
        if (this->get()) {
            intrusive_ptr_del_back_ref(IntrusiveReferer(referer_, this), this->get());
        }
    }

    IntrusivePtrRef & operator=(IntrusivePtrRef const &rhs) {
        IntrusivePtrRef(rhs, this).swap(*this);
        return *this;
    }

    IntrusivePtrRef & operator=(D *rhs) {
        IntrusivePtrRef(rhs, this).swap(*this);
        return *this;
    }

    void reset() {
        IntrusivePtrRef(NULL, this).swap(*this);
    }

    void reset(D *rhs) {
        IntrusivePtrRef(rhs, this).swap(*this);
    }

    void swap(IntrusivePtrRef &rhs) {
        boost::intrusive_ptr<D>::swap(rhs);
        D *tmp = this->get();
        if (tmp) {
            // change referer for new value of IntrusivePtrRef
            intrusive_ptr_add_back_ref(IntrusiveReferer(referer_, this), tmp);
            intrusive_ptr_del_back_ref(IntrusiveReferer(rhs.referer_, &rhs), tmp);
        }
        tmp = rhs.get();
        if (tmp) {
            // change referer for new value of rhs
            intrusive_ptr_add_back_ref(IntrusiveReferer(rhs.referer_, &rhs), tmp);
            intrusive_ptr_del_back_ref(IntrusiveReferer(referer_, this), tmp);
        }
    }

private:
    void *referer_;
};

#endif  // SRC_BASE_INTRUSIVE_PTR_BACK_REF_H_
