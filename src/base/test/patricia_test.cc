/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdlib.h>
#include <arpa/inet.h>
#include "base/patricia.h"
#include "base/logging.h"
#include "testing/gunit.h"

using namespace Patricia;

struct Rt {
    int ip;
    int len;
    int nh;
};

Rt rt [] = {{0x00000000,  0, 1},
            {0x01000000,  8, 2},
            {0x01010000, 24, 3},
            {0x01010001, 32, 4},
            {0x01010002, 32, 5},
            {0x01010003, 32, 6},
            {0x01010004, 32, 7},
            {0x01010005, 32, 8},
            {0x01010006, 32, 9},
            {0x01010007, 32, 10},
            {0x01010008, 32, 11},
            {0x01010009, 32, 12},
            {0x0101000a, 32, 13},
            {0x01010100, 24, 14},
            {0x01010101, 32, 15},
            {0x01010102, 32, 16},
            {0x01010103, 32, 17},
            {0x01010104, 32, 18},
            {0x01010105, 32, 19},
            {0x01010106, 32, 20},
            {0x01010107, 32, 21},
            {0x01010108, 32, 22},
            {0x01010109, 32, 23},
            {0x0101010a, 32, 24},
            {0x0a010000, 24, 25},
            {0x0a010001, 32, 26},
            {0x0a010002, 32, 27},
            {0x0a010003, 32, 28},
            {0x0a010004, 32, 29},
            {0x0a010005, 32, 30},
            {0x0a010006, 32, 31},
            {0x0a010007, 32, 32},
            {0x0a010008, 32, 33},
            {0x0a010009, 32, 34},
            {0x0a01000a, 32, 35},
            {0x0b010000, 24, 36},
            {0x0b010001, 32, 37},
            {0x0b010002, 32, 38},
            {0x0b010003, 32, 39},
            {0x0b010004, 32, 40},
            {0x0b010005, 32, 41},
            {0x0b010006, 32, 42},
            {0x0b010007, 32, 43},
            {0x0b010008, 32, 44},
            {0x0b010009, 32, 45},
            {0x0b01000a, 32, 46},
            {0x0b010100, 24, 47},
            {0x0b010101, 32, 48},
            {0x0b010102, 32, 49},
            {0x0b010103, 32, 50},
            {0x0b010104, 32, 51},
            {0x0b010105, 32, 52},
            {0x0b010106, 32, 53},
            {0x0b010107, 32, 54},
            {0x0b010108, 31, 55},
            {0x0b010108, 32, 56},
            {0x0b010109, 32, 57},
            {0x0b01010a, 32, 58}
            };

std::size_t rt_size = sizeof(rt)/sizeof(rt[0]);

class Route {
public:
    Route(int ip = 0, int len = 0, int nexthop = 0) : ip_(ip), len_(len), nexthop_(nexthop) {
    }

    class RtKey {
    public:
        static std::size_t Length(Route *route_key) {
            return route_key->len_;
        }

        static char ByteValue(Route *route_key, std::size_t i) {
            char *ch = (char *)&route_key->ip_;
            return ch[sizeof(route_key->ip_) - i - 1];
        }
    };

    int ip_;
    int len_;
    int nexthop_;
    Node rtnode_;
};

typedef Patricia::Tree<Route, &Route::rtnode_, Route::RtKey> RouteTable;

class PatriciaBaseTest : public ::testing::Test {
    public:
    PatriciaBaseTest() {
        itbl_ = new RouteTable;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    RouteTable      *itbl_;
};

TEST_F(PatriciaBaseTest, Core) {
    std::size_t i;
    Route *route;
    Route *route_next;
    bool    ret;

    for (i = 0; i < rt_size; i++) {
        route = new Route(rt[i].ip, rt[i].len, rt[i].nh);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, false);
    }

    i = 0;
    route = NULL;
    while ((route = itbl_->GetNext(route))) {
        EXPECT_EQ(route->nexthop_, i+1);
        i++;
    }
    EXPECT_EQ(i, rt_size);
    EXPECT_EQ(rt_size, itbl_->Size());

    route = itbl_->GetNext(NULL);;
    while (route) {
        route_next = itbl_->GetNext(route);;
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);
        delete route;
        route = route_next;
    }

    route = itbl_->GetNext(NULL);
    EXPECT_EQ(route, (Route *)NULL);
    EXPECT_EQ(0, itbl_->Size());
}

