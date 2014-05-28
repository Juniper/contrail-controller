/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_proto.h"

#include "base/proto.h"
#include "base/logging.h"
#include "bgp/bgp_common.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp_server.h"
#include "net/bgp_af.h"

using namespace std;
namespace mpl = boost::mpl;

BgpProto::OpenMessage::OpenMessage()
    : BgpMessage(OPEN), as_num(-1), holdtime(-1), identifier(-1) {
}

BgpProto::OpenMessage::~OpenMessage() {
    STLDeleteValues(&opt_params);
}

//
// Validate the capabilities in an incoming Open Message.
//
// Return the capability code that's missing if a problem is detected.
// Return 0 if capabilities in the message are OK.
//
// Note that we must not generate an error if we see a capability that we
// do not support.
//
// Go through all the capabilities and make sure that there's at least one
// MpExtension with an (afi, safi) pair that's also configured on the peer.
//
//
int BgpProto::OpenMessage::ValidateCapabilities(BgpPeer *peer) const {
    bool mp_extension_ok = false;

    // Go through each OptParam in the OpenMessage.
    for (vector<OptParam *>::const_iterator param_it = opt_params.begin();
         param_it != opt_params.end(); ++param_it) {
        const OptParam *param = *param_it;

        // Go through each Capability in the OptParam.
        for (vector<Capability *>::const_iterator cap_it =
             param->capabilities.begin();
             cap_it != param->capabilities.end(); ++cap_it) {
            const Capability *cap = *cap_it;

            // See if the (afi,safi) in the MpExtension is configured on peer.
            if (cap->code == Capability::MpExtension) {
                const uint8_t *data = cap->capability.data();
                uint16_t afi = get_value(data, 2);
                uint8_t safi = get_value(data + 3, 1);
                Address::Family family = BgpAf::AfiSafiToFamily(afi, safi);
                if (peer->LookupFamily(family))
                    mp_extension_ok = true;
            }
        }
    }

    if (!mp_extension_ok)
        return Capability::MpExtension;

    return 0;
}

