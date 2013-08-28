/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XML_BASE_H__
#define __XML_BASE_H__

#include "base/util.h"

class XmlBase {

public:
    // Set new xml doc. Null string means reset to new doc. 
    // Resets previous doc
    virtual int LoadDoc(const std::string &doc) = 0;

    // returns bytes encoded. -1 for error.
    virtual int WriteDoc(uint8_t *buf)= 0;
    virtual int WriteRawDoc(uint8_t *buf) = 0;

    // prints the xml doc to ostream
    virtual void PrintDoc(std::ostream& os) const = 0;

    // Create child node and move node context to new node
    virtual int AddChildNode(const std::string &key, const std::string &value) = 0;

    // Create child node after node-name and move node context to new node
    virtual int AddChildNodeAfter(const std::string &node_name,
                                  const std::string &key, const std::string &value) = 0;

    // Create new node as sibling of node in context. Set context to new node
    virtual int AddNode(const std::string &key, const std::string &value) = 0;

    // Delete node
    virtual int DeleteNode(const std::string &key) = 0;

    // Find child node by key and set its value. Set context to new node.
    virtual int ModifyNode(const std::string &key, const std::string &value) = 0;

    // Add attriburte to current node context. Sets context to new attrib.
    virtual int AddAttribute(const std::string &key, const std::string &value) = 0;

    // Modify attriburte to current node context. Sets context to new attrib.
    virtual int ModifyAttribute(const std::string &key, const std::string &value) = 0;

    // Delete attribute, if exist, at current node context. Sets context to
    // next available attrib.
    virtual int DeleteAttribute(const std::string &key) = 0;

    // Return 'value' of given node 'name'. Set context to given node.
    virtual const char *ReadNode(const std::string &name) = 0;

    // Return 'value' of given node 'name'. Set context to given node.
    virtual const char *ReadNodeName(const std::string &name) = 0;

    // Return 'value' of given node.
    virtual const char *ReadNodeValue() = 0;

    // Moves node context to first child node.
    virtual const char *ReadChildNode() = 0;

    // Moves node context to first child node.
    virtual const char *ReadChildNodeName() = 0;

    // Get next sibling. Moves node context to next sibling
    virtual const char *ReadNextNode() = 0;

    // Get next sibling. Moves node context to next sibling
    virtual const char *ReadNextNodeName() = 0;

    // Moves node context to immediate parent
    virtual void RewindNode() = 0;

    virtual const char *ReadAttrib(const std::string &str) = 0; 
    virtual const char *ReadFirstAttrib() = 0;
    virtual const char *ReadNextAttrib() = 0;
    virtual void RewindAttrib() = 0;

    // Get Parent node. Move node context to parent
    virtual const char *ReadParentName() = 0;
    
    virtual void RewindDoc() = 0;
    virtual void AppendDoc(const std::string &str, XmlBase *a_doc) = 0;

    virtual ~XmlBase() { }
    XmlBase() {}
};

struct XmppXmlImplFactory {
public:

    XmlBase *GetXmlImpl() ;
    void ReleaseXmlImpl(XmlBase *tmp); 

    static XmppXmlImplFactory *Instance();

private:
    //singleton
    XmppXmlImplFactory() {  }
    static XmppXmlImplFactory *Inst_;

    DISALLOW_COPY_AND_ASSIGN(XmppXmlImplFactory);
};

#endif // __XML_BASE_H__