TEST_F(PatriciaBaseTest, iterator) {
    std::size_t i;
    Route *route;
    bool    ret;
    RouteTable::Iterator it;

    for (i = 0; i < rt_size; i++) {
        route = new Route(rt[i].ip, rt[i].len, rt[i].nh);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, false);
    }

    i = 0;
    for (it = itbl_->begin(); it != itbl_->end(); it++) {
        EXPECT_EQ((*it)->nexthop_, i+1);
        i++;
    }
    EXPECT_EQ(i, rt_size);

    for (it = itbl_->begin(); it != itbl_->end();) {
        route = *it;
        it++;
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);
        delete (route);
    }

    EXPECT_TRUE(itbl_->begin() == itbl_->end());
}

TEST_F(PatriciaBaseTest, RandomInsert) {
    std::size_t i;
    std::size_t rand_num;
    Route *route;
    Route *route_next;
    bool    ret;
    Rt      rt_int[rt_size];

    for (i = 0; i < rt_size; i++) {
        rt_int[i] = rt[i];
    }
    for (i = 0; i < rt_size; i++) {
        rand_num = rand() % (rt_size - i);
        route = new Route(rt_int[rand_num].ip, rt_int[rand_num].len,
                          rt_int[rand_num].nh);
        for (; rand_num < rt_size - i - 1; rand_num++) {
            rt_int[rand_num] = rt_int[rand_num + 1];
        }
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, false);
    }

    i = 0;
    route = NULL;
    while ((route = itbl_->GetNext(route))) {
        EXPECT_EQ(route->nexthop_, i+1);
        i++;
    }
    EXPECT_EQ(i, rt_size);

    route = itbl_->GetNext(NULL);;
    while (route) {
        route_next = itbl_->GetNext(route);;
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);
        delete route;
        route = route_next;
    }

    route = itbl_->GetNext(route);
    EXPECT_EQ(route, (Route *)NULL);
}

class PatriciaTest : public ::testing::Test {
    public:
    PatriciaTest() {
        itbl_ = new RouteTable;
    }

    virtual void SetUp() {
        std::size_t i;
        Route *route;
        bool    ret;

        for (i = 0; i < rt_size; i++) {
            route = new Route(rt[i].ip, rt[i].len, rt[i].nh);
            ret = itbl_->Insert(route);
            EXPECT_EQ(ret, true);
        }

    }

    virtual void TearDown() {
        Route *route;

        route = NULL;
        while ((route = itbl_->GetNext(route))) {
            itbl_->Remove(route);
            delete route;
            route = NULL;
        }

        route = itbl_->GetNext(NULL);
        EXPECT_EQ(route, (Route *)NULL);

    }

    RouteTable      *itbl_;
};

TEST_F(PatriciaTest, Find) {
    std::size_t i;
    Route *route;
    Route route_key;
    Rt rt_key = {0x01010000, 16, 0};

    for (i = 0; i < rt_size; i++) {
        route_key.ip_ = rt[i].ip;
        route_key.len_ = rt[i].len;
        route = itbl_->Find(&route_key);
        EXPECT_NE(route, (Route *)NULL);
    }

    route_key.ip_ = rt_key.ip;
    route_key.len_ = rt_key.len;
    route = itbl_->Find(&route_key);
    EXPECT_EQ(route, (Route *)NULL);
}

