/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/trace.h"

namespace {

class TraceTest : public ::testing::Test {
};

TEST_F(TraceTest, 1MillionIntWrite) {
    // Enable trace
    Trace<int>::GetInstance()->TraceOn();
    // Create tracebuffer
    boost::shared_ptr<TraceBuffer<int> > trace_buf(
            Trace<int>::GetInstance()->TraceBufAdd("1MillionIntWriteTraceBuf",
            1000, true));
    // Write 1 million entries to the tracebuf
    for (int i = 0; i < 1000000; i++) {
        trace_buf->TraceWrite(i);
    }
}
} // namespace

template<> Trace<int>
        *Trace<int>::trace_ = NULL;

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
