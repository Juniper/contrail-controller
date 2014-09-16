/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_BGP_PROTO_H__
#define __BGP_BGP_PROTO_H__

#include "base/parse_object.h"
#include "bgp/bgp_attr.h"
#include "bgp/community.h"

struct BgpMpNlri;
class BgpPeer;

class BgpProto {
public:
    enum MessageType {
        OPEN = 1,
        UPDATE = 2,
        NOTIFICATION = 3,
        KEEPALIVE = 4
    };

    enum BgpPeerType {
        IBGP,
        EBGP,
        XMPP,
    };
    struct BgpMessage : public ParseObject {
        BgpMessage(MessageType type) : type(type) {
        }
        MessageType type;
        virtual const std::string ToString() const { return ""; }
    };

    struct OpenMessage : public BgpMessage {
        enum OpenOptParamTypes {
            OPEN_OPT_CAPABILITIES = 2,
        };

        OpenMessage();
        ~OpenMessage();
        int Validate(BgpPeer *peer) const;
        uint32_t as_num;
        int32_t holdtime;
        uint32_t identifier;

        struct Capability : public ParseObject {
            enum CapabilityCode {
                MpExtension = 1,
                RouteRefresh = 2,
                OutboundRouteFiltering = 3,
                MultipleRoutesToADestination = 4,
                ExtendedNextHop = 5,
                GracefulRestart = 64,
                AS4Support = 65,
                Dynamic = 67,
                MultisessionBgp = 68,
                AddPath = 69,
                EnhancedRouteRefresh = 70
            };
            static const char *CapabilityToString(int capability) {
                switch (capability) {
                    case MpExtension:
                        return "MpExtension";
                    case RouteRefresh:
                        return "RouteRefresh";
                    case OutboundRouteFiltering:
                        return "OutboundRouteFiltering";
                    case MultipleRoutesToADestination:
                        return "MultipleRoutesToADestination";
                    case ExtendedNextHop:
                        return "ExtendedNextHop";
                    case GracefulRestart:
                        return "GracefulRestart";
                    case AS4Support:
                        return "AS4Support";
                    case Dynamic:
                        return "Dynamic";
                    case MultisessionBgp:
                        return "MultisessionBgp";
                    case AddPath:
                        return "AddPath";
                    case EnhancedRouteRefresh:
                        return "EnhancedRouteRefresh";
                }
                return "Unknown";
            }
            Capability() {}
            explicit Capability(int code, const uint8_t *src, int size) :
                code(code), capability(src, src + size) {}
            int code;
            std::vector<uint8_t> capability;
        };
        struct OptParam : public ParseObject {
            ~OptParam() {
                STLDeleteValues(&capabilities);
            }
            std::vector<Capability *> capabilities;
        };
        std::vector<OptParam *> opt_params;
        static BgpProto::OpenMessage *Decode(const uint8_t *data, size_t size);
        static int EncodeData(OpenMessage *msg, uint8_t *data, size_t size);
        virtual const std::string ToString() const;

    private:
        int ValidateCapabilities(BgpPeer *peer) const;
        int EncodeCapabilities(OpenMessage *msg, uint8_t *data, size_t size);
    };

