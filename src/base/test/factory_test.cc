/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/factory.h"

#include <boost/functional/factory.hpp>
#include <boost/scoped_ptr.hpp>

#include "testing/gunit.h"

class TypeA {
  public:
    TypeA() : value_(0) { }

  private:
    int value_;
};

class TypeB {
  public:
    TypeB() : value_(1) { }

    int value() const { return value_; }
  private:
    int value_;
};

class TypeX {
  public:
    TypeX(int value) : value_(value) { }
    virtual ~TypeX() { }
    const int value() const { return value_; }
  private:
    int value_;
};

class TypeY {
  public:
    TypeY(int x, int y) : x_(x), y_(x) { }

  private:
    int x_;
    int y_;
};

class TypeZ {
  public:
    TypeZ(int value) : value_(value + 1) { }
    const int value() const { return value_; }

  private:
    int value_;
};

class TypeW {
  public:
    TypeW(const TypeX *ptr) : ptr_(ptr) {
    }
  private:
    const TypeX *ptr_;
};

class TestModule : public Factory<TestModule> {
    FACTORY_TYPE_N0(TestModule, TypeA);
    FACTORY_TYPE_N0(TestModule, TypeB);
    FACTORY_TYPE_N1(TestModule, TypeX, int);
    FACTORY_TYPE_N2(TestModule, TypeY, int, int);
    FACTORY_TYPE_N1(TestModule, TypeW, const TypeX *);
    FACTORY_TYPE_N1(TestModule, TypeZ, int);
};

template <>
TestModule *Factory<TestModule>::singleton_ = NULL;

FACTORY_STATIC_REGISTER(TestModule, TypeA, TypeA);
FACTORY_STATIC_REGISTER(TestModule, TypeB, TypeB);
FACTORY_STATIC_REGISTER(TestModule, TypeX, TypeX);
FACTORY_STATIC_REGISTER(TestModule, TypeY, TypeY);
FACTORY_STATIC_REGISTER(TestModule, TypeW, TypeW);
FACTORY_STATIC_REGISTER(TestModule, TypeZ, TypeZ);

class FactoryTest : public ::testing::Test {
  protected:
};

TEST_F(FactoryTest, Basic) {
    boost::scoped_ptr<TypeB> b(TestModule::Create<TypeB>());
    EXPECT_EQ(1, b->value());
    boost::scoped_ptr<TypeX> x(TestModule::Create<TypeX>(1));
    EXPECT_EQ(1, x->value());
    boost::scoped_ptr<TypeY> y(TestModule::Create<TypeY>(2, 3));
    boost::scoped_ptr<TypeW> w(TestModule::Create<TypeW>(x.get()));
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