TEST_F(PatriciaTest, FindNext) {
    std::size_t i;
    Route *route;
    Route route_key;

    route_key.ip_ = rt[0].ip;
    route_key.len_ = rt[0].len;
    route = itbl_->Find(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    for (i = 1; i < rt_size; i++) {
        if (route) {
            route = itbl_->FindNext(route);
            EXPECT_EQ(route->nexthop_, i+1);
        }
        EXPECT_NE(route, (Route *)NULL);
    }
}

TEST_F(PatriciaTest, FindNext1) {
    Route *route;
    Route route_key;
    Rt rt_key = {0x01010000, 16, 0};

    route_key.ip_ = rt_key.ip;
    route_key.len_ = rt_key.len;
    route = itbl_->FindNext(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    if (route) {
        EXPECT_EQ(route->nexthop_, 3);
    }
}

TEST_F(PatriciaTest, FindNextAll) {
    std::size_t i;
    Route *route;
    Route *route_next;
    Route route_key;
    bool ret;

    route_key.ip_ = rt[0].ip;
    route_key.len_ = rt[0].len;
    route = itbl_->Find(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    for (i = 1; i < rt_size; i++) {
        if (route) {
            ret = itbl_->Remove(route);
            EXPECT_TRUE(ret);
            route_next = itbl_->FindNext(route);
            ret = itbl_->Insert(route);
            EXPECT_TRUE(ret);
            EXPECT_NE(route_next, (Route *)NULL);
            if (route_next) {
                EXPECT_EQ(route_next->nexthop_, i+1);
            }
            route = route_next;
        }
    }
    route_next = itbl_->FindNext(route);
    EXPECT_EQ(route_next, (Route *)NULL);
}

TEST_F(PatriciaTest, FindnRemove) {
    std::size_t i;
    Route *route;
    Route route_key;
    bool    ret;

    for (i = 0; i < rt_size; i++) {
        route_key.ip_ = rt[i].ip;
        route_key.len_ = rt[i].len;
        route = itbl_->Find(&route_key);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);
        delete route;
    }

    route = itbl_->GetNext(NULL);
    EXPECT_EQ(route, (Route *)NULL);

    for (i = 0; i < rt_size; i++) {
        route = new Route(rt[i].ip, rt[i].len, rt[i].nh);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, false);
    }

    i = 0;
    route = NULL;
    while ((route = itbl_->GetNext(route))) {
        i++;
    }
    EXPECT_EQ(i, rt_size);

    for (i = rt_size; i != 0; i--) {
        route_key.ip_ = rt[i-1].ip;
        route_key.len_ = rt[i-1].len;
        route = itbl_->Find(&route_key);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);
        delete route;
    }
}

TEST_F(PatriciaTest, Reinsert) {
    std::size_t i;
    std::size_t j;
    Route *route;
    Route *route_1;
    Route route_key;
    bool    ret;

    for (i = 0; i < rt_size; i++) {
        route_key.ip_ = rt[i].ip;
        route_key.len_ = rt[i].len;
        route = itbl_->Find(&route_key);
        EXPECT_NE(route, (Route *)NULL);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Remove(route);
        EXPECT_EQ(ret, false);

        j = 0;
        route_1 = NULL;
        while ((route_1 = itbl_->GetNext(route_1))) {
            j++;
        }
        EXPECT_EQ(j, rt_size - 1);

        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, true);
        ret = itbl_->Insert(route);
        EXPECT_EQ(ret, false);

        j = 0;
        route_1 = NULL;
        while ((route_1 = itbl_->GetNext(route_1))) {
            j++;
        }
        EXPECT_EQ(j, rt_size);
    }
    EXPECT_EQ(i, rt_size);

    i = 0;
    route = NULL;
    while ((route = itbl_->GetNext(route))) {
        i++;
    }
    EXPECT_EQ(i, rt_size);
}

TEST_F(PatriciaTest, LPMFind1) {
    Rt rt_key[] = {{0x01010011, 32, 0},
                   {0x01110101, 32, 0},
                   {0x01010000, 16, 0},
                   {0x01010001, 32, 0}
                   };
    Route *route;
    Route route_key;

    route_key.ip_ = rt_key[0].ip;
    route_key.len_ = rt_key[0].len;
    route = itbl_->LPMFind(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    EXPECT_EQ(route->nexthop_, 3);

    route_key.ip_ = rt_key[1].ip;
    route_key.len_ = rt_key[1].len;

    route = itbl_->LPMFind(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    EXPECT_EQ(route->nexthop_, 2);

    route_key.ip_ = rt_key[2].ip;
    route_key.len_ = rt_key[2].len;

    route = itbl_->LPMFind(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    EXPECT_EQ(route->nexthop_, 2);

    route_key.ip_ = rt_key[3].ip;
    route_key.len_ = rt_key[3].len;

    route = itbl_->LPMFind(&route_key);
    EXPECT_NE(route, (Route *)NULL);
    EXPECT_EQ(route->nexthop_, 4);
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
