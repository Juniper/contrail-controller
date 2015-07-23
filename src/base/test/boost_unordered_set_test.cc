/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/unordered_set.hpp>
#include "testing/gunit.h"
#include "base/logging.h"

using namespace std;

class TestObject {
public:
    TestObject(int ii) : i(ii) { }
private:
    int i;
};

class BoostUnorderedSetTest : public ::testing::Test {
public:
    typedef boost::unordered_set<TestObject *> BoostUS;
protected:
    BoostUS entries;
};

// Add a million pointers to the unordered set and check the trend in
// bucket-count and load_factor.
TEST_F(BoostUnorderedSetTest, PointerTest) {
    cout << "Default values for unordered_set are:" << endl;
    cout << "bucket_count is " << entries.bucket_count() << endl;
    cout << "load_factor is " << entries.load_factor() << endl;
    cout << "max_load_factor is " << entries.max_load_factor() << endl;
    cout << "*****************************" << endl;

    for (int i = 1; i < 1000000; ++i) {
        TestObject *ptr = new TestObject(i);
        entries.insert(ptr);
        if ((i % 1000) == 0) {
            cout << "After " << i << " elements:   ";
            cout << "bc " << entries.bucket_count();
            cout << ", lf " << entries.load_factor();
            cout << ", mlf " << entries.max_load_factor() << endl;
        }
    }
    BoostUS::iterator iter = entries.begin();
    while (iter != entries.end()) {
        TestObject *ptr = *iter;
        iter = entries.erase(iter);
        delete ptr;
    }
    ASSERT_TRUE(entries.size() == 0);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
