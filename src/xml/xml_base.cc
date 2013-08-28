/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xml/xml_base.h"
#include "xml/xml_pugi.h"

XmppXmlImplFactory *XmppXmlImplFactory::Inst_ = NULL;

XmlBase *XmppXmlImplFactory::GetXmlImpl() {
    return new XmlPugi();
}

void XmppXmlImplFactory::ReleaseXmlImpl(XmlBase *tmp) {
    delete tmp;
}

XmppXmlImplFactory *XmppXmlImplFactory::Instance() { 
    if (Inst_ == NULL) 
        Inst_ = new XmppXmlImplFactory();
    return Inst_;
}
