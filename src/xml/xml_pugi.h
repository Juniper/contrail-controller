/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XML_PUGI_H__
#define __XML_PUGI_H__

#include <pugixml/pugixml.hpp>

class XmlPugi : public XmlBase {
public:

    virtual int LoadDoc(const std::string &doc);
    virtual int WriteDoc(uint8_t *buf);
    virtual int WriteRawDoc(uint8_t *buf);
    virtual void PrintDoc(std::ostream& os) const;
    virtual void PrintDocFormatted(std::ostream& os) const;
    virtual int AddNode(const std::string &key, const std::string &value);
    virtual int DeleteNode(const std::string &key);
    virtual int ModifyNode(const std::string &key, const std::string &value);
    virtual int AddChildNode(const std::string &key, const std::string &value);
    virtual int AddChildNodeAfter(const std::string &node_name,
                                  const std::string &key, const std::string &value);
    virtual int AddAttribute(const std::string &key, const std::string &value);
    virtual int DeleteAttribute(const std::string &key);
    virtual int ModifyAttribute(const std::string &key, const std::string &value);
 
    // Read methods
    virtual const char *ReadNode(const std::string &name);
    virtual const char *ReadNodeName(const std::string &name);
    virtual const char *ReadNodeValue();
    virtual const char *ReadChildNode();
    virtual const char *ReadChildNodeName();
    virtual const char *ReadNextNode();
    virtual const char *ReadNextNodeName();
    virtual void RewindNode();

    virtual const char *ReadAttrib(const std::string &str); 
    virtual const char *ReadFirstAttrib();
    virtual const char *ReadNextAttrib();
    virtual void RewindAttrib();

    virtual const char *ReadParentName();

    // resets node and attributes
    virtual void RewindDoc();
    virtual void AppendDoc(const std::string &str, XmlBase *a_doc);

    pugi::xml_node RootNode();
    pugi::xml_node FindNode(const std::string &name);

    XmlPugi();
    virtual ~XmlPugi();

    struct xmpp_buf_write : pugi::xml_writer {
        xmpp_buf_write(XmlPugi *arg) : ref(arg) {}
        virtual void write(const void *data, size_t sz); 
        XmlPugi *ref;
    };

    void SetBuf(const void *buf, size_t sz);

    bool IsNull(pugi::xml_node &node) { return node.type() == pugi::node_null; }
    bool IsNull(pugi::xml_attribute &attr) { 
        return attr == NULL; 
    }

private:
    uint8_t *buf_tmp_;
    size_t ts_; //temp size
    struct xmpp_buf_write writer_;

    pugi::xml_document    doc_;

    // Foll maintains traversal context
    pugi::xml_node        node_;
    pugi::xml_attribute   attrib_;

    struct PugiPredicate {
        bool operator()(pugi::xml_attribute attr) const {
            return (strcmp(attr.name(), tmp_.c_str()) == 0);
        }
        bool operator()(pugi::xml_node node) const {
            return (strcmp(node.name(), tmp_.c_str()) == 0); 
        }
        PugiPredicate(const std::string &name) : tmp_(name) { }
        std::string tmp_;
    };

    static pugi::xml_attribute GAttr;
    static pugi::xml_node GNode;
    void SetContext(pugi::xml_node node = GNode, 
                    pugi::xml_attribute atrib = GAttr);

};

#endif //  __XML_PUGI_H__