//
// Validate an incoming Open Message.
//
// Return one of the values from BgpProto::Notification::OpenMsgSubCode if
// an error is detected.
// Return 0 if message is OK.
//
int BgpProto::OpenMessage::Validate(BgpPeer *peer) const {
    if (identifier == 0) {
        BGP_LOG_PEER(Message, peer, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Bad BGP Identifier: " << 0);
        return BgpProto::Notification::BadBgpId;
    }
    if (identifier == peer->server()->bgp_identifier()) {
        BGP_LOG_PEER(Message, peer, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN,
                     "Bad (Same as mine) BGP Identifier: " << identifier);
        return BgpProto::Notification::BadBgpId;
    }
    if (as_num != peer->peer_as()) {
        BGP_LOG_PEER(Message, peer, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Bad Peer AS Number: " << as_num);
        return BgpProto::Notification::BadPeerAS;
    }

    int result = ValidateCapabilities(peer);
    if (result != 0) {
        BGP_LOG_PEER(Message, peer, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                     BGP_PEER_DIR_IN, "Unsupported Capability: " << result);
        return BgpProto::Notification::UnsupportedCapability;
    }
    return 0;
}

const string BgpProto::OpenMessage::ToString() const {
    std::ostringstream os;

    // Go through each OptParam in the OpenMessage.
    for (vector<OptParam *>::const_iterator param_it = opt_params.begin();
         param_it != opt_params.end(); ++param_it) {
        const OptParam *param = *param_it;

        // Go through each Capability in the OptParam.
        vector<Capability *>::const_iterator cap_it =
             param->capabilities.begin();
        while (cap_it != param->capabilities.end()) {
            const Capability *cap = *cap_it;

            os << "Code "<< Capability::CapabilityToString(cap->code);

            if (cap->code == Capability::MpExtension) {
                const uint8_t *data = cap->capability.data();
                uint16_t afi = get_value(data, 2);
                uint8_t safi = get_value(data + 3, 1);
                Address::Family family = BgpAf::AfiSafiToFamily(afi, safi);
                os << " Family  " << Address::FamilyToString(family);
            }

            if (++cap_it == param->capabilities.end()) break;
            os << ", ";
        }

    }
    return os.str();
}

BgpProto::Notification::Notification()
    : BgpMessage(NOTIFICATION), error(0), subcode(0) {
}

const std::string BgpProto::Notification::ToString() const {
    return toString(static_cast<BgpProto::Notification::Code>(error), subcode);
}

const std::string BgpProto::Notification::toString(BgpProto::Notification::Code code,
                                                   int sub_code) {
    std::string msg("");
    switch (code) {
        case MsgHdrErr:
            msg += std::string("Message Header Error:") +
                MsgHdrSubcodeToString(static_cast<MsgHdrSubCode>(sub_code));
            break;
        case OpenMsgErr:
            msg += std::string("OPEN Message Error:") +
                OpenMsgSubcodeToString(static_cast<OpenMsgSubCode>(sub_code));
            break;
        case UpdateMsgErr:
            msg += std::string("UPDATE Message Error:") + 
                UpdateMsgSubCodeToString(static_cast<UpdateMsgSubCode>(sub_code));
            break;
        case HoldTimerExp:
            msg += "Hold Timer Expired";
            break;
        case FSMErr:
            msg += std::string("Finite State Machine Error:") + 
                FsmSubcodeToString(static_cast<FsmSubcode>(sub_code));
            break;
        case Cease:
            msg += std::string("Cease:") + 
                CeaseSubcodeToString(static_cast<CeaseSubCode>(sub_code));
            break;
        default:
            msg += "Unknown";
            break;
    }
    return msg;
}

BgpProto::Keepalive::Keepalive()
    : BgpMessage(KEEPALIVE) {
}

BgpProto::Update::Update()
    : BgpMessage(UPDATE) {
}

BgpProto::Update::~Update() {
    STLDeleteValues(&withdrawn_routes);
    STLDeleteValues(&path_attributes);
    STLDeleteValues(&nlri);
}

struct BgpAttrCodeCompare {
    bool operator()(BgpAttribute *lhs, BgpAttribute *rhs) {
        return lhs->code < rhs->code;
    }
};

//Validate an incoming Update Message
//Returns 0 if message is OK
//Returns one of the values from enum UpdateMsgSubCode if an error is detected
int BgpProto::Update::Validate(const BgpPeer *peer, std::string &data) {
    BgpAttrCodeCompare comp;
    std::sort(path_attributes.begin(), path_attributes.end(), comp);
    bool origin = false, nh = false, as_path = false, mp_reach_nlri = false, local_pref = false;

    bool ibgp = (peer->PeerType() == IBGP);

    BgpAttrSpec::const_iterator it;
    std::string rxed_attr("Path attributes : ");
    for (it = path_attributes.begin(); it < path_attributes.end(); it++) {
        if (it+1 < path_attributes.end() && (*it)->code == (*(it+1))->code) {
            //duplicate attributes
            return BgpProto::Notification::MalformedAttributeList;
        }

        rxed_attr +=  (*it)->ToString() + " ";

        if ((*it)->code == BgpAttribute::Origin)
            origin = true;
        if ((*it)->code == BgpAttribute::LocalPref)
            local_pref = true;
        else if ((*it)->code == BgpAttribute::NextHop)
            nh = true;
        else if ((*it)->code == BgpAttribute::AsPath) {
            as_path = true;
            AsPathSpec *asp = static_cast<AsPathSpec *>(*it);
            // Check segments size for ebpg, 
            // IBGP can have empty path for routes that are originated
            if (!ibgp) {
                if (!asp->path_segments.size() ||
                    !asp->path_segments[0]->path_segment.size())
                    return BgpProto::Notification::MalformedASPath;
            }
        } else if ((*it)->code == BgpAttribute::MPReachNlri)
            mp_reach_nlri = true;
    }

    BGP_LOG_PEER(Message, const_cast<BgpPeer *>(peer),
                 SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
                 BGP_PEER_DIR_IN, rxed_attr);
    if (nlri.size() > 0 && !nh) {
        // next-hop attribute must be present if IPv4 NLRI is present
        char attrib_type = BgpAttribute::NextHop;
        data = std::string(&attrib_type, 1);
        return BgpProto::Notification::MissingWellKnownAttrib;
    }
    if (nlri.size() > 0 || mp_reach_nlri) {
        // origin and as_path must be present if any NLRI is present
        if (!origin) {
            char attrib_type = BgpAttribute::Origin;
            data = std::string(&attrib_type, 1);
            return BgpProto::Notification::MissingWellKnownAttrib;
        }
        if (!as_path) {
            char attrib_type = BgpAttribute::AsPath;
            data = std::string(&attrib_type, 1);
            return BgpProto::Notification::MissingWellKnownAttrib;
        }

        // All validations for IBGP
        if (ibgp && !local_pref) {
            // If IBGP, local_pref is mandatory
            char attrib_type = BgpAttribute::LocalPref;
            data = std::string(&attrib_type, 1);
            return BgpProto::Notification::MissingWellKnownAttrib;
        }
    }
    return 0;
}

int BgpProto::Update::CompareTo(const BgpProto::Update &rhs) const{
    KEY_COMPARE(withdrawn_routes.size(), rhs.withdrawn_routes.size());
    for (size_t i=0; i < withdrawn_routes.size(); i++) {
        KEY_COMPARE(withdrawn_routes[i]->prefixlen, rhs.withdrawn_routes[i]->prefixlen);
        KEY_COMPARE(withdrawn_routes[i]->prefix, rhs.withdrawn_routes[i]->prefix);
    }

    KEY_COMPARE(path_attributes.size(), rhs.path_attributes.size());
    for (size_t i = 0; i < rhs.path_attributes.size(); i++) {
        int ret = path_attributes[i]->CompareTo(*rhs.path_attributes[i]);
        if (ret != 0) {
            cout << "Unequal " << TYPE_NAME(*path_attributes[i]) << endl;
            return ret;
        }
    }

    KEY_COMPARE(nlri.size(), rhs.nlri.size());
    for (size_t i=0; i<rhs.nlri.size(); i++) {
        KEY_COMPARE(nlri[i]->prefixlen, rhs.nlri[i]->prefixlen);
        KEY_COMPARE(nlri[i]->prefix, rhs.nlri[i]->prefix);
    }
    return 0;
}

class BgpMarker : public ProtoElement<BgpMarker> {
public:
    static const int kSize = 16;
    static const int kErrorCode = BgpProto::Notification::MsgHdrErr;
    static const int kErrorSubcode = BgpProto::Notification::ConnNotSync;
    static bool Verifier(const void *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        for (int i = 0; i < 16; i++) {
            if (data[i] != 0xff) return false;
        }
        return true;
    }
    static void Writer(const void *msg, uint8_t *data, size_t size) {
        for (int i = 0; i < 16; i++) {
            data[i] = 0xff;
        }
    }
};

class BgpMsgLength : public ProtoElement<BgpMsgLength> {
public:
    static const int kSize = 2;
    static const int kErrorCode = BgpProto::Notification::MsgHdrErr;
    static const int kErrorSubcode = BgpProto::Notification::BadMsgLength;
    struct Offset {
        std::string operator()() {
            return "BgpMsgLength";
        }
    };
    typedef Offset SaveOffset;
    static bool Verifier(const void *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        int value = get_short(data);
        if (value < BgpProto::kMinMessageSize ||
            value > BgpProto::kMaxMessageSize) {
            return false;
        }
        if ((size_t) value < context->offset() + size) {
            return false;
        }
        return true;
    }
    struct SetLength {
        static void Callback(EncodeContext *context, uint8_t *data,
                             int offset, int element_size) {
            put_value(data, kSize, context->length());
        }
    };
    typedef SetLength EncodingCallback;
};

// BGP OPEN
class BgpOpenVersion : public ProtoElement<BgpOpenVersion> {
public:
    static const int kSize = 1;
    static const int kErrorCode = BgpProto::Notification::OpenMsgErr;
    static const int kErrorSubcode = BgpProto::Notification::UnsupportedVersion;
    static bool Verifier(const void * obj, const uint8_t *data, size_t size,
                          ParseContext *context) {
        return data[0] == 0x4;
    }
    static void Writer(const void *msg, uint8_t *data, size_t size) {
        *data = 0x4;
    }
};

class BgpOpenAsNum : public ProtoElement<BgpOpenAsNum> {
public:
    static const int kSize = 2;
    typedef Accessor<BgpProto::OpenMessage, uint32_t,
        &BgpProto::OpenMessage::as_num> Setter;
    
};

class BgpHoldTime : public ProtoElement<BgpHoldTime> {
public:
    static const int kSize = 2;
    static const int kErrorCode = BgpProto::Notification::OpenMsgErr;
    static const int kErrorSubcode =
            BgpProto::Notification::UnacceptableHoldTime;
    static bool Verifier(const BgpProto::OpenMessage *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint16_t value = get_value(data, 2);
        return (value == 0 || value >= 3);
    }
    typedef Accessor<BgpProto::OpenMessage, int32_t,
        &BgpProto::OpenMessage::holdtime> Setter;
};

class BgpIdentifier : public ProtoElement<BgpIdentifier> {
public:
    static const int kSize = 4;
    typedef Accessor<BgpProto::OpenMessage, uint32_t,
        &BgpProto::OpenMessage::identifier> Setter;
};

class BgpOpenCapabilityCode : public ProtoElement<BgpOpenCapabilityCode> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpProto::OpenMessage::Capability, int,
    &BgpProto::OpenMessage::Capability::code> Setter;
    
};

class BgpOpenCapabilityLength : public ProtoElement<BgpOpenCapabilityLength> {
public:
    static const int kSize = 1;
    typedef int SequenceLength;
};

class BgpOpenCapabilityValue : public ProtoElement<BgpOpenCapabilityValue> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<BgpProto::OpenMessage::Capability, uint8_t,
    		&BgpProto::OpenMessage::Capability::capability> Setter;
};

