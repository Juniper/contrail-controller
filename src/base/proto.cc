/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/proto.h"

#include <boost/bind.hpp>

using namespace std;

namespace detail {
    bool debug_ = false;
}

struct ParseContext::StackFrame {
    StackFrame() : offset(0), lensize(0), size(-1), total_size(-1) {
    }
    int offset; // offset of the data pointer at present
    int lensize; //size of the length of the current element being parsed
    size_t size;
    size_t total_size;
    std::auto_ptr<ParseObject> data;
};

ParseContext::ParseContext()
    : offset_(0) {
}

struct deleter {
    template <typename T>
    void operator()(T *ptr) {
        delete ptr;
    }
};

ParseContext::~ParseContext() {
    for_each(stack_.begin(), stack_.end(), deleter());
    stack_.clear();
}

ParseObject *ParseContext::release() {
    if (stack_.empty()) {
        return NULL;
    }
    StackFrame *current = stack_.back();
    return current->data.release();
}

void ParseContext::Push(ParseObject *data) {
    StackFrame *frame = new StackFrame();
    frame->data.reset(data);
    stack_.push_back(frame);
}

ParseObject *ParseContext::Pop() {
    if (stack_.size() <= 1) {
        return NULL;
    }
    StackFrame *frame = stack_.back();
    ParseObject *obj = frame->data.release();
    stack_.pop_back();
    delete frame;
    return obj;
}

void ParseContext::ReleaseData() {
    if (!stack_.empty()) {
        StackFrame *frame = stack_.back();
        frame->data.release();
    }
}
void ParseContext::SwapData(ParseObject *obj) {
    if (!stack_.empty() && obj) {
        StackFrame *frame = stack_.back();
        frame->data.reset(obj);
    } else {
        delete obj;
    }
}

ParseObject *ParseContext::data() {
    if (stack_.empty()) {
        return NULL;
    }
    StackFrame *current = stack_.back();
    return current->data.get();
}

void ParseContext::advance(int delta) {
    if (!stack_.empty()) {
        stack_.back()->offset += delta;
    }
    offset_ += delta;
}

void ParseContext::set_lensize(int length) {
    if (!stack_.empty()) {
        StackFrame *current = stack_.back();
        current->lensize = length;
    }
}

int ParseContext::lensize() const {
    if (stack_.empty()) {
        return -1;
    }
    StackFrame *current = stack_.back();
    return current->lensize;
}

void ParseContext::set_size(size_t length) {
    if (!stack_.empty()) {
        StackFrame *current = stack_.back();
        current->size = length;
    }
}

size_t ParseContext::size() const {
    if (stack_.empty()) {
        return -1;
    }
    StackFrame *current = stack_.back();
    return current->size;
}

void ParseContext::set_total_size() {
    if (!stack_.empty()) {
        StackFrame *current = stack_.back();
        current->total_size = current->size;
    }
}

size_t ParseContext::total_size() const {
    if (stack_.empty()) {
        return -1;
    }
    StackFrame *current = stack_.back();
    return current->total_size >= 0 ? current->total_size : current->size;
}

void ParseContext::SetError(int error, int subcode, std::string type,
                            const uint8_t *data, int data_size) {
    if (error_context_.error_code) {
        // Error already set. Ignore this one.
        return;
    }
    error_context_.error_code = error;
    error_context_.error_subcode = subcode;
    error_context_.type_name = type;
    error_context_.data = data;
    error_context_.data_size = data_size;
}

struct EncodeContext::StackFrame {
    typedef boost::function<void(EncodeContext *)> CallbackFn;
    typedef std::vector<CallbackFn>::iterator CallbackIterator;
    StackFrame() : offset(0) {
    }
    int offset;
    std::vector<CallbackFn> callbacks;
};

EncodeContext::EncodeContext() {
}

EncodeContext::~EncodeContext() {
}

void EncodeContext::Push() {
    StackFrame *frame = new StackFrame();
    stack_.push_back(frame);
}

void EncodeContext::Pop(bool callback) {
    StackFrame *frame = &stack_.back();
    if (callback) {
        for (StackFrame::CallbackIterator iter =
             frame->callbacks.begin(); iter != frame->callbacks.end(); ++iter) {
            StackFrame::CallbackFn cb = *iter;
            (cb)(this);
        }
    }

    int p_offset = frame->offset;
    stack_.pop_back();                  // deletes frame
    if (!stack_.empty()) {
        StackFrame *top = &(stack_.back());
        top->offset += p_offset;
    }
}

void EncodeContext::advance(int delta) {
    if (stack_.empty()) {
        return;
    }
    StackFrame *frame = &stack_.back();
    frame->offset += delta;
}

int EncodeContext::length() const {
    const StackFrame *frame = &stack_.back();
    return frame->offset;
};

void EncodeContext::AddCallback(CallbackType cb, uint8_t *data,
                                int element_size) {
    StackFrame *top = &(stack_.back());
    top->callbacks.push_back(
        boost::bind(cb, _1, data, top->offset, element_size));
}

void EncodeOffsets::SaveOffset(std::string key, int offset) {
    std::pair<std::map<std::string, int>::iterator, bool > result =
            offsets_.insert(make_pair(key, offset));
    if (!result.second) {
        PROTO_DEBUG("Offset for " << key << " already set");
    }
}

void EncodeContext::SaveOffset(std::string key) {
    int length = 0;
    for (boost::ptr_vector<StackFrame>::iterator it = stack_.begin();
         it != stack_.end(); ++it) {
        length += it->offset;
    }
    PROTO_DEBUG("Saving Offset for " << key << " at " << length);
    offsets_.SaveOffset(key, length);
}

int EncodeOffsets::FindOffset(const char *key) {
    std::map<std::string, int>::iterator it = offsets_.find(std::string(key));
    if (it == offsets_.end())
        return -1;
    return it->second;
}
