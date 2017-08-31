/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <testing/gunit.h>
#include <base/time_util.h>
#include <base/string_util.h>
#include <database/gendb_constants.h>
#include <database/gendb_if.h>
#include "../utils.h"

class UtilsTest : public ::testing::Test {
protected:
    UtilsTest() { }
};

TEST_F(UtilsTest, Parsetime) {
    uint64_t value, value1, value2;

    value1 = UTCTimestampUsec();
    EXPECT_TRUE(parse_time("\"now\"", &value));
    value2 = UTCTimestampUsec();
    EXPECT_TRUE((value1 <= value) && (value <= value2));

    value1 = UTCTimestampUsec()-(uint64_t)5*3600*1000000;
    EXPECT_TRUE(parse_time("\"now-5h\"", &value));
    value2 = UTCTimestampUsec()-(uint64_t)5*3600*1000000;
    EXPECT_TRUE((value1 <= value) && (value <= value2));

    value1 = UTCTimestampUsec()-(uint64_t)120*60*1000000;
    EXPECT_TRUE(parse_time("\"now-120m\"", &value));
    value2 = UTCTimestampUsec()-(uint64_t)120*60*1000000;
    EXPECT_TRUE((value1 <= value) && (value <= value2));

    value1 = UTCTimestampUsec()+(uint64_t)3600*1000000;
    EXPECT_TRUE(parse_time("\"now+3600s\"", &value));
    value2 = UTCTimestampUsec()+(uint64_t)3600*1000000;
    EXPECT_TRUE((value1 <= value) && (value <= value2));

    value1 = UTCTimestampUsec()+(uint64_t)2*24*3600*1000000;
    EXPECT_TRUE(parse_time("\"now+2d\"", &value));
    value2 = UTCTimestampUsec()+(uint64_t)2*24*3600*1000000;
    EXPECT_TRUE((value1 <= value) && (value <= value2));

    value1 = UTCTimestampUsec();
    std::string time_str(integerToString(value1));
    EXPECT_TRUE(parse_time(time_str, &value));
    EXPECT_TRUE(value == value1);

    EXPECT_FALSE(parse_time("\"now+2d2h\"", &value));

    EXPECT_FALSE(parse_time("\"now+\"", &value));

    EXPECT_FALSE(parse_time("\"now+2hours\"", &value));

    EXPECT_FALSE(parse_time("\"now-2m2d\"", &value));

    EXPECT_FALSE(parse_time("\"now+2minutes\"", &value));

    EXPECT_FALSE(parse_time("now-2m", &value));

    EXPECT_FALSE(parse_time("4hours", &value));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}