class BgpOpenCapability : public ProtoSequence<BgpOpenCapability> {
public:
    typedef mpl::list<BgpOpenCapabilityCode, BgpOpenCapabilityLength,
        BgpOpenCapabilityValue> Sequence;
};

class BgpOpenCapabilities : public ProtoSequence<BgpOpenCapabilities> {
public:
    static const int kSize = 1;
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    typedef mpl::list<BgpOpenCapability> Sequence;

    typedef CollectionAccessor<BgpProto::OpenMessage::OptParam,
        vector<BgpProto::OpenMessage::Capability *>,
        &BgpProto::OpenMessage::OptParam::capabilities> ContextStorer;
    
    struct OptMatch {
        bool match(const BgpProto::OpenMessage::OptParam *ctx) {
            return !ctx->capabilities.empty();
        }
    };
    typedef OptMatch ContextMatch;
};

class BgpOpenOptParamChoice : public ProtoChoice<BgpOpenOptParamChoice> {
public:
    static const int kSize = 1;
    static const int kErrorCode = BgpProto::Notification::OpenMsgErr;
    static const int kErrorSubcode =
            BgpProto::Notification::UnsupportedOptionalParam;
    typedef mpl::map<
        mpl::pair<mpl::int_<BgpProto::OpenMessage::OPEN_OPT_CAPABILITIES>,
                  BgpOpenCapabilities>
    > Choice;
};

class BgpOpenOptParam : public ProtoSequence<BgpOpenOptParam> {
public:
    static const int kSize = 1;
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    typedef CollectionAccessor<BgpProto::OpenMessage,
        vector<BgpProto::OpenMessage::OptParam *>,
        &BgpProto::OpenMessage::opt_params> ContextStorer;
    typedef mpl::list<BgpOpenOptParamChoice> Sequence;
};

class BgpOpenMessage : public ProtoSequence<BgpOpenMessage> {
public:
    typedef mpl::list<BgpOpenVersion,
                      BgpOpenAsNum,
                      BgpHoldTime,
                      BgpIdentifier,
                      BgpOpenOptParam> Sequence;
    typedef BgpProto::OpenMessage ContextType;
};

class NotificationErrorCode : public ProtoElement<NotificationErrorCode> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpProto::Notification, int,
        &BgpProto::Notification::error> Setter;
};

class NotificationErrorSubcode : public ProtoElement<NotificationErrorSubcode> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpProto::Notification, int,
        &BgpProto::Notification::subcode> Setter;    
};

class NotificationData : public ProtoElement<NotificationData> {
public:
    static const int kSize = -1;
    typedef Accessor<BgpProto::Notification, string,
        &BgpProto::Notification::data> Setter;
};

class BgpNotificationMessage : public ProtoSequence<BgpNotificationMessage> {
public:
    typedef mpl::list<NotificationErrorCode,
                      NotificationErrorSubcode,
                      NotificationData> Sequence;
    typedef BgpProto::Notification ContextType;
};

class BgpKeepaliveMessage : public ProtoSequence<BgpKeepaliveMessage> {
public:
    typedef mpl::list<> Sequence;
    typedef BgpProto::Keepalive ContextType;
};

class BgpPrefixLen : public ProtoElement<BgpPrefixLen> {
public:
    static const int kSize = 1;
    struct PrefixLen {
        int operator()(BgpProtoPrefix *obj, const uint8_t *data, size_t size) {
            int bits = data[0];
            return (bits + 7) / 8;
        }
    };
    typedef PrefixLen SequenceLength;
    typedef Accessor<BgpProtoPrefix, int,
            &BgpProtoPrefix::prefixlen> Setter;
};

class BgpPrefixAddress : public ProtoElement<BgpPrefixAddress> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<BgpProtoPrefix, uint8_t,
            &BgpProtoPrefix::prefix> Setter;
};

class BgpUpdateWithdrawnRoutes : public ProtoSequence<BgpUpdateWithdrawnRoutes> {
public:
    static const int kSize = 2;
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    static const int kErrorCode = BgpProto::Notification::UpdateMsgErr;
    static const int kErrorSubcode =
            BgpProto::Notification::MalformedAttributeList;

    typedef CollectionAccessor<BgpProto::Update,
            vector<BgpProtoPrefix *>,
            &BgpProto::Update::withdrawn_routes> ContextStorer;

    struct OptMatch {
        bool match(const BgpProto::Update *ctx) {
            return !ctx->withdrawn_routes.empty();
        }
    };
    typedef OptMatch ContextMatch;

    typedef mpl::list<BgpPrefixLen, BgpPrefixAddress> Sequence;
};

class BgpPathAttrLength : public ProtoElement<BgpPathAttrLength> {
public:
    static const int kSize = -1;
    struct AttrLen {
        int operator()(BgpAttribute *obj,
                       const uint8_t *data, size_t &size) {
            if (obj->flags & BgpAttribute::ExtendedLength) {
                // Extended Length: use 2 bytes to read
                size = 2;
            } else {
                size = 1;
            }
            return get_value(data, size);
        }
    };
    typedef AttrLen SequenceLength;
    struct AttrSizeSet {
        static int get(const BgpAttribute *obj) {
            if (obj->flags & BgpAttribute::ExtendedLength) {
                // Extended Length: use 2 bytes to read
                return 2;
            } else {
                return 1;
            }
        }
    };
    typedef AttrSizeSet SizeSetter;
};

