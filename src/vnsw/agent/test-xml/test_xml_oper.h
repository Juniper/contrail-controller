/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_xml_test_xml_oper_h
#define vnsw_agent_test_xml_test_xml_oper_h

#include <test-xml/test_xml.h>
#include <test-xml/test_xml_validate.h>

void AgentUtXmlOperInit(AgentUtXmlTest *test);

class AgentUtXmlGlobalVrouter : public AgentUtXmlConfig {
public:
    AgentUtXmlGlobalVrouter(const std::string &name,
                            const boost::uuids::uuid &uuid,
                            const pugi::xml_node &node,
                            AgentUtXmlTestCase *test_case);
    ~AgentUtXmlGlobalVrouter();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

    std::string &vxlan_mode() { return vxlan_mode_;}
    int flow_export_rate() const { return flow_export_rate_; }

private:
    std::string vxlan_mode_;
    int flow_export_rate_;
};

class AgentUtXmlVn : public AgentUtXmlConfig {
public:
    AgentUtXmlVn(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVn();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

    std::string &vxlan_id() { return vxlan_id_;}
    std::string &network_id() { return network_id_;}
    std::string &flood_unknown_unicast() {
        return flood_unknown_unicast_;
    }

private:
    std::string vxlan_id_;
    std::string network_id_;
    std::string flood_unknown_unicast_;
};

class AgentUtXmlVm : public AgentUtXmlConfig {
public:
    AgentUtXmlVm(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVm();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlVmInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlVmInterface(const std::string &name,
                          const boost::uuids::uuid &uuid,
                          const pugi::xml_node &node,
                          AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVmInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string mac_;
    boost::uuids::uuid vn_uuid_;
    std::string vn_name_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    std::string vrf_;
    std::string ip_;
    bool add_nova_;
    uint16_t vlan_tag_;
    std::vector<uint16_t> fat_flow_port_;
    std::string parent_vmi_;
};

class AgentUtXmlEthInterface : public AgentUtXmlConfig {
public:
    AgentUtXmlEthInterface(const std::string &name,
                           const boost::uuids::uuid &uuid,
                           const pugi::xml_node &node,
                           AgentUtXmlTestCase *test_case);
    ~AgentUtXmlEthInterface();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
};

class AgentUtXmlVrf : public AgentUtXmlConfig {
public:
    AgentUtXmlVrf(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVrf();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string vn_name_;
};

class AgentUtXmlVmiVrf : public AgentUtXmlConfig {
public:
    AgentUtXmlVmiVrf(const std::string &name, const boost::uuids::uuid &uuid,
                     const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlVmiVrf();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
};

class AgentUtXmlAcl : public AgentUtXmlConfig {
public:
    struct Ace {
        Ace() : 
            src_sg_(0), dst_sg_(0), src_vn_(""), dst_vn_(""), src_ip_(""),
            src_ip_plen_(0), dst_ip_(""), dst_ip_plen_(0), proto_(0),
            sport_begin_(-1), sport_end_(-1), dport_begin_(-1), dport_end_(-1) {
        }

        uint16_t src_sg_;
        uint16_t dst_sg_;
        std::string src_vn_;
        std::string dst_vn_;
        std::string src_ip_;
        uint16_t src_ip_plen_;
        std::string dst_ip_;
        uint16_t dst_ip_plen_;
        uint8_t proto_;
        int sport_begin_;
        int sport_end_;
        int dport_begin_;
        int dport_end_;
        std::string action_;
        std::string direction_;
    };
    typedef std::vector<Ace> AceList;

    AgentUtXmlAcl(const std::string &name, const boost::uuids::uuid &uuid,
                  const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlAcl();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    AceList ace_list_;
};

class AgentUtXmlSg : public AgentUtXmlConfig {
public:
    AgentUtXmlSg(const std::string &name, const boost::uuids::uuid &uuid,
                  const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlSg();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string ingress_;
    std::string egress_;
    std::string sg_id_;
};

class AgentUtXmlInstanceIp : public AgentUtXmlConfig {
public:
    AgentUtXmlInstanceIp(const std::string &name, const boost::uuids::uuid &uuid,
                 const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlInstanceIp();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);

private:
    std::string ip_;
    std::string vmi_;
};

/////////////////////////////////////////////////////////////////////////////
//   Nova messages
/////////////////////////////////////////////////////////////////////////////
class AgentUtXmlNova : public AgentUtXmlConfig {
public:
    AgentUtXmlNova(const std::string &name, const boost::uuids::uuid &uuid,
                   const pugi::xml_node &node, AgentUtXmlTestCase *test_case);
    ~AgentUtXmlNova();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    std::string mac_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    boost::uuids::uuid vm_uuid_;
    std::string ip_;
};

class AgentUtXmlGlobalVrouterValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlGlobalVrouterValidate(const std::string &name,
                         const boost::uuids::uuid &id,
                         const pugi::xml_node &node);
    virtual ~AgentUtXmlGlobalVrouterValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    int flow_export_rate_;
};

class AgentUtXmlVnValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVnValidate(const std::string &name,
                         const boost::uuids::uuid &id,
                         const pugi::xml_node &node);
    virtual ~AgentUtXmlVnValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    uint16_t vxlan_id_;
    bool check_vxlan_id_ref_;
    uint16_t vxlan_id_ref_;
};

class AgentUtXmlVmValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVmValidate(const std::string &name,
                         const boost::uuids::uuid &id,
                         const pugi::xml_node &node);
    virtual ~AgentUtXmlVmValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlVxlanValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVxlanValidate(const std::string &name,
                            const boost::uuids::uuid &id,
                            const pugi::xml_node &node);
    virtual ~AgentUtXmlVxlanValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    uint16_t vxlan_id_;
    std::string vrf_;
    std::string flood_unknown_unicast_;
};

class AgentUtXmlVmInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVmInterfaceValidate(const std::string &name,
                                  const boost::uuids::uuid &id,
                                  const pugi::xml_node &node);
    virtual ~AgentUtXmlVmInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
    std::string active_;
    std::string device_type_;
    std::string vmi_type_;
    uint16_t vlan_tag_;
    std::vector<uint16_t> fat_flow_port_;

    boost::uuids::uuid vn_uuid_;
};

class AgentUtXmlEthInterfaceValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlEthInterfaceValidate(const std::string &name,
                                   const boost::uuids::uuid &id,
                                   const pugi::xml_node &node);
    virtual ~AgentUtXmlEthInterfaceValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    const boost::uuids::uuid id_;
};

class AgentUtXmlVrfValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlVrfValidate(const std::string &name,
                          const pugi::xml_node &node);
    virtual ~AgentUtXmlVrfValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    std::string vn_name_;
};

class AgentUtXmlAclValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlAclValidate(const std::string &name,
                          const pugi::xml_node &node);
    virtual ~AgentUtXmlAclValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
};

class AgentUtXmlFlowValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlFlowValidate(const std::string &name,
                           const pugi::xml_node &node);
    virtual ~AgentUtXmlFlowValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
    virtual uint32_t wait_count() const;
private:
    uint16_t nh_id_;
    std::string sip_;
    std::string dip_;
    std::string proto_;
    uint16_t sport_;
    uint16_t dport_;
    uint16_t proto_id_;
    std::string svn_;
    std::string dvn_;
    std::string action_;
    uint16_t rpf_nh_;
    std::string deleted_;
};

class AgentUtXmlL2Route : public AgentUtXmlNode {
public:
    AgentUtXmlL2Route(const std::string &name, const boost::uuids::uuid &uuid,
                      const pugi::xml_node &node,
                      AgentUtXmlTestCase *test_case);
    ~AgentUtXmlL2Route();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    std::string mac_;
    std::string vrf_;
    std::string vn_name_;
    std::string ip_;
    uint16_t vxlan_id_;
    std::string vn_;
    std::string tunnel_dest_;
    std::string tunnel_type_;
    uint16_t sg_id_;
    uint16_t label_;
};

class AgentUtXmlL2RouteValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlL2RouteValidate(const std::string &name,
                              const pugi::xml_node &node);
    virtual ~AgentUtXmlL2RouteValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    std::string mac_;
    std::string vrf_;
    uint16_t vxlan_id_;
    IpAddress ip_;
    std::string vn_;
    std::string nh_type_;
    std::string tunnel_dest_;
    std::string tunnel_type_;
    uint16_t intf_uuid_;
};

class AgentUtXmlL3Route : public AgentUtXmlNode {
public:
    AgentUtXmlL3Route(const std::string &name, const boost::uuids::uuid &uuid,
                      const pugi::xml_node &node,
                      AgentUtXmlTestCase *test_case);
    ~AgentUtXmlL3Route();

    virtual bool ReadXml();
    virtual bool ToXml(pugi::xml_node *parent);
    virtual std::string NodeType();
    virtual void ToString(std::string *str);
    virtual bool Run();

private:
    std::string src_ip_;
    std::string vrf_;
    std::string vn_name_;
    std::string ip_;
    uint16_t vxlan_id_;
    std::string vn_;
    std::string tunnel_dest_;
    std::string tunnel_type_;
    uint16_t sg_id_;
    uint16_t label_;
    uint16_t plen_;
};


class AgentUtXmlL3RouteValidate : public AgentUtXmlValidationNode {
public:
    AgentUtXmlL3RouteValidate(const std::string &name,
                              const pugi::xml_node &node);
    virtual ~AgentUtXmlL3RouteValidate();

    virtual bool ReadXml();
    virtual bool Validate();
    virtual const std::string ToString();
private:
    std::string src_ip_;
    std::string vrf_;
    uint16_t vxlan_id_;
    IpAddress ip_;
    std::string vn_;
    std::string nh_type_;
    std::string tunnel_dest_;
    std::string tunnel_type_;
    uint16_t intf_uuid_;
};
#endif //vnsw_agent_test_xml_test_xml_oper_h