    struct Notification : public BgpMessage {
        enum Code {
            MsgHdrErr = 1,
            OpenMsgErr = 2,
            UpdateMsgErr = 3,
            HoldTimerExp = 4,
            FSMErr = 5,
            Cease = 6
        };
        static const char *CodeToString(
                BgpProto::Notification::Code code) {
            switch (code) {
            case MsgHdrErr:
                return "Message Header Error";
            case OpenMsgErr:
                return "OPEN Message Error";
            case UpdateMsgErr:
                return "UPDATE Message Error";
            case HoldTimerExp:
                return "Hold Timer Expired";
            case FSMErr:
                return "Finite State Machine Error";
            case Cease:
                return "Cease";
            }
            return "Unknown";
        }
        enum MsgHdrSubCode {
            ConnNotSync = 1,
            BadMsgLength = 2,
            BadMsgType = 3,
        };
        static const char *MsgHdrSubcodeToString(
                BgpProto::Notification::MsgHdrSubCode sub_code) {
            switch (sub_code) {
            case ConnNotSync:
                return "Connection Not Synchronized";
            case BadMsgLength:
                return "Bad Message Length";
            case BadMsgType:
                return "Bad Message Type";
            default:
                return "Unknown";
            }
        }
        enum OpenMsgSubCode {
            UnsupportedVersion = 1,
            BadPeerAS = 2,
            BadBgpId = 3,
            UnsupportedOptionalParam = 4,
            UnacceptableHoldTime = 6,
            UnsupportedCapability = 7
        };
        static const char *OpenMsgSubcodeToString(
                BgpProto::Notification::OpenMsgSubCode sub_code) {
            switch (sub_code) {
            case UnsupportedVersion:
                return "Unsupported Version Number";
            case BadPeerAS:
                return "Bad Peer AS";
            case BadBgpId:
                return "Bad BGP Identifier";
            case UnsupportedOptionalParam:
                return "Unsupported Optional Parameter";
            case UnacceptableHoldTime:
                return "Unacceptable Hold Time";
            case UnsupportedCapability:
                return "Unsupported Capability";
            default:
                return "Unknown";
            }
        }
        enum UpdateMsgSubCode {
            MalformedAttributeList = 1,
            UnrecognizedWellKnownAttrib = 2,
            MissingWellKnownAttrib = 3,
            AttribFlagsError = 4,
            AttribLengthError = 5,
            InvalidOrigin = 6,
            InvalidNH = 8,
            OptionalAttribError = 9,
            InvalidNetworkField = 10,
            MalformedASPath = 11
        };
        static const char *UpdateMsgSubCodeToString(
                BgpProto::Notification::UpdateMsgSubCode sub_code) {
            switch (sub_code) {
            case MalformedAttributeList:
                return "Malformed Attribute List";
            case UnrecognizedWellKnownAttrib:
                return "Unrecognized Well-known Attribute";
            case MissingWellKnownAttrib:
                return "Missing Well-known Attribute";
            case AttribFlagsError:
                return "Attribute Flags Error";
            case AttribLengthError:
                return "Attribute Length Error";
            case InvalidOrigin:
                return "Invalid ORIGIN Attribute";
            case InvalidNH:
                return "Invalid NEXT_HOP Attribute";
            case OptionalAttribError:
                return "Optional Attribute Error";
            case InvalidNetworkField:
                return "Invalid Network Field";
            case MalformedASPath:
                return "Malformed AS_PATH";
            default:
                return "Unknown";
            }
        }
        enum FsmSubcode {
            UnspecifiedError = 0,
            OpenSentError = 1,
            OpenConfirmError = 2,
            EstablishedError = 3
        };
        static const char *FsmSubcodeToString(
                BgpProto::Notification::FsmSubcode sub_code) {
            switch (sub_code) {
            case UnspecifiedError:
                return "Unspecified Error";
            case OpenSentError:
                return "Receive Unexpected Message in OpenSent State";
            case OpenConfirmError:
                return "Receive Unexpected Message in OpenConfirm State";
            case EstablishedError:
                return "Receive Unexpected Message in Established State";
            default:
                return "Unknown";
            }
        }
        enum CeaseSubCode {
            Unknown = 0,
            MaxPrefixes = 1,
            AdminShutdown = 2,
            PeerDeconfigured = 3,
            AdminReset = 4,
            ConnectionRejected = 5,
            OtherConfigChange = 6,
            ConnectionCollision = 7,
            OutOfResources = 8
        };

        static const char *CeaseSubcodeToString(
                        BgpProto::Notification::CeaseSubCode sub_code) {
            switch (sub_code) {
            case Unknown:
                return "Unspecified Error";

            case MaxPrefixes:
                return "Received Maximum prefixes from peer";

            case AdminShutdown:
                return "Administrator has disabled the peer";

            case PeerDeconfigured:
                return "Administrator has unconfigured the peer";

            case AdminReset:
                return "Administrator reset the peer";

            case ConnectionRejected:
                return "Connection is rejected by the peer";

            case OtherConfigChange:
                return "Peer configuration has changed";

            case ConnectionCollision:
                return "Connection collision";

            case OutOfResources:
                return "Unable handle peer due to resource limit";

            default:
                return "Unknown";
            }
        }

        Notification();
        int error;
        int subcode;
        std::string data;
        static BgpProto::Notification *Decode(const uint8_t *data, size_t size);
    	static int EncodeData(Notification *msg, uint8_t *data, size_t size);
        virtual const std::string ToString() const;
        const static std::string toString(BgpProto::Notification::Code code,
                                          int subcode);
    };

    struct Keepalive : public BgpMessage {
        Keepalive();
        static BgpProto::Keepalive *Decode(const uint8_t *data, size_t size);
    };

    struct Update : public BgpMessage {
    	Update();
    	~Update();
        int Validate(const BgpPeer *, std::string &data);
        int CompareTo(const Update &rhs) const;
        static BgpProto::Update *Decode(const uint8_t *data, size_t size);

        std::vector <BgpProtoPrefix *> withdrawn_routes;
        std::vector <BgpAttribute *> path_attributes;
        std::vector <BgpProtoPrefix *> nlri;
        static int EncodeData(Update *msg, uint8_t *data, size_t size);
    };

    static const int kMinMessageSize = 19;
    static const int kMaxMessageSize = 4096;

    static BgpMessage *Decode(const uint8_t *data, size_t size,
                              ParseErrorContext *ec = NULL);

    static int Encode(const BgpMessage *msg, uint8_t *data, size_t size,
                      EncodeOffsets *offsets = NULL);
    static int Encode(const BgpMpNlri *msg, uint8_t *data, size_t size,
                      EncodeOffsets *offsets = NULL);

private:    
};

#endif