template<class Derived>
struct BgpContextSwap {
    Derived *operator()(const BgpAttribute *attr) {
        return new Derived(*attr);
    }
};

template<class C>
struct BgpAttributeVerifier {
    static bool Verifier(const C *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        int pre = (obj->flags & BgpAttribute::ExtendedLength) ? 4 : 3;
        if (C::kSize > 0 && (int)context->size() != C::kSize) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribLengthError,
                    TYPE_NAME(C), data - pre, context->size() + pre);
            return false;
        }
        if ((obj->flags & BgpAttribute::FLAG_MASK) != C::kFlags) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribFlagsError,
                    TYPE_NAME(C), data - pre, context->size() + pre);
            return false;
        }
        return true;
    }
};

template <>
struct BgpAttributeVerifier<BgpAttrOrigin> {
    static bool Verifier(const BgpAttrOrigin *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        int pre = (obj->flags & BgpAttribute::ExtendedLength) ? 4 : 3;
        if (context->size() != 1) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribLengthError,
                    "BgpAttrOrigin", data - pre, size + pre);
            return false;
        }
        if ((obj->flags & BgpAttribute::FLAG_MASK) != BgpAttrOrigin::kFlags) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribFlagsError,
                    "BgpAttrOrigin", data - pre, size + pre);
            return false;
        }
        uint8_t value = data[0];
        if (value != BgpAttrOrigin::IGP && value != BgpAttrOrigin::EGP &&
                value != BgpAttrOrigin::INCOMPLETE) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::InvalidOrigin,
                    "BgpAttrOrigin", data - pre, size + pre);
            return false;
        }
        return true;
    }
};

template <>
struct BgpAttributeVerifier<BgpAttrNextHop> {
    static bool Verifier(const BgpAttrNextHop *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        int pre = (obj->flags & BgpAttribute::ExtendedLength) ? 4 : 3;
        if ((int)context->size() != BgpAttrNextHop::kSize) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribLengthError,
                    "BgpAttrNextHop", data - pre, size + pre);
            return false;
        }
        if ((obj->flags & BgpAttribute::FLAG_MASK) != BgpAttrNextHop::kFlags) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribFlagsError,
                    "BgpAttrNextHop", data - pre, size + pre);
            return false;
        }
        uint32_t value = get_value(data, size);
        //TODO: More checks are needed
        if (value == 0) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::InvalidNH,
                    "BgpAttrNextHop", data - pre, size + pre);
        return false;
    }
    return true;
    }
};

template <>
struct BgpAttributeVerifier<AsPathSpec::PathSegment> {
    static bool Verifier(const AsPathSpec::PathSegment *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return true;
    }
};

template <int Size, class C, typename T, T C::*Member>
class BgpAttributeValue :
    public ProtoElement<BgpAttributeValue<Size, C, T, Member> > {
public:
    static const int kSize = Size;
    static bool Verifier(const C *obj, const uint8_t *data, size_t size,
                             ParseContext *context) {
        return BgpAttributeVerifier<C>::Verifier(obj, data, size, context);
    }
    typedef Accessor<C, T, Member> Setter;
};

template<class C, int Size, typename T, T C::*M>
class BgpAttrTemplate :
    public ProtoSequence<BgpAttrTemplate<C, Size, T, M> > {
public:
    static const int kErrorCode = BgpProto::Notification::UpdateMsgErr;
    static const int kErrorSubcode = BgpProto::Notification::AttribLengthError;
    typedef C ContextType;
    typedef BgpContextSwap<C> ContextSwap;
    typedef mpl::list<BgpPathAttrLength, BgpAttributeValue<Size, C, T, M>
            > Sequence;
};

class BgpPathAttributeAtomicAggregate :
    public ProtoSequence<BgpPathAttributeAtomicAggregate> {
public:
    static bool Verifier(const BgpAttrAtomicAggregate * obj,
                         const uint8_t *data, size_t size,
                         ParseContext *context) {
        if (data[0] != 0) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribLengthError,
                    "BgpAttrAtomicAggregate", data - 2, data[0] + 3);
            return false;
        }
        if (obj->flags != BgpAttrAtomicAggregate::kFlags) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                    BgpProto::Notification::AttribFlagsError,
                    "BgpAttrAtomicAggregate", data - 2, 3);
            return false;
        }
        return true;
    }
    typedef BgpAttrAtomicAggregate ContextType;
    typedef BgpContextSwap<BgpAttrAtomicAggregate> ContextSwap;
    typedef mpl::list<BgpPathAttrLength> Sequence;
};

class BgpPathAttributeAggregator :
    public ProtoSequence<BgpPathAttributeAggregator> {
public:
    typedef BgpAttrAggregator ContextType;
    typedef BgpContextSwap<BgpAttrAggregator> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
            BgpAttributeValue<2, BgpAttrAggregator, as_t,
                                  &BgpAttrAggregator::as_num>,
            BgpAttributeValue<4, BgpAttrAggregator, uint32_t,
                                  &BgpAttrAggregator::address>
    > Sequence;
};

class BgpPathAttrAsPathSegmentLength :
    public ProtoElement<BgpPathAttrAsPathSegmentLength> {
public:
    static const int kSize = 1;
    struct PathSegmentLength {
        int operator()(AsPathSpec::PathSegment *obj,
                       const uint8_t *data, size_t size) {
            return get_value(data, 1) * 2;
        }
    };
    typedef PathSegmentLength SequenceLength;
    struct SetLength {
        static void Callback(EncodeContext *context, uint8_t *data,
                             int offset, int element_size) {
            int len = get_value(data, kSize);
            put_value(data, kSize, len/2);
        }
    };
    typedef SetLength EncodingCallback;
};


class BgpPathAttrAsPathSegmentValue :
    public ProtoElement<BgpPathAttrAsPathSegmentValue> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<AsPathSpec::PathSegment, as_t,
                           &AsPathSpec::PathSegment::path_segment> Setter;
};

