/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_PROTO_H_
#define SRC_BGP_BGP_PROTO_H_

#include <sstream>
#include <string>
#include <vector>

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
        explicit BgpMessage(MessageType type) : type(type) {
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
                Reserved = 0,
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
                EnhancedRouteRefresh = 70,
                LongLivedGracefulRestart = 71,
                RouteRefreshCisco = 128
            };
            static std::string CapabilityToString(int capability) {
                switch (capability) {
                case Reserved:
                    return "Reserved";
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
                case LongLivedGracefulRestart:
                    return "LongLivedGracefulRestart";
                case RouteRefreshCisco:
                    return "RouteRefreshCisco";
                default:
                    break;
                }

                std::ostringstream oss;
                oss << "Unknown(" << capability << ")";
                return oss.str();
            }
            Capability() : code(Reserved) { }
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
        static std::string CodeToString(Code code) {
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
            default:
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << code << ")";
            return oss.str();
        }
        enum MsgHdrSubCode {
            ConnNotSync = 1,
            BadMsgLength = 2,
            BadMsgType = 3,
        };
        static std::string MsgHdrSubcodeToString(MsgHdrSubCode sub_code) {
            switch (sub_code) {
            case ConnNotSync:
                return "Connection Not Synchronized";
            case BadMsgLength:
                return "Bad Message Length";
            case BadMsgType:
                return "Bad Message Type";
            default:
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << sub_code << ")";
            return oss.str();
        }
        enum OpenMsgSubCode {
            UnsupportedVersion = 1,
            BadPeerAS = 2,
            BadBgpId = 3,
            UnsupportedOptionalParam = 4,
            AuthenticationFailure = 5,
            UnacceptableHoldTime = 6,
            UnsupportedCapability = 7
        };
        static std::string OpenMsgSubcodeToString(OpenMsgSubCode sub_code) {
            switch (sub_code) {
            case UnsupportedVersion:
                return "Unsupported Version Number";
            case BadPeerAS:
                return "Bad Peer AS";
            case BadBgpId:
                return "Bad BGP Identifier";
            case UnsupportedOptionalParam:
                return "Unsupported Optional Parameter";
            case AuthenticationFailure:
                return "Authentication Failure";
            case UnacceptableHoldTime:
                return "Unacceptable Hold Time";
            case UnsupportedCapability:
                return "Unsupported Capability";
            default:
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << sub_code << ")";
            return oss.str();
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
        static std::string UpdateMsgSubCodeToString(UpdateMsgSubCode sub_code) {
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
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << sub_code << ")";
            return oss.str();
        }
        enum FsmSubcode {
            UnspecifiedError = 0,
            OpenSentError = 1,
            OpenConfirmError = 2,
            EstablishedError = 3
        };
        static std::string FsmSubcodeToString(FsmSubcode sub_code) {
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
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << sub_code << ")";
            return oss.str();
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

        static std::string CeaseSubcodeToString(CeaseSubCode sub_code) {
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
                break;
            }

            std::ostringstream oss;
            oss << "Unknown(" << sub_code << ")";
            return oss.str();
        }

        Notification();
        int error;
        int subcode;
        std::string data;
        static BgpProto::Notification *Decode(const uint8_t *data, size_t size);
        static int EncodeData(Notification *msg, uint8_t *data, size_t size);
        virtual const std::string ToString() const;
        static const std::string toString(Code code, int subcode);
    };

    struct Keepalive : public BgpMessage {
        Keepalive();
        static BgpProto::Keepalive *Decode(const uint8_t *data, size_t size);
    };

    struct Update : public BgpMessage {
        Update();
        ~Update();
        int Validate(const BgpPeer *, std::string *data);
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
};

#endif  // SRC_BGP_BGP_PROTO_H_
