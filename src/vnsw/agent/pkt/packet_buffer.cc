/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <string>
#include <boost/shared_ptr.hpp>
#include <pkt/packet_buffer.h>
#include <pkt/tap_interface.h>

PacketBufferManager::PacketBufferManager(PktModule *pkt_module) :
    alloc_(0), free_(0), pkt_module_(pkt_module) {
}

PacketBufferManager::~PacketBufferManager() {
}

PacketBufferPtr PacketBufferManager::Allocate(uint32_t module, uint16_t len,
                                              uint32_t mdata) {
    PacketBufferPtr ptr(new PacketBuffer(this, module, len, mdata));
    alloc_++;
    return ptr;
}

PacketBufferPtr PacketBufferManager::Allocate(uint32_t module, uint8_t *buff,
                                              uint16_t len,
                                              uint16_t data_offset,
                                              uint16_t data_len,
                                              uint32_t mdata) {
    PacketBufferPtr ptr(new PacketBuffer(this, module, buff, len, data_offset,
                                         data_len, mdata));
    alloc_++;
    return ptr;
}

void PacketBufferManager::FreeIndication(PacketBuffer *pkt) {
    free_++;
}

PacketBuffer::PacketBuffer(PacketBufferManager *mgr, uint32_t module,
                           uint16_t len, uint32_t mdata) :
    buffer_(new uint8_t[len]), buffer_len_(len), data_(buffer_.get()),
    data_len_(len), module_(module), mdata_(mdata), mgr_(mgr) {
}

PacketBuffer::PacketBuffer(PacketBufferManager *mgr, uint32_t module,
                           uint8_t *buff, uint16_t len, uint16_t data_offset,
                           uint16_t data_len, uint32_t mdata) :
    buffer_(buff), buffer_len_(len), data_(buffer_.get() + data_offset),
    data_len_(data_len), module_(module), mdata_(mdata), mgr_(mgr) {
}

PacketBuffer::~PacketBuffer() {
    mgr_->FreeIndication(this);
}

uint8_t *PacketBuffer::data() const {
    return data_;
}

uint16_t PacketBuffer::data_len() const {
    return data_len_;
}
