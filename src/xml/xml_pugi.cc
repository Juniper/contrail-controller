/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/replace.hpp>
#include "xml/xml_base.h"
#include "xml/xml_pugi.h"
#include "base/logging.h"

// This is internal implementation detail specific to this lib to make
// exposed methods efficient. 

//XmlPugi::tmp_("");
pugi::xml_attribute XmlPugi::GAttr;
pugi::xml_node XmlPugi::GNode;

XmlPugi::XmlPugi() : writer_(this), node_(GNode), attrib_(GAttr) {
}

XmlPugi::~XmlPugi() {
    doc_.reset();
}

pugi::xml_node XmlPugi::RootNode() {
    return(doc_.root());
}

const char *XmlPugi::ReadNode(const std::string &name) {
    PugiPredicate p1(name);

    pugi::xml_node node = doc_.find_node(p1);
    if (IsNull(node)) {
        return NULL;
    }

    SetContext(node);
    if (node.type() != pugi::node_pi) {
        for (node = node.first_child(); node; node = node.next_sibling()) {
            // traverse to first plain character data or char data,
            // skipping XML comments
            if (node.type() == pugi::node_pcdata || 
                node.type() == pugi::node_cdata)
               break; 
        }
    }

    return node.value();

}

pugi::xml_node XmlPugi::FindNode(const std::string &name) {
    PugiPredicate p1(name);

    pugi::xml_node node = doc_.find_node(p1);
    return node;
}

const char *XmlPugi::ReadNodeName(const std::string &name) {
    PugiPredicate p1(name);

    pugi::xml_node node = doc_.find_node(p1);
    SetContext(node);

    return node.name();
}

const char *XmlPugi::ReadNodeValue() {
    pugi::xml_node node = node_;
    
    if (node.type() != pugi::node_pi) {
        for (node = node.first_child(); node; node = node.next_sibling()) {
            // traverse to first plain character data or char data,
            // skipping XML comments
            if (node.type() == pugi::node_pcdata || 
                node.type() == pugi::node_cdata)
               break; 
        }
    }
    return node.value();
}

const char *XmlPugi::ReadChildNode() {
    pugi::xml_node tmp = (node_ == GNode) ? doc_.first_child() :
                          node_.first_child();
    SetContext(tmp);
    return tmp.value();
}

const char *XmlPugi::ReadChildNodeName() {
    pugi::xml_node tmp = (node_ == GNode) ? doc_.first_child() :
                          node_.first_child();
    SetContext(tmp);
    return tmp.name();
}

const char *XmlPugi::ReadNextNode() {
    pugi::xml_node tmp = node_.next_sibling();

    SetContext(tmp);
    return tmp.value();
}

const char *XmlPugi::ReadNextNodeName() {
    pugi::xml_node tmp = node_.next_sibling();

    SetContext(tmp);
    return tmp.name();
}

void XmlPugi::RewindNode() {
    SetContext(node_);
}

const char *XmlPugi::ReadAttrib(const std::string &str) {
    pugi::xml_attribute tmp = node_.attribute(str.c_str());
    SetContext(node_, tmp);
    return tmp.value();
}

const char *XmlPugi::ReadFirstAttrib() {
    pugi::xml_attribute tmp = node_.first_attribute();
    SetContext(node_, tmp);
    return tmp.value();
}

const char *XmlPugi::ReadNextAttrib() {
    pugi::xml_attribute tmp = attrib_.next_attribute();
    SetContext(node_, tmp);
    return tmp.value();
}

void XmlPugi::RewindAttrib() {
    SetContext(node_, GAttr);
}

const char *XmlPugi::ReadParentName() {
    pugi::xml_node tmp = node_.parent();
    SetContext(tmp);
    return tmp.name();
}

int XmlPugi::WriteDoc(uint8_t *buf) {
    buf_tmp_ = buf; // this will be used by writer_->write
    ts_ = 0;
    doc_.save(writer_, "", pugi::format_default, pugi::encoding_utf8);

    if (ts_ == 0) return -1;
    return static_cast<int>(ts_);
}

int XmlPugi::WriteRawDoc(uint8_t *buf) {
    buf_tmp_ = buf; // this will be used by writer_->write
    ts_ = 0;
    doc_.save(writer_, "", pugi::format_raw | pugi::format_no_declaration);

    if (ts_ == 0) return -1;
    return static_cast<int>(ts_);
}

void XmlPugi::PrintDoc(std::ostream& os) const {
    doc_.print(os, " ", pugi::format_raw | pugi::format_no_declaration);
}

void XmlPugi::PrintDocFormatted(std::ostream& os) const {
    doc_.print(os, " ", pugi::format_indent | pugi::format_no_declaration);
}

