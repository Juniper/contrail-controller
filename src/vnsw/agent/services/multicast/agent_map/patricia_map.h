/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_patricia_map_h
#define vnsw_agent_patricia_map_h

#include "base/patricia.h"

#include <stdint.h>

class PatriciaNode {
public:
    PatriciaNode(int klen, int koffset, void *knode, const void *key_ptr) :
            klen_(klen), koffset_(koffset), knode_(knode), key_ptr_(key_ptr) {
    }

    class Key {
    public:
        static std::size_t BitLength(const PatriciaNode *key) {
            return key->klen_*8;
        }

        static char ByteValue(const PatriciaNode *key, std::size_t i) {
            const char *ch = (const char *)key->key_ptr_;
            return ch[key->koffset_ + key->klen_ - i - 1];
        }
    };

    int klen_;
    int koffset_;
    void *knode_;
    const void *key_ptr_;
    Patricia::Node node_;
};

template <class D, Patricia::Node D::* P, class K>
class PatriciaMap : public Patricia::Tree<D, P, K> {
public:
    PatriciaMap(uint16_t klen, uint8_t koffset) :
                Patricia::Tree<D, P, K>(), klen_(klen), koffset_(koffset) {
    }

    uint16_t klen_;
    uint8_t koffset_;
};

typedef PatriciaMap<PatriciaNode, &PatriciaNode::node_, PatriciaNode::Key> PatriciaMapTable;

#endif /* vnsw_agent_patricia_map_h */
