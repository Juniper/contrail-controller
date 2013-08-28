/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
package net.juniper.contrail.api;

import java.util.ArrayList;
import java.util.List;

public class ObjectReference<AttrType extends ApiPropertyBase> {
    List<String> to;
    String href;
    AttrType attr;
    String uuid;

    public ObjectReference() {
    }

    public ObjectReference(List<String> to, AttrType attr) {
        this.to = to;
        this.attr = attr;
    }
    public void setReference(List<String> to, AttrType attr, String href, String uuid) {
        this.to = new ArrayList<String>(to);
        this.attr = attr;
        this.href = href;
        this.uuid = uuid;
    }
    public String getHRef() {
        return href;
    }
    public String getUuid() {
        return uuid;
    }
    public List<String> getReferredName() {
        return to;
    }
    public AttrType getAttr() {
        return attr;
    }
    
    public static <T extends ApiPropertyBase> String getReferenceListUuid(List<ObjectReference<T>> reflist) {
        if (reflist != null && !reflist.isEmpty()) {
            ObjectReference<T> ref = reflist.get(0);
            return ref.getUuid();
        }
        return null;
    }
}
