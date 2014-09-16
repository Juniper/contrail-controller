/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_packet_buffer_hpp
#define vnsw_agent_pkt_packet_buffer_hpp

#include <string>
#include <stdint.h>
#include <boost/shared_ptr.hpp>
#include <base/util.h>

class PacketBuffer;
class PacketBufferManager;
class AgentHdr;
class PktHandler;
class PktModule;

typedef boost::shared_ptr<PacketBuffer> PacketBufferPtr;
class PacketBuffer {
public:
    static const uint32_t kDefaultBufferLen = 1024;
    virtual ~PacketBuffer();

    uint8_t *buffer() const { return buffer_.get(); }
    uint16_t buffer_len() const { return buffer_len_; }

    uint8_t *data() const;
    uint16_t data_len() const;

    uint32_t module() const { return module_; }
    void set_module(uint32_t module) { module_ = module; }

    void set_len(uint32_t len);
    bool SetOffset(uint16_t offset);
private:
    friend class PacketBufferManager;
    PacketBuffer(PacketBufferManager *mgr, uint32_t module, uint16_t len,
                 uint32_t mdata);

    // Create PacketBuffer from existing memory
    PacketBuffer(PacketBufferManager *mgr, uint32_t module, uint8_t *buff,
                 uint16_t len, uint16_t data_offset, uint16_t data_len,
                 uint32_t mdata);

    boost::shared_ptr<uint8_t> buffer_;
    uint16_t buffer_len_;

    uint8_t *data_;
    uint16_t data_len_;

    uint32_t module_;
    uint32_t mdata_;
    PacketBufferManager *mgr_;
    DISALLOW_COPY_AND_ASSIGN(PacketBuffer);
};

class PacketBufferManager {
public:
    PacketBufferManager(PktModule *pkt_module);
    virtual ~PacketBufferManager();

    PacketBufferPtr Allocate(uint32_t module, uint16_t len, uint32_t mdata);

    PacketBufferPtr Allocate(uint32_t module, uint8_t *buff, uint16_t len,
                             uint16_t data_offset, uint16_t data_len,
                             uint32_t mdata);
private:
    friend class PacketBuffer;
    void FreeIndication(PacketBuffer *);

    uint64_t alloc_;
    uint64_t free_;
    PktModule *pkt_module_;

    DISALLOW_COPY_AND_ASSIGN(PacketBufferManager);
};

#endif // vnsw_agent_pkt_packet_buffer_hpp
