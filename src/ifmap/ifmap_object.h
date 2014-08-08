/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ifmap_entry_h
#define ctrlplane_ifmap_entry_h

#include <boost/crc.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/dynamic_bitset.hpp>

#include "base/util.h"
#include "ifmap/ifmap_origin.h"

namespace pugi {
class xml_node;
}

struct AutogenProperty;
class IFMapTable;

class IFMapObject {
public:
    IFMapObject();
    virtual ~IFMapObject();
    virtual std::string ToString() const = 0;

    virtual void EncodeUpdate(pugi::xml_node *parent) const = 0;

    void set_origin(IFMapOrigin origin) { origin_ = origin; }

    uint64_t sequence_number() { return sequence_number_; }
    const uint64_t sequence_number() const { return sequence_number_; }
    void set_sequence_number(uint64_t sequence_number) {
        sequence_number_ = sequence_number;
    }
    IFMapOrigin origin() const { return origin_; }
    virtual bool ResolveStaleness() = 0; // return true if something was stale
    virtual boost::crc_32_type::value_type CalculateCrc() const = 0;

private:
    friend class IFMapNode;
    friend void intrusive_ptr_add_ref(IFMapObject *object);
    friend void intrusive_ptr_release(IFMapObject *object);
    friend void intrusive_ptr_add_ref(const IFMapObject *object);
    friend void intrusive_ptr_release(const IFMapObject *object);
    
    static void Release(IFMapObject *object);

    boost::intrusive::list_member_hook<> node_;
    mutable int refcount_;
    uint64_t sequence_number_;
    IFMapOrigin origin_;
    DISALLOW_COPY_AND_ASSIGN(IFMapObject);
};

typedef boost::intrusive_ptr<IFMapObject> IFMapObjectRef;
inline void intrusive_ptr_add_ref(IFMapObject *object) {
    if (object == NULL) {
        return;
    }
    object->refcount_++;
}
inline void intrusive_ptr_release(IFMapObject *object) {
    if (object == NULL) {
        return;
    }
    if (--object->refcount_ == 0) {
        IFMapObject::Release(object);
    }
}
inline void intrusive_ptr_add_ref(const IFMapObject *object) {
    object->refcount_++;
}
inline void intrusive_ptr_release(const IFMapObject *object) {
    if (--object->refcount_ == 0) {
        IFMapObject::Release(const_cast<IFMapObject *>(object));
    }
}

// Base class for entries that correspond to IF-MAP identifiers
class IFMapIdentifier : public IFMapObject {
public:
    IFMapIdentifier();
    explicit IFMapIdentifier(int property_count);

    virtual void SetProperty(const std::string &attr_key,
                             AutogenProperty *data) = 0;
    virtual void ClearProperty(const std::string &attr_key) = 0;

    // Return true if any properties are set.
    virtual bool empty() const {
        return true;
    }

    void TransferPropertyToOldProperty();
    bool ResolveStalePropertiesAndResetOld();
    bool IsPropertySet(int id) const {return property_set_.test(id);};
    virtual bool ResolveStaleness();

protected:
    boost::dynamic_bitset<> property_set_;
    boost::dynamic_bitset<> old_property_set_;

private:
    DISALLOW_COPY_AND_ASSIGN(IFMapIdentifier);
};

// Base class for entries that correspond to meta-data with attributes
class IFMapLinkAttr : public IFMapObject {
public:
    IFMapLinkAttr();
    virtual bool SetData(const AutogenProperty *data) = 0;
    virtual bool ResolveStaleness();

private:
    DISALLOW_COPY_AND_ASSIGN(IFMapLinkAttr);
};

#endif