class BgpPathAttrAsPathSegmentList :
    public ProtoSequence<BgpPathAttrAsPathSegmentList> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;

    static bool Verifier(const AsPathSpec *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return BgpAttributeVerifier<AsPathSpec>::Verifier(obj, data, size,
                                                          context);
    }

    typedef CollectionAccessor<AsPathSpec,
            vector<AsPathSpec::PathSegment *>,
            &AsPathSpec::path_segments> ContextStorer;

    typedef mpl::list<BgpAttributeValue<1, AsPathSpec::PathSegment, int,
                      &AsPathSpec::PathSegment::path_segment_type>,
            BgpPathAttrAsPathSegmentLength,
            BgpPathAttrAsPathSegmentValue
    > Sequence;
};

class BgpPathAttributeAsPath : public ProtoSequence<BgpPathAttributeAsPath> {
public:
    typedef AsPathSpec ContextType;
    typedef BgpContextSwap<AsPathSpec> ContextSwap;
    typedef mpl::list<BgpPathAttrLength, BgpPathAttrAsPathSegmentList> Sequence;
};

class BgpPathAttributeFlags : public ProtoElement<BgpPathAttributeFlags> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpAttribute, uint8_t, &BgpAttribute::flags> Setter;
};

class BgpPathAttributeCommunityList :
    public ProtoElement<BgpPathAttributeCommunityList> {
public:
    static const int kSize = -1;
    static bool Verifier(const CommunitySpec *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return BgpAttributeVerifier<CommunitySpec>::Verifier(obj, data, size,
                                                             context);
    }

    typedef VectorAccessor<CommunitySpec, uint32_t,
                           &CommunitySpec::communities> Setter;
};

class BgpPathAttributeCommunities :
    public ProtoSequence<BgpPathAttributeCommunities> {
public:
    typedef CommunitySpec ContextType;
    typedef BgpContextSwap<CommunitySpec> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
                      BgpPathAttributeCommunityList> Sequence;
};

class BgpPathAttributeExtendedCommunityList :
    public ProtoElement<BgpPathAttributeExtendedCommunityList> {
public:
    static const int kSize = -1;
    static bool Verifier(const ExtCommunitySpec *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return BgpAttributeVerifier<ExtCommunitySpec>::Verifier(obj, data, size,
                                                                context);
    }

    typedef VectorAccessor<ExtCommunitySpec, uint64_t,
                           &ExtCommunitySpec::communities> Setter;
};

class BgpPathAttributeExtendedCommunities :
    public ProtoSequence<BgpPathAttributeExtendedCommunities> {
public:
    typedef ExtCommunitySpec ContextType;
    typedef BgpContextSwap<ExtCommunitySpec> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
                      BgpPathAttributeExtendedCommunityList> Sequence;
};

class BgpPathAttributeDiscoveryEdgeAddressLen :
    public ProtoElement<BgpPathAttributeDiscoveryEdgeAddressLen> {
public:
    static const int kSize = 1;
    static bool Verifier(const void *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return (value == 4);
    }
    typedef int SequenceLength;
};

class BgpPathAttributeDiscoveryEdgeAddressValue :
    public ProtoElement<BgpPathAttributeDiscoveryEdgeAddressValue> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<EdgeDiscoverySpec::Edge, uint8_t,
            &EdgeDiscoverySpec::Edge::address> Setter;
};

class BgpPathAttributeDiscoveryEdgeAddress :
    public ProtoSequence<BgpPathAttributeDiscoveryEdgeAddress> {
public:
    typedef mpl::list<BgpPathAttributeDiscoveryEdgeAddressLen,
            BgpPathAttributeDiscoveryEdgeAddressValue> Sequence;
};

class BgpPathAttributeDiscoveryEdgeLabelLen :
    public ProtoElement<BgpPathAttributeDiscoveryEdgeLabelLen> {
public:
    static const int kSize = 1;
    static bool Verifier(const void *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return (value > 0 && value % 8 == 0);
    }
    typedef int SequenceLength;
};

class BgpPathAttributeDiscoveryEdgeLabelValues :
    public ProtoElement<BgpPathAttributeDiscoveryEdgeLabelValues> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<EdgeDiscoverySpec::Edge, uint32_t,
            &EdgeDiscoverySpec::Edge::labels> Setter;
};

class BgpPathAttributeDiscoveryEdgeLabels :
    public ProtoSequence<BgpPathAttributeDiscoveryEdgeLabels> {
public:
    typedef mpl::list<BgpPathAttributeDiscoveryEdgeLabelLen,
            BgpPathAttributeDiscoveryEdgeLabelValues> Sequence;
};

class BgpPathAttributeDiscoveryEdgeList :
    public ProtoSequence<BgpPathAttributeDiscoveryEdgeList> {
public:
    static const int kMinOccurs = 1;
    static const int kMaxOccurs = -1;

    static bool Verifier(const EdgeDiscoverySpec *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return BgpAttributeVerifier<EdgeDiscoverySpec>::Verifier(
                obj, data, size, context);
    }

    typedef CollectionAccessor<EdgeDiscoverySpec,
            vector<EdgeDiscoverySpec::Edge *>,
            &EdgeDiscoverySpec::edge_list> ContextStorer;

    typedef mpl::list<BgpPathAttributeDiscoveryEdgeAddress,
            BgpPathAttributeDiscoveryEdgeLabels> Sequence;
};

class BgpPathAttributeEdgeDiscovery :
    public ProtoSequence<BgpPathAttributeEdgeDiscovery> {
public:
    typedef EdgeDiscoverySpec ContextType;
    typedef BgpContextSwap<EdgeDiscoverySpec> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
            BgpPathAttributeDiscoveryEdgeList> Sequence;
};

class BgpPathAttributeForwardingEdgeLen :
    public ProtoElement<BgpPathAttributeForwardingEdgeLen> {
public:
    static const int kSize = 1;
    static bool Verifier(const void *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        uint8_t value = get_value(data, 1);
        return (value == 4);
    }
    struct GetLength {
        int operator()(EdgeForwardingSpec::Edge *obj,
                       const uint8_t *data, size_t size) {
            obj->address_len = get_value(data, 1) * 2 + 8;
            return obj->address_len;
        }
    };
    typedef GetLength SequenceLength;
    struct SetLength {
        static void Callback(EncodeContext *context, uint8_t *data,
                             int offset, int element_size) {
            int len = get_value(data, kSize);
            put_value(data, kSize, (len - 8) / 2);
        }
    };
    typedef SetLength EncodingCallback;
};

