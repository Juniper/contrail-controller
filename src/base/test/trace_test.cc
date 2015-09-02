/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"
#include "base/trace.h"

namespace {

class TraceTest : public ::testing::Test {
};

class TraceStruct {
    char data[4096];
};

TEST_F(TraceTest, DISABLED_1MillionTraceWrite) {
    // Enable trace
    Trace<TraceStruct>::GetInstance()->TraceOn();
    // Create tracebuffer
    boost::shared_ptr<TraceBuffer<TraceStruct> > trace_buf(
            Trace<TraceStruct>::GetInstance()->TraceBufAdd("1MillionWriteTraceBuf",
            1000, true));
    // Write 1 million entries to the tracebuf
    for (int i = 0; i < 1000000; i++) {
        TraceStruct *ni(new TraceStruct);
        trace_buf->TraceWrite(ni);
    }
}
} // namespace

template<> Trace<TraceStruct>
        *Trace<TraceStruct>::trace_ = NULL;

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
