/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/dependency.h"

#include "base/logging.h"
#include "testing/gunit.h"

using namespace std;
class Bar;

class Foo {
  public:
    Foo(const string &name) : name_(name), bar_(this) {
    }
    const string &name() const { return name_; }
    void set_bar(Bar *bar) {
        bar_.reset(bar);
    }
    Bar *bar() { return bar_.get(); }

  private:
    string name_;
    DependencyRef<Foo, Bar> bar_;
};

class Bar {
  public:
    typedef DependencyList<Foo, Bar>::const_iterator foo_iterator;
    Bar(const string &name) : name_(name) { }
    const string &name() const { return name_; }

    foo_iterator foo_begin() const { return ref_list_.begin(); }
    foo_iterator foo_end() const { return ref_list_.end(); }

  private:
    string name_;
    DEPENDENCY_LIST(Foo, Bar, ref_list_);
};

class DependencyTest : public ::testing::Test {
protected:
    int FooCount(const Bar &x) {
        int count = 0;
        for (Bar::foo_iterator iter = x.foo_begin(); iter != x.foo_end();
             ++iter) {
            ++count;
        }
        return count;
    }
};

TEST_F(DependencyTest, Basic) {
    Foo a("a"), b("b");
    Bar x("x");

    a.set_bar(&x);
    EXPECT_EQ("x", a.bar()->name());
    EXPECT_EQ("a", x.foo_begin()->name());
    b.set_bar(&x);
    EXPECT_EQ(2, FooCount(x));
    {
        Foo c("c");
        c.set_bar(&x);
        EXPECT_EQ(3, FooCount(x));
    }
    EXPECT_EQ(2, FooCount(x));
    EXPECT_EQ("a", x.foo_begin()->name());
    EXPECT_EQ("b", (++x.foo_begin())->name());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