class BgpPathAttributeForwardingEdgeAddressLen :
    public ProtoElement<BgpPathAttributeForwardingEdgeAddressLen> {
public:
    static const int kSize = 0;
    struct GetLength {
        int operator()(EdgeForwardingSpec::Edge *obj,
                       const uint8_t *data, size_t size) {
            return (obj->address_len - 8) / 2;
        }
    };
    typedef GetLength SequenceLength;
};

class BgpPathAttributeForwardingEdgeInAddressValue :
    public ProtoElement<BgpPathAttributeForwardingEdgeInAddressValue> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<EdgeForwardingSpec::Edge, uint8_t,
            &EdgeForwardingSpec::Edge::inbound_address> Setter;
};

class BgpPathAttributeForwardingEdgeInAddress :
    public ProtoSequence<BgpPathAttributeForwardingEdgeInAddress> {
public:
    static const int kMinOccurs = 1;
    static const int kMaxOccurs = 1;
    typedef mpl::list<BgpPathAttributeForwardingEdgeAddressLen,
            BgpPathAttributeForwardingEdgeInAddressValue> Sequence;
};

class BgpPathAttributeForwardingEdgeInLabel :
    public ProtoElement<BgpPathAttributeForwardingEdgeInLabel> {
public:
    static const int kSize = 4;
    typedef Accessor<EdgeForwardingSpec::Edge, uint32_t,
            &EdgeForwardingSpec::Edge::inbound_label> Setter;
};

class BgpPathAttributeForwardingEdgeOutAddressValue :
    public ProtoElement<BgpPathAttributeForwardingEdgeOutAddressValue> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<EdgeForwardingSpec::Edge, uint8_t,
            &EdgeForwardingSpec::Edge::outbound_address> Setter;
};

class BgpPathAttributeForwardingEdgeOutAddress :
    public ProtoSequence<BgpPathAttributeForwardingEdgeOutAddress> {
public:
    static const int kMinOccurs = 1;
    static const int kMaxOccurs = 1;
    typedef mpl::list<BgpPathAttributeForwardingEdgeAddressLen,
            BgpPathAttributeForwardingEdgeOutAddressValue> Sequence;
};

class BgpPathAttributeForwardingEdgeOutLabel :
    public ProtoElement<BgpPathAttributeForwardingEdgeOutLabel> {
public:
    static const int kSize = 4;
    typedef Accessor<EdgeForwardingSpec::Edge, uint32_t,
            &EdgeForwardingSpec::Edge::outbound_label> Setter;
};

class BgpPathAttributeForwardingEdgeList :
    public ProtoSequence<BgpPathAttributeForwardingEdgeList> {
public:
    static const int kMinOccurs = 1;
    static const int kMaxOccurs = -1;

    static bool Verifier(const EdgeForwardingSpec *obj, const uint8_t *data,
                         size_t size, ParseContext *context) {
        return BgpAttributeVerifier<EdgeForwardingSpec>::Verifier(
                obj, data, size, context);
    }

    typedef CollectionAccessor<EdgeForwardingSpec,
            vector<EdgeForwardingSpec::Edge *>,
            &EdgeForwardingSpec::edge_list> ContextStorer;

    typedef mpl::list<BgpPathAttributeForwardingEdgeLen,
            BgpPathAttributeForwardingEdgeInAddress,
            BgpPathAttributeForwardingEdgeInLabel,
            BgpPathAttributeForwardingEdgeOutAddress,
            BgpPathAttributeForwardingEdgeOutLabel> Sequence;
};

class BgpPathAttributeEdgeForwarding :
    public ProtoSequence<BgpPathAttributeEdgeForwarding> {
public:
    typedef EdgeForwardingSpec ContextType;
    typedef BgpContextSwap<EdgeForwardingSpec> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
            BgpPathAttributeForwardingEdgeList> Sequence;
};

class BgpPathAttributeReserved : public ProtoElement<BgpPathAttributeReserved> {
public:
    const static int kSize = 1;
    static void Writer(const void *msg, uint8_t *data, size_t size) {
        *data = 0;
    }
};

class BgpPathAttributeMpNlriNextHopLength :
    public ProtoElement<BgpPathAttributeMpNlriNextHopLength> {
public:
    const static int kSize = 1;
    typedef int SequenceLength;
};


class BgpPathAttributeMpNlriNexthopAddr : public ProtoElement<BgpPathAttributeMpNlriNexthopAddr> {
public:
    static const int kSize = -1;
    typedef VectorAccessor<BgpMpNlri, uint8_t,
    		&BgpMpNlri::nexthop> Setter;
};

class BgpPathAttributeMpNlriNextHop :
    public ProtoSequence<BgpPathAttributeMpNlriNextHop> {
public:
    typedef mpl::list<BgpPathAttributeMpNlriNextHopLength,
                      BgpPathAttributeMpNlriNexthopAddr> Sequence;
};

class BgpPathAttributeMpNlri : public ProtoSequence<BgpPathAttributeMpNlri> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;

    struct OptMatch {
        bool match(const BgpMpNlri *obj) {
            return 
                (((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Unicast)) ||
                 ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Vpn)));
        }
    };

    typedef OptMatch ContextMatch;
    typedef CollectionAccessor<BgpMpNlri, vector<BgpProtoPrefix *>,
            &BgpMpNlri::nlri> ContextStorer;

    typedef mpl::list<BgpPrefixLen, BgpPrefixAddress> Sequence;
};

class BgpPathAttributeMpRTargetNlri : 
    public ProtoSequence<BgpPathAttributeMpRTargetNlri> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;

    struct OptMatch {
        bool match(const BgpMpNlri *obj) {
            return ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::RTarget));
        }
    };

    typedef OptMatch ContextMatch;

    typedef CollectionAccessor<BgpMpNlri, vector<BgpProtoPrefix *>,
            &BgpMpNlri::nlri> ContextStorer;

    typedef mpl::list<BgpPrefixLen, BgpPrefixAddress> Sequence;
};

class BgpErmVpnNlriType : public ProtoElement<BgpErmVpnNlriType> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpProtoPrefix, uint8_t,
            &BgpProtoPrefix::type> Setter;
};

class BgpErmVpnNlriLen : public ProtoElement<BgpErmVpnNlriLen> {
public:
    static const int kSize = 1;

    struct ErmVpnPrefixLen {
        static void set(BgpProtoPrefix *obj, int value) {
            obj->prefixlen = value * 8;
        }

        static int get(const BgpProtoPrefix *obj) {
            return obj->prefixlen / 8;
        }
    };


    typedef int SequenceLength;

