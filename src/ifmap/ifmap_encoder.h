/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_encoder__
#define __ctrlplane__ifmap_encoder__

#include <pugixml/pugixml.hpp>

class IFMapNode;
class IFMapLink;
class IFMapUpdate;

class IFMapMessage {
public:
    static const int kObjectsPerMessage = 16;
    IFMapMessage();

    void Close();
    // set the 'to' field in the message
    void SetReceiverInMsg(const std::string &cli_identifier);
    void SetObjectsPerMessage(int num);
    void EncodeUpdate(const IFMapUpdate *update);
    bool IsFull();
    bool IsEmpty();
    void Reset();

    const char *c_str() const;

private:
    enum Op {
        NONE,
        UPDATE,
        DELETE
    };
    void Open();
    void EncodeNode(const IFMapUpdate *update);
    void EncodeLink(const IFMapUpdate *update);

    pugi::xml_document doc_;
    pugi::xml_node config_;
    Op op_type_;             // the current  type of op_node_
    pugi::xml_node op_node_;
    std::string str_;
    int node_count_;
    int objects_per_message_;
};

#endif /* defined(__ctrlplane__ifmap_encoder__) */
