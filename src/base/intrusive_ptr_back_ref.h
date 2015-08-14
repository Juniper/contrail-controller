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

// referrer, pair of base pointer and member pointer
typedef std::pair<void*, void*> IntrusiveReferrer;

template <class D>
class IntrusivePtrRef : public boost::intrusive_ptr<D> {
public:
    IntrusivePtrRef() : boost::intrusive_ptr<D>(), referrer_(NULL) {
    }

    IntrusivePtrRef(D *data) : boost::intrusive_ptr<D>(data), referrer_(NULL) {
        if (data) {
            intrusive_ptr_add_back_ref(IntrusiveReferrer(referrer_, this), data);
        }
    }

    IntrusivePtrRef(D *data, void *referrer) : boost::intrusive_ptr<D>(data),
                                              referrer_(referrer) {
        if (data) {
            intrusive_ptr_add_back_ref(IntrusiveReferrer(referrer, this), data);
        }
    }

    IntrusivePtrRef(IntrusivePtrRef const &rhs, void *referrer)
        : boost::intrusive_ptr<D>(rhs), referrer_(referrer) {
        if (this->get()) {
            intrusive_ptr_add_back_ref(IntrusiveReferrer(referrer, this),
                                       this->get());
        }
    }

    virtual ~IntrusivePtrRef() {
        if (this->get()) {
            intrusive_ptr_del_back_ref(IntrusiveReferrer(referrer_, this), this->get());
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
        // Trigger Base class swap to swap values
        boost::intrusive_ptr<D>::swap(rhs);

        // swap the referrer for this and rhs
        D *tmp = this->get();
        if (tmp) {
            // change referrer for new value of IntrusivePtrRef
            intrusive_ptr_add_back_ref(IntrusiveReferrer(referrer_, this), tmp);
            intrusive_ptr_del_back_ref(IntrusiveReferrer(rhs.referrer_, &rhs), tmp);
        }
        tmp = rhs.get();
        if (tmp) {
            // change referrer for new value of rhs
            intrusive_ptr_add_back_ref(IntrusiveReferrer(rhs.referrer_, &rhs), tmp);
            intrusive_ptr_del_back_ref(IntrusiveReferrer(referrer_, this), tmp);
        }
    }

private:
    void *referrer_;
};

#endif  // SRC_BASE_INTRUSIVE_PTR_BACK_REF_H_