    typedef ErmVpnPrefixLen Setter;
};

class BgpPathAttributeMpErmVpnNlri : public ProtoSequence<BgpPathAttributeMpErmVpnNlri> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;

    struct OptMatch {
        bool match(const BgpMpNlri *obj) {
            return ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::ErmVpn));
        }
    };

    typedef OptMatch ContextMatch;

    typedef CollectionAccessor<BgpMpNlri, vector<BgpProtoPrefix *>,
            &BgpMpNlri::nlri> ContextStorer;

    typedef mpl::list<BgpErmVpnNlriType, BgpErmVpnNlriLen, BgpPrefixAddress> Sequence;
};

class BgpEvpnNlriType : public ProtoElement<BgpEvpnNlriType> {
public:
    static const int kSize = 1;
    typedef Accessor<BgpProtoPrefix, uint8_t,
            &BgpProtoPrefix::type> Setter;
};

class BgpEvpnNlriLen : public ProtoElement<BgpEvpnNlriLen> {
public:
    static const int kSize = 1;

    struct EvpnPrefixLen {
        static void set(BgpProtoPrefix *obj, int value) {
            obj->prefixlen = value * 8;
        }

        static int get(const BgpProtoPrefix *obj) {
            return obj->prefixlen / 8;
        }
    };


    typedef int SequenceLength;

    typedef EvpnPrefixLen Setter;
};

class BgpPathAttributeMpEvpnNlri : public ProtoSequence<BgpPathAttributeMpEvpnNlri> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;

    struct OptMatch {
        bool match(const BgpMpNlri *obj) {
            return ((obj->afi == BgpAf::L2Vpn) && (obj->safi == BgpAf::EVpn));
        }
    };

    typedef OptMatch ContextMatch;

    typedef CollectionAccessor<BgpMpNlri, vector<BgpProtoPrefix *>,
            &BgpMpNlri::nlri> ContextStorer;

    typedef mpl::list<BgpEvpnNlriType, BgpEvpnNlriLen, BgpPrefixAddress> Sequence;
};

class BgpPathAttributeMpNlriChoice : public ProtoChoice<BgpPathAttributeMpNlriChoice> {
public:
    static const int kSize = 0;
    struct MpChoice {
        static void set(BgpMpNlri *obj, int &value) {
            if ((obj->afi == BgpAf::L2Vpn) && (obj->safi == BgpAf::EVpn)) {
                value = 1;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Unicast)) {
                value = 0;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Vpn)) {
                value = 0;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::RTarget)) {
                value = 2;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::ErmVpn)) {
                value = 3;
            }
        }

        static int get(BgpMpNlri *obj) {
            if ((obj->afi == BgpAf::L2Vpn) && (obj->safi == BgpAf::EVpn)) {
                return 1;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Unicast)) {
                return 0;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::Vpn)) {
                return 0;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::RTarget)) {
                return 2;
            }
            if ((obj->afi == BgpAf::IPv4) && (obj->safi == BgpAf::ErmVpn)) {
                return 3;
            }
            return -1;
        }
    };

    typedef MpChoice Setter;
    typedef mpl::map<
          mpl::pair<mpl::int_<0>, BgpPathAttributeMpNlri>,
          mpl::pair<mpl::int_<1>, BgpPathAttributeMpEvpnNlri>,
          mpl::pair<mpl::int_<2>, BgpPathAttributeMpRTargetNlri>,
          mpl::pair<mpl::int_<3>, BgpPathAttributeMpErmVpnNlri>
    > Choice;
};

class BgpPathAttributeMpReachNlriSequence :
public ProtoSequence<BgpPathAttributeMpReachNlriSequence> {
public:
    struct Offset {
        std::string operator()() {
            return "MpReachUnreachNlri";
        }
    };
    typedef Offset SaveOffset;
    typedef BgpMpNlri ContextType;
    typedef BgpContextSwap<BgpMpNlri> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
                  BgpAttributeValue<2, BgpMpNlri, uint16_t, &BgpMpNlri::afi>,
                  BgpAttributeValue<1, BgpMpNlri, uint8_t, &BgpMpNlri::safi>,
                  BgpPathAttributeMpNlriNextHop,
                  BgpPathAttributeReserved,
                  BgpPathAttributeMpNlriChoice> Sequence;
};

class BgpPathAttributeMpUnreachNlriSequence :
public ProtoSequence<BgpPathAttributeMpUnreachNlriSequence> {
public:
    struct Offset {
        std::string operator()() {
            return "MpReachUnreachNlri";
        }
    };
    typedef Offset SaveOffset;
    typedef BgpMpNlri ContextType;
    typedef BgpContextSwap<BgpMpNlri> ContextSwap;
    typedef mpl::list<BgpPathAttrLength,
                  BgpAttributeValue<2, BgpMpNlri, uint16_t, &BgpMpNlri::afi>,
                  BgpAttributeValue<1, BgpMpNlri, uint8_t, &BgpMpNlri::safi>,
                  BgpPathAttributeMpNlriChoice> Sequence;
};

class BgpPathAttrUnknownValue : public ProtoElement<BgpPathAttrUnknownValue> {
public:
    static const int kSize = -1;
    static bool Verifier(BgpAttrUnknown *obj, const uint8_t *data, size_t size,
                         ParseContext *context) {
        int pre = (obj->flags & BgpAttribute::ExtendedLength) ? 4 : 3;
        if (!(obj->flags & BgpAttribute::Optional)) {
            context->SetError(BgpProto::Notification::UpdateMsgErr,
                BgpProto::Notification::UnrecognizedWellKnownAttrib,
                "BgpAttrUnknown", data - pre, context->size() + pre);
            return false;
        }
        return true;
    }
    typedef VectorAccessor<BgpAttrUnknown, uint8_t, &BgpAttrUnknown::value> Setter;
};

class BgpPathAttributeUnknown : public ProtoSequence<BgpPathAttributeUnknown> {
public:
    typedef BgpAttrUnknown ContextType;
    typedef BgpContextSwap<BgpAttrUnknown> ContextSwap;
    typedef mpl::list<BgpPathAttrLength, BgpPathAttrUnknownValue> Sequence;
};

class BgpPathAttribute : public ProtoChoice<BgpPathAttribute> {
public:
    static const int kSize = 1;

