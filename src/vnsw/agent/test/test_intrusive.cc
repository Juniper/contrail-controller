/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "db/db.h"

class ref_counter {
public:
    friend void intrusive_ptr_add_ref(ref_counter* p)
    {
        BOOST_ASSERT(p);
        ++p->counter_;
        std::cout << "add_ref <" << p << " : " << p->counter_ << ">" << std::endl;
    }
    friend void intrusive_ptr_release(ref_counter* p)
    {
        BOOST_ASSERT(p);
        std::cout << "del_ref <" << p << " : " << p->counter_ << ">" << std::endl;
        if (--p->counter_ == 0)
            delete p;
    }
protected:
    ref_counter(): counter_(0) {}
    virtual ~ref_counter() {
        std::cout << "destructor <" << this << " : " << counter_ << ">" << std::endl;
    };
private:
    unsigned long counter_;
    DISALLOW_COPY_AND_ASSIGN(ref_counter);
};

class foo : public ref_counter {
public:
    foo() : ref_counter(), i(0) {};
    int i;
private:
    DISALLOW_COPY_AND_ASSIGN(foo);
};

void DestroyX(foo *ptr) {
    std::cout << __FUNCTION__ << std::endl;
}

int main() {
    boost::intrusive_ptr<foo> p1(new foo());
    boost::shared_ptr<foo> test(new foo(), DestroyX, CreateX());

    std::cout << "Exiting" << std::endl;
    return 0;
}