void XmlPugi::SetContext(pugi::xml_node node, pugi::xml_attribute attrib) {
    node_ = node;
    attrib_ = attrib;
}

int XmlPugi::LoadDoc(const std::string &document) {
    RewindDoc();
    doc_.reset();

    pugi::xml_parse_result ret = doc_.load_buffer(document.c_str(), document.size(), 
                                                 pugi::parse_default, 
                                                 pugi::encoding_utf8);
    if (ret == false) {
        LOG(DEBUG, "XML doc load failed, code: " << ret << " " << ret.description());
        LOG(DEBUG, "Error offset: " << ret.offset << " (error at [..." << (document.c_str() + ret.offset) << "]");
        LOG(DEBUG, "Document: " << document);
        return -1;
    }
    return 0;
}

void XmlPugi::RewindDoc() {
    SetContext();
}

void XmlPugi::AppendDoc(const std::string &node_name, XmlBase *a_doc) {
    std::string str;
    pugi::xml_node node_s;
    pugi::xml_node node1;
    pugi::xml_node node2;

    PugiPredicate p1(node_name);
    node1 = doc_.find_node(p1);
    SetContext(node1);

    XmlPugi *xp = static_cast<XmlPugi *>(a_doc);
    node_s = xp->doc_.first_child();
    if (!IsNull(node1)) {
        node2 = node1.parent().append_copy(node_s);
        SetContext(node2);
    }

}
    
int XmlPugi::AddNode(const std::string &key, const std::string &value) {
    pugi::xml_node node;

    if (IsNull(node_)) {
        node = doc_.append_child(key.c_str());
    } else {
        node = node_.parent().append_child(key.c_str());
    }
    if (value != "") {
        node.text().set(value.c_str());
    }

    SetContext(node);
    return 0;
}

int XmlPugi::DeleteNode(const std::string &key) {
    PugiPredicate p1(key);
    pugi::xml_node node = doc_.find_node(p1); 
    if (IsNull(node))
        return -1;

    node.parent().remove_child(key.c_str());
    return 0;
}

int XmlPugi::ModifyNode(const std::string &key, const std::string &value) {
    PugiPredicate p1(key);
    pugi::xml_node node = doc_.find_node(p1); 
    if (IsNull(node_))
        return -1;

    node.text() = value.c_str();

    SetContext(node); 
    return 0;
}

int XmlPugi::AddChildNode(const std::string &key, const std::string &value) {
    pugi::xml_node node;

    if (IsNull(node_)) {
        node = doc_.append_child(key.c_str());
    } else {
        node = node_.append_child(key.c_str());
    }
    if (value != "") {
        node.text().set(value.c_str());
    }

    SetContext(node);
    return 0;
}

int XmlPugi::AddChildNodeAfter(const std::string &node_name, 
                               const std::string &key, 
                               const std::string &value) {

    PugiPredicate p1(node_name);
    pugi::xml_node node1 = doc_.find_node(p1);
    SetContext(node1);

    if (!IsNull(node1)) {
        AddChildNode(key, value);
    }

    return 0;
}


int XmlPugi::AddAttribute(const std::string &key, const std::string &value) {
    if (IsNull(node_)) 
        return -1;

    pugi::xml_attribute attrib; 

    if (IsNull(attrib_) ) {
        attrib = node_.append_attribute(key.c_str()) = value.c_str();
    } else {
        attrib = 
            node_.insert_attribute_after(key.c_str(), attrib_) = value.c_str();
    }

    SetContext(node_, attrib);
    return 0;
}

int XmlPugi::DeleteAttribute(const std::string &key) {
    if (IsNull(node_))
        return -1;
    pugi::xml_attribute tmp = node_.attribute(key.c_str());
    bool res;
    if (IsNull(tmp)) {
        return -1;
    } else {
         pugi::xml_attribute attrib = tmp.next_attribute();
         res = node_.remove_attribute(tmp);
         SetContext(node_, attrib);
         return (res ? -1 : 0);
    }
    return 0;
}

int XmlPugi::ModifyAttribute(const std::string &key, const std::string &value) {
    if (node_.type() == pugi::node_null)
        return -1;

    //ReadAttrib(key);
    pugi::xml_attribute tmp = node_.attribute(key.c_str());
    if (IsNull(tmp)) {
        return -1;
    } else {
        tmp.set_value(value.c_str());
        SetContext(node_, tmp);
    }

    return 0;
}

void XmlPugi::xmpp_buf_write::write(const void *data, size_t sz) {
    ref->SetBuf(data, sz);
}

void XmlPugi::SetBuf(const void *buf, size_t sz) {
    memcpy(buf_tmp_ + ts_, buf, sz);
    ts_ += sz;
    *(buf_tmp_+ts_) = '\0';
}