    typedef Accessor<BgpAttribute, uint8_t, &BgpAttribute::code> Setter;
    typedef mpl::map<
          mpl::pair<mpl::int_<BgpAttribute::Origin>,
                    BgpAttrTemplate<BgpAttrOrigin, 1, int,
                                    &BgpAttrOrigin::origin> >,
          mpl::pair<mpl::int_<BgpAttribute::NextHop>,
                    BgpAttrTemplate<BgpAttrNextHop, 4, uint32_t,
                                    &BgpAttrNextHop::nexthop> >,
          mpl::pair<mpl::int_<BgpAttribute::MultiExitDisc>,
                    BgpAttrTemplate<BgpAttrMultiExitDisc, 4, uint32_t,
                                    &BgpAttrMultiExitDisc::med> >,
          mpl::pair<mpl::int_<BgpAttribute::LocalPref>,
                    BgpAttrTemplate<BgpAttrLocalPref, 4, uint32_t,
                                    &BgpAttrLocalPref::local_pref> >,
          mpl::pair<mpl::int_<BgpAttribute::AtomicAggregate>,
                    BgpPathAttributeAtomicAggregate>,
          mpl::pair<mpl::int_<BgpAttribute::Aggregator>,
                    BgpPathAttributeAggregator>,
          mpl::pair<mpl::int_<BgpAttribute::AsPath>, BgpPathAttributeAsPath>,
          mpl::pair<mpl::int_<BgpAttribute::Communities>,
                    BgpPathAttributeCommunities>,
          mpl::pair<mpl::int_<BgpAttribute::MPReachNlri>,
                    BgpPathAttributeMpReachNlriSequence>,
          mpl::pair<mpl::int_<BgpAttribute::MPUnreachNlri>,
                    BgpPathAttributeMpUnreachNlriSequence>,
          mpl::pair<mpl::int_<BgpAttribute::ExtendedCommunities>,
                    BgpPathAttributeExtendedCommunities>,
          mpl::pair<mpl::int_<BgpAttribute::McastEdgeDiscovery>,
                    BgpPathAttributeEdgeDiscovery>,
          mpl::pair<mpl::int_<BgpAttribute::McastEdgeForwarding>,
                    BgpPathAttributeEdgeForwarding>,
          mpl::pair<mpl::int_<-1>, BgpPathAttributeUnknown>
    > Choice;
};

class BgpPathAttributeList : public ProtoSequence<BgpPathAttributeList> {
public:
    static const int kSize = 2;
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    static const int kErrorCode = BgpProto::Notification::UpdateMsgErr;
    static const int kErrorSubcode =
            BgpProto::Notification::MalformedAttributeList;
    struct Offset {
        std::string operator()() {
            return "BgpPathAttribute";
        }
    };
    typedef Offset SaveOffset;
    typedef CollectionAccessor<BgpProto::Update,
                vector<BgpAttribute *>,
                &BgpProto::Update::path_attributes> ContextStorer;
    typedef mpl::list<BgpPathAttributeFlags, BgpPathAttribute> Sequence;
};

class BgpUpdateNlri : public ProtoSequence<BgpUpdateNlri> {
public:
    static const int kMinOccurs = 0;
    static const int kMaxOccurs = -1;
    
    typedef CollectionAccessor<BgpProto::Update,
            vector<BgpProtoPrefix *>,
            &BgpProto::Update::nlri> ContextStorer;
    
    struct OptMatch {
        bool match(const BgpProto::Update *ctx) {
            return !ctx->nlri.empty();
        }
    };
    typedef OptMatch ContextMatch;
    
    typedef mpl::list<BgpPrefixLen, BgpPrefixAddress> Sequence;
};

class BgpUpdateMessage : public ProtoSequence<BgpUpdateMessage> {
public:
    typedef mpl::list<BgpUpdateWithdrawnRoutes, BgpPathAttributeList,
                      BgpUpdateNlri> Sequence;
    typedef BgpProto::Update ContextType;
};

class BgpMsgType : public ProtoChoice<BgpMsgType> {
public:
    static const int kSize = 1;
    static const int kErrorCode = BgpProto::Notification::MsgHdrErr;
    static const int kErrorSubcode = BgpProto::Notification::BadMsgType;
    typedef mpl::map<
        mpl::pair<mpl::int_<BgpProto::OPEN>, BgpOpenMessage>,
        mpl::pair<mpl::int_<BgpProto::NOTIFICATION>, BgpNotificationMessage>,
        mpl::pair<mpl::int_<BgpProto::KEEPALIVE>, BgpKeepaliveMessage>,
        mpl::pair<mpl::int_<BgpProto::UPDATE>, BgpUpdateMessage>
    > Choice;
};

class BgpProtocol : public ProtoSequence<BgpProtocol> {
public:
    typedef mpl::list<BgpMarker, BgpMsgLength, BgpMsgType> Sequence;
};

BgpProto::BgpMessage *BgpProto::Decode(const uint8_t *data, size_t size,
                                       ParseErrorContext *ec) {
    ParseContext context;
    int result = BgpProtocol::Parse(data, size, &context, (void *) NULL);
    if (result < 0) {
        if (ec) {
            *ec = context.error_context();
        }
        return NULL;
    }
    return static_cast<BgpMessage *>(context.release());
}

int BgpProto::Encode(const BgpMessage *msg, uint8_t *data, size_t size,
                     EncodeOffsets *offsets) {
    EncodeContext ctx;
    int result = BgpProtocol::Encode(&ctx, msg, data, size);
    if (offsets) {
        *offsets = ctx.encode_offsets();
    }
    return result;
}

int BgpProto::Encode(const BgpMpNlri *msg, uint8_t *data, size_t size,
                     EncodeOffsets *offsets) {
    EncodeContext ctx;
    int result = 0;
    if ((msg->afi == BgpAf::L2Vpn) && (msg->safi == BgpAf::EVpn)) {
        result = BgpPathAttributeMpEvpnNlri::Encode(&ctx, msg, data, size);
    } else if ((msg->afi == BgpAf::IPv4) && (msg->safi == BgpAf::RTarget)) {
        result = BgpPathAttributeMpRTargetNlri::Encode(&ctx, msg, data, size);
    } else if ((msg->afi == BgpAf::IPv4) && (msg->safi == BgpAf::ErmVpn)) {
        result = BgpPathAttributeMpErmVpnNlri::Encode(&ctx, msg, data, size);
    } else {
        result = BgpPathAttributeMpNlri::Encode(&ctx, msg, data, size);
    }
    if (offsets) {
        *offsets = ctx.encode_offsets();
    }
    return result;
}
