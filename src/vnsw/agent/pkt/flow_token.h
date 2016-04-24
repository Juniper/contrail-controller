#ifndef __AGENT_PKT_FLOW_TOKEN_H__
#define __AGENT_PKT_FLOW_TOKEN_H__

#include <memory>
#include <tbb/atomic.h>
#include <base/util.h>

class FlowToken;
class FlowTokenPool;
class FlowEntry;
class FlowProto;

typedef std::auto_ptr<FlowToken> FlowTokenPtr;

class FlowToken {
public:
    FlowToken(FlowTokenPool *pool, FlowEntry *flow);
    virtual ~FlowToken();

private:
    FlowEntry *flow_;
    FlowTokenPool *pool_;
    DISALLOW_COPY_AND_ASSIGN(FlowToken);
};

class FlowTokenPool {
public:
    FlowTokenPool(const std::string &name, FlowProto *proto, int count);
    virtual ~FlowTokenPool();

    FlowTokenPtr GetToken(FlowEntry *flow);
    int token_count() const { return token_count_; }
    bool TokenCheck() const;
    uint64_t failures() const { return failures_; }
    void IncrementRestarts() { restarts_++; }
    uint64_t restarts() const { return restarts_; }
private:
    friend class FlowToken;

    // FlowToken destructor invokes this
    void FreeToken();

    std::string name_;
    int max_tokens_;
    int min_tokens_;
    int low_water_mark_;
    tbb::atomic<int> token_count_;
    mutable uint64_t failures_;
    uint64_t restarts_;
    FlowProto *proto_;
    DISALLOW_COPY_AND_ASSIGN(FlowTokenPool);
};
#endif //  __AGENT_PKT_FLOW_TOKEN_H__
