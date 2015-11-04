/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_proto.h"
#include "xml/xml_pugi.h"

#include <pugixml/pugixml.hpp>
#include <fstream>
#include <set>

class EventManager;
class XmppChannelConfig;

class IFMapXmppClientMock : public XmppClient {
public:
    static const std::string kDefaultXmppLocalAddr;
    static const std::string kDefaultXmppServerName;
    static const std::string kDefaultXmppServerAddr;
    static const std::string kDefaultXmppServerConfigName;
    static const std::string kDefaultOutputFile;

    typedef std::set<std::string> ObjectSet;

    explicit IFMapXmppClientMock(EventManager *evm, int port,
        const std::string &name,
        const std::string &output_file_path = kDefaultOutputFile,
        const std::string &laddr = kDefaultXmppLocalAddr,
        const std::string &xmpp_srv_name = kDefaultXmppServerName,
        const std::string &xmpp_srv_addr = kDefaultXmppServerAddr,
        const std::string &xmpp_srv_cfg_name = kDefaultXmppServerConfigName);
    ~IFMapXmppClientMock();

    void RegisterWithXmpp();
    void UnRegisterWithXmpp();
    bool IsEstablished();
    static XmppChannelConfig *CreateXmppChannelCfg(const char *saddr,
        const char *laddr, int sport, const std::string &to,
        const std::string &from);
    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg);
    virtual void WriteReadyCb(const boost::system::error_code &ec);
    void SendDocument(const pugi::xml_document &xdoc);
    pugi::xml_node PubSubHeader(pugi::xml_document *xdoc);
    void SendConfigSubscribe();
    void SendVmConfigSubscribe(std::string vm_name);
    void SendVmConfigUnsubscribe(std::string vm_name);

    uint64_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    bool HasMessages() const { return count_ > 0; }
    bool Has2Messages() const { return count_ == 2; }
    bool HasNMessages(uint64_t n) const { return count_ == n; }
    const std::string& name() { return name_; }

    void ProcessNodeTag(pugi::xml_node xnode, ObjectSet *oset);
    void ProcessLinkTag(pugi::xml_node xnode) { return; }

    // This logic works since we have SetObjectsPerMessage as 1. Otherwise, we
    // will need another loop after accessing 'config'.
    void XmlDocWalk(pugi::xml_node xnode, ObjectSet *oset);
    void OutputRecvBufferToFile() { os_ << recv_buffer_; }

    // Compare the contents of the received buffer with master_file_path
    bool OutputFileCompare(std::string master_file_path);
    void PrintSet(ObjectSet &oset);

private:
    uint64_t count_;
    std::ostream os_;
    std::filebuf fb_;
    std::string name_;
    std::string local_addr_;
    std::string xmpp_server_name_;
    std::string xmpp_server_addr_;
    std::string xmpp_server_config_name_;
    std::string recv_buffer_;
};

