/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include "service_instance.h"

#include "ifmap/ifmap_node.h"
#include "schema/vnc_cfg_types.h"

#include "oper/ifmap_dependency_manager.h"
#include "oper/operdb_init.h"
#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>
#include "oper/instance_manager.h"

using boost::uuids::uuid;

/*
 * ServiceInstanceTable create requests contain the IFMapNode that this
 * entry corresponds to.
 */
class ServiceInstanceCreate : public AgentData {
  public:
    ServiceInstanceCreate(IFMapNode *node) :
            node_(node) {
    }
    IFMapNode *node() { return node_; }

  private:
    IFMapNode *node_;
};

class ServiceInstanceTypesMapping {
public:
    static const std::string kOtherType;
    static int StrServiceTypeToInt(const std::string &type);
    static const std::string &IntServiceTypeToStr(
        const ServiceInstance::ServiceType &type);
    static int StrVirtualizationTypeToInt(const std::string &type);
    static const std::string &IntVirtualizationTypeToStr(
        const ServiceInstance::VirtualizationType &type);
    static int StrVRouterInstanceTypeToInt(const std::string &type);
    static const std::string &IntVRouterInstanceTypeToStr(
        const ServiceInstance::VRouterInstanceType &type);

private:
    typedef std::map<std::string, int> StrTypeToIntMap;
    typedef std::pair<std::string, int> StrTypeToIntPair;
    static StrTypeToIntMap service_type_map_;
    static StrTypeToIntMap virtualization_type_map_;
    static StrTypeToIntMap vrouter_instance_type_map_;

    static StrTypeToIntMap InitServiceTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("source-nat", ServiceInstance::SourceNAT));
        types.insert(StrTypeToIntPair("loadbalancer", ServiceInstance::LoadBalancer));

        return types;
    };

    static StrTypeToIntMap InitVirtualizationTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("virtual-machine", ServiceInstance::VirtualMachine));
        types.insert(StrTypeToIntPair("network-namespace", ServiceInstance::NetworkNamespace));
        types.insert(StrTypeToIntPair("vrouter-instance", ServiceInstance::VRouterInstance));

        return types;
    };

    static StrTypeToIntMap InitVRouterInstanceTypeMap() {
        StrTypeToIntMap types;
        types.insert(StrTypeToIntPair("libvirt-qemu", ServiceInstance::KVM));
        types.insert(StrTypeToIntPair("docker", ServiceInstance::Docker));
        return types;
    };
};

static uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static bool IsNodeType(IFMapNode *node, const char *node_typename) {
    return (strcmp(node->table()->Typename(), node_typename) == 0);
}

/*
 * Walks through the graph starting from the service instance in order to
 * find the Virtual Machine associated. Set the vm_id of the ServiceInstanceData
 * object and return the VM node.
 */
static IFMapNode *FindAndSetVirtualMachine(
    DBGraph *graph, IFMapNode *si_node,
    ServiceInstance::Properties *properties) {

    for (DBGraphVertex::adjacency_iterator iter = si_node->begin(graph);
         iter != si_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "virtual-machine")) {
            autogen::VirtualMachine *vm =
                    static_cast<autogen::VirtualMachine *>(adj->GetObject());
            properties->instance_id = IdPermsGetUuid(vm->id_perms());
            return adj;
        }
    }
    return NULL;
}

static IFMapNode *FindNetworkIpam(DBGraph *graph, IFMapNode *vn_ipam_node) {
    for (DBGraphVertex::adjacency_iterator iter = vn_ipam_node->begin(graph);
             iter != vn_ipam_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        if (IsNodeType(adj, "network-ipam")) {
            return adj;
        }
    }
    return NULL;
}

static IFMapNode *FindNetwork(DBGraph *graph, IFMapNode *vmi_node) {
    /*
     * Lookup for VirtualNetwork nodes
     */
    for (DBGraphVertex::adjacency_iterator iter = vmi_node->begin(graph);
         iter != vmi_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "virtual-network")) {
            return adj;
        }
    }
    return NULL;
}


static std::string FindInterfaceIp(DBGraph *graph, IFMapNode *vmi_node) {
    for (DBGraphVertex::adjacency_iterator iter = vmi_node->begin(graph);
             iter != vmi_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "instance-ip")) {
            autogen::InstanceIp *ip =
                    static_cast<autogen::InstanceIp *>(adj->GetObject());
            return ip->address();
        }
    }
    return std::string();
}

static bool SubNetContainsIpv4(const autogen::IpamSubnetType &subnet,
            const std::string &ip) {
    typedef boost::asio::ip::address_v4 Ipv4Address;
    std::string prefix = subnet.subnet.ip_prefix;
    int prefix_len = subnet.subnet.ip_prefix_len;

    boost::system::error_code ec;
    Ipv4Address ipv4 = Ipv4Address::from_string(ip, ec);
    Ipv4Address ipv4_prefix = Ipv4Address::from_string(prefix, ec);
    unsigned long mask = (0xFFFFFFFF << (32 - prefix_len)) & 0xFFFFFFFF;

    if ((ipv4.to_ulong() & mask) == (ipv4_prefix.to_ulong() & mask)) {
        return true;
    }
    return false;
}

static void FindAndSetInterfaces(
    DBGraph *graph, IFMapNode *vm_node,
    autogen::ServiceInstance *svc_instance,
    ServiceInstance::Properties *properties) {

    /*
     * The outside virtual-network is always specified (by the
     * process that creates the service-instance).
     * The inside virtual-network is optional for loadbalancer.
     * For VRouter instance there can be up to 3 interfaces.
     * TODO: support more than 3 interfaces for VRouter instances.
     * Lookup for VMI nodes
     */
    properties->interface_count = 0;
    for (DBGraphVertex::adjacency_iterator iter = vm_node->begin(graph);
         iter != vm_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (!IsNodeType(adj, "virtual-machine-interface")) {
            continue;
        }
        autogen::VirtualMachineInterface *vmi =
                static_cast<autogen::VirtualMachineInterface *>(
                    adj->GetObject());

        const autogen::VirtualMachineInterfacePropertiesType &vmi_props =
                vmi->properties();

        IFMapNode *vn_node = FindNetwork(graph, adj);
        if (vn_node == NULL) {
            continue;
        }

        properties->interface_count++;
        if(vmi_props.service_interface_type == "left") {
            properties->vmi_inside = IdPermsGetUuid(vmi->id_perms());
            if  (vmi->mac_addresses().size())
                properties->mac_addr_inside = vmi->mac_addresses().at(0);
            properties->ip_addr_inside = FindInterfaceIp(graph, adj);
        }
        else if(vmi_props.service_interface_type == "right") {
            properties->vmi_outside = IdPermsGetUuid(vmi->id_perms());
            if  (vmi->mac_addresses().size())
                properties->mac_addr_outside = vmi->mac_addresses().at(0);
            properties->ip_addr_outside = FindInterfaceIp(graph, adj);
        }
        else if(vmi_props.service_interface_type == "management") {
            properties->vmi_management = IdPermsGetUuid(vmi->id_perms());
            if  (vmi->mac_addresses().size())
                properties->mac_addr_management = vmi->mac_addresses().at(0);
            properties->ip_addr_management = FindInterfaceIp(graph, adj);
        }

        for (DBGraphVertex::adjacency_iterator iter = vn_node->begin(graph);
             iter != vn_node->end(graph); ++iter) {
            IFMapNode *vn_ipam_node =
                static_cast<IFMapNode *>(iter.operator->());
            if (!IsNodeType(vn_ipam_node, "virtual-network-network-ipam")) {
                continue;
            }
            autogen::VirtualNetworkNetworkIpam *ipam =
                static_cast<autogen::VirtualNetworkNetworkIpam *>
                    (vn_ipam_node->GetObject());
            IFMapNode *ipam_node = FindNetworkIpam(graph, vn_ipam_node);
            if (ipam_node == NULL) {
                continue;
            }
            autogen::NetworkIpam *network_ipam =
                static_cast<autogen::NetworkIpam *>(ipam_node->GetObject());
            const std::string subnet_method =
                boost::to_lower_copy(network_ipam->ipam_subnet_method());
            const std::vector<autogen::IpamSubnetType> &subnets =
                (subnet_method == "flat-subnet") ?
                    network_ipam->ipam_subnets() : ipam->data().ipam_subnets;
            for (unsigned int i = 0; i < subnets.size(); ++i) {
                int prefix_len = subnets[i].subnet.ip_prefix_len;
                int service_type = properties->service_type;
                if (vmi_props.service_interface_type == "left") {
                    std::string &ip_addr = properties->ip_addr_inside;
                    if (SubNetContainsIpv4(subnets[i], ip_addr)) {
                        properties->ip_prefix_len_inside = prefix_len;
                        if (service_type == ServiceInstance::SourceNAT)
                            properties->gw_ip = subnets[i].default_gateway;
                    }
                } else if (vmi_props.service_interface_type == "right") {
                    std::string &ip_addr = properties->ip_addr_outside;
                    if (SubNetContainsIpv4(subnets[i], ip_addr)) {
                        properties->ip_prefix_len_outside = prefix_len;
                        if (service_type == ServiceInstance::LoadBalancer)
                            properties->gw_ip = subnets[i].default_gateway;
                    }
                } else if (vmi_props.service_interface_type == "management") {
                    std::string &ip_addr = properties->ip_addr_management;
                    if (SubNetContainsIpv4(subnets[i], ip_addr))
                        properties->ip_prefix_len_management = prefix_len;
                }
            }
        }
    }
}

/*
 * Walks through the graph in order to get the template associated to the
 * Service Instance Node and set the types in the ServiceInstanceData object.
 */
static void FindAndSetTypes(DBGraph *graph, IFMapNode *si_node,
                            ServiceInstance::Properties *properties) {
    IFMapNode *st_node = NULL;

    for (DBGraphVertex::adjacency_iterator iter = si_node->begin(graph);
         iter != si_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "service-template")) {
            st_node = adj;
            break;
        }
    }

    if (st_node == NULL) {
        return;
    }

    autogen::ServiceTemplate *svc_template =
            static_cast<autogen::ServiceTemplate *>(st_node->GetObject());
    autogen::ServiceTemplateType svc_template_props =
            svc_template->properties();

    properties->service_type =
            ServiceInstanceTypesMapping::StrServiceTypeToInt(
                svc_template_props.service_type);

    properties->virtualization_type =
            ServiceInstanceTypesMapping::StrVirtualizationTypeToInt(
                svc_template_props.service_virtualization_type);

    properties->vrouter_instance_type =
            ServiceInstanceTypesMapping::StrVRouterInstanceTypeToInt(
                svc_template_props.vrouter_instance_type);

    properties->image_name = svc_template_props.image_name;
    properties->instance_data = svc_template_props.instance_data;
}

static void FindAndSetLoadbalancer(ServiceInstance::Properties *properties) {
    const std::vector<autogen::KeyValuePair> &kvps = properties->instance_kvps;
    std::vector<autogen::KeyValuePair>::const_iterator iter;
    for (iter = kvps.begin(); iter != kvps.end(); ++iter) {
        autogen::KeyValuePair kvp = *iter;
        if (kvp.key == "lb_uuid") {
            properties->loadbalancer_id = kvp.value;
            break;
        }
    }
}

/*
 * ServiceInstance Properties
 */
void ServiceInstance::Properties::Clear() {
    service_type = 0;
    virtualization_type = 0;
    vrouter_instance_type = 0;

    instance_id = boost::uuids::nil_uuid();

    vmi_inside = boost::uuids::nil_uuid();
    vmi_outside = boost::uuids::nil_uuid();
    vmi_management = boost::uuids::nil_uuid();

    mac_addr_inside.clear();
    mac_addr_outside.clear();
    mac_addr_management.clear();

    ip_addr_inside.clear();
    ip_addr_outside.clear();
    ip_addr_management.clear();

    gw_ip.clear();
    image_name.clear();

    ip_prefix_len_inside = -1;
    ip_prefix_len_outside = -1;
    ip_prefix_len_management = -1;

    interface_count = 0;

    instance_data.clear();
    std::vector<autogen::KeyValuePair>::const_iterator iter;
    for (iter = instance_kvps.begin(); iter != instance_kvps.end(); ++iter) {
        autogen::KeyValuePair kvp = *iter;
        kvp.Clear();
    }

    loadbalancer_id.clear();
}

static int compare_kvps(const std::vector<autogen::KeyValuePair> &lhs,
        const std::vector<autogen::KeyValuePair> &rhs) {
    int ret = 0;
    int match;
    int remining_rhs_items;
    std::vector<autogen::KeyValuePair>::const_iterator iter1;
    std::vector<autogen::KeyValuePair>::const_iterator iter2;

    iter1 = lhs.begin();
    iter2 = rhs.begin();
    match = 0;
    remining_rhs_items = rhs.end() - rhs.begin();
    while (iter1 != lhs.end()) {
        while (iter2 != rhs.end()) {
            if (iter1->key.compare(iter2->key) == 0) {
                if ((ret = iter1->value.compare(iter2->value)) != 0) {
                    return ret;
                }
                remining_rhs_items--;
                match = 1;
                break;
            }
            iter2++;
        }
        if (match == 0) {
            return 1;
        }
        match = 0;
        iter2 = rhs.begin();
        iter1++;
    }

    if (remining_rhs_items)
        return -1;

    return 0;
}

template <typename Type>
static int compare(const Type &lhs, const Type &rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

int ServiceInstance::Properties::CompareTo(const Properties &rhs) const {
    int cmp = 0;
    cmp = compare(service_type, rhs.service_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vrouter_instance_type, rhs.vrouter_instance_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(virtualization_type, rhs.virtualization_type);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(instance_id, rhs.instance_id);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_inside, rhs.vmi_inside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(vmi_outside, rhs.vmi_outside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(ip_addr_inside, rhs.ip_addr_inside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(ip_addr_outside, rhs.ip_addr_outside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(ip_prefix_len_inside, rhs.ip_prefix_len_inside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(ip_prefix_len_outside, rhs.ip_prefix_len_outside);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(interface_count, rhs.interface_count);
    if (cmp != 0) {
        return cmp;
    }

    cmp = compare(gw_ip, rhs.gw_ip);
    if (cmp != 0) {
        return cmp;
    }
    cmp = compare(image_name, rhs.image_name);
    if (cmp != 0) {
        return cmp;
    }

    cmp = compare(instance_data, rhs.instance_data);
    if (cmp != 0) {
        return cmp;
    }

    cmp = compare_kvps(instance_kvps, rhs.instance_kvps);
    if (cmp != 0) {
        return cmp;
    }

    cmp = compare(loadbalancer_id, rhs.loadbalancer_id);
    if (cmp != 0) {
            return cmp;
    }
    return cmp;
}

void InstanceKvpsDiffString(const std::vector<autogen::KeyValuePair> &lhs,
        const std::vector<autogen::KeyValuePair> &rhs,
        std::stringstream *ss) {
    int ret = 0;
    int match;
    int remining_rhs_items;
    std::vector<autogen::KeyValuePair>::const_iterator iter1;
    std::vector<autogen::KeyValuePair>::const_iterator iter2;

    iter1 = lhs.begin();
    iter2 = rhs.begin();
    match = 0;
    remining_rhs_items = rhs.size();
    while (iter1 != lhs.end()) {
        while (iter2 != rhs.end()) {
            if (iter1->key.compare(iter2->key) == 0) {
                remining_rhs_items--;
                match = 1;
                if ((ret = iter1->value.compare(iter2->value)) != 0) {
                    *ss << iter1->key << ": -" << iter1->value;
                    *ss << " +" << iter2->value;
                    break;
                }
            }
            iter2++;
        }
        if (match == 0) {
            *ss << " -" << iter1->key << ": " << iter1->value;
        }
        match = 0;
        iter2 = rhs.begin();
        iter1++;
    }

    if (remining_rhs_items == 0)
        return;

    iter1 = rhs.begin();
    iter2 = lhs.begin();
    match = 0;
    while (iter1 != rhs.end()) {
        while (iter2 != lhs.end()) {
            if (iter1->key.compare(iter2->key) == 0) {
                match = 1;
                break;
            }
            iter2++;
        }
        if (match == 0) {
            *ss << " +" << iter1->key << ": " << iter1->value;
        }
        match = 0;
        iter2 = lhs.begin();
        iter1++;
    }
}

std::string ServiceInstance::Properties::DiffString(
    const Properties &rhs) const {
    std::stringstream ss;

    if (compare(service_type, rhs.service_type)) {
        ss << " type: -" << service_type << " +" << rhs.service_type;
    }
    if (compare(virtualization_type, rhs.virtualization_type)) {
        ss << " virtualization: -" << virtualization_type
           << " +" << rhs.virtualization_type;
    }
    if (compare(vrouter_instance_type, rhs.vrouter_instance_type)) {
        ss << " vrouter-instance-type: -" << vrouter_instance_type
           << " +" << rhs.vrouter_instance_type;
    }
    if (compare(instance_id, rhs.instance_id)) {
        ss << " id: -" << instance_id << " +" << rhs.instance_id;
    }
    if (compare(vmi_inside, rhs.vmi_inside)) {
        ss << " vmi-inside: -" << vmi_inside << " +" << rhs.vmi_inside;
    }
    if (compare(vmi_outside, rhs.vmi_outside)) {
        ss << " vmi-outside: -" << vmi_outside << " +" << rhs.vmi_outside;
    }
    if (compare(ip_addr_inside, rhs.ip_addr_inside)) {
        ss << " ip-inside: -" << ip_addr_inside
           << " +" << rhs.ip_addr_inside;
    }
    if (compare(ip_addr_outside, rhs.ip_addr_outside)) {
        ss << " ip-outside: -" << ip_addr_outside
           << " +" << rhs.ip_addr_outside;
    }
    if (compare(ip_prefix_len_inside, rhs.ip_prefix_len_inside)) {
        ss << " pfx-inside: -" << ip_prefix_len_inside
           << " +" << rhs.ip_prefix_len_inside;
    }
    if (compare(ip_prefix_len_outside, rhs.ip_prefix_len_outside)) {
        ss << " pfx-outside: -" << ip_prefix_len_outside
           << " +" << rhs.ip_prefix_len_outside;
    }

    if (compare(loadbalancer_id, rhs.loadbalancer_id)) {
        ss << " loadbalancer_id: -" << loadbalancer_id << " +"
            << rhs.loadbalancer_id;
    }

    if (compare(gw_ip, rhs.gw_ip)) {
        ss << " gw_ip: -" << gw_ip << " +" << rhs.gw_ip;
    }
    if (compare(image_name, rhs.image_name)) {
        ss << " image: -" << image_name << " +" << rhs.image_name;
    }
    if (compare(instance_data, rhs.instance_data)) {
        ss << " image: -" << instance_data << " +" << rhs.instance_data;
    }
    if (compare_kvps(instance_kvps, rhs.instance_kvps)) {
        InstanceKvpsDiffString(instance_kvps, rhs.instance_kvps, &ss);
    }
    return ss.str();
}

bool ServiceInstance::Properties::Usable() const {
    if (instance_id.is_nil()) {
        return false;
    }

    if (virtualization_type == ServiceInstance::VRouterInstance) {
        //TODO: investigate for docker
        return true;
    }

    bool common = (!vmi_outside.is_nil() &&
                   !ip_addr_outside.empty() &&
                   (ip_prefix_len_outside >= 0));
    if (!common) {
        return false;
    }

    if (service_type == SourceNAT || interface_count == 2) {
        bool outside = (!vmi_inside.is_nil() &&
                       !ip_addr_inside.empty() &&
                       (ip_prefix_len_inside >= 0));
        if (!outside) {
            return false;
        }
    }

    if (gw_ip.empty())
        return false;

    if (service_type == LoadBalancer) {
        if (loadbalancer_id.empty())
            return false;
    }

    return true;
}

const std::string &ServiceInstance::Properties::ServiceTypeString() const {
    return ServiceInstanceTypesMapping::IntServiceTypeToStr(
        static_cast<ServiceType>(service_type));
}

/*
 * ServiceInstance class
 */
ServiceInstance::ServiceInstance() {
    properties_.Clear();
}

bool ServiceInstance::IsLess(const DBEntry &rhs) const {
    const ServiceInstance &si = static_cast<const ServiceInstance &>(rhs);
    return uuid_ < si.uuid_;
}

std::string ServiceInstance::ToString() const {
    return UuidToString(uuid_);
}

void ServiceInstance::SetKey(const DBRequestKey *key) {
    const ServiceInstanceKey *si_key =
            static_cast<const ServiceInstanceKey *>(key);
    uuid_ = si_key->instance_id();
}

DBEntryBase::KeyPtr ServiceInstance::GetDBRequestKey() const {
    ServiceInstanceKey *key = new ServiceInstanceKey(uuid_);
    return KeyPtr(key);
}

bool ServiceInstance::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    ServiceInstanceResp *resp = static_cast<ServiceInstanceResp *> (sresp);

    std::string str_uuid = UuidToString(uuid_);
    if (! name.empty() && str_uuid != name) {
        return false;
    }

    ServiceInstanceSandeshData data;

    data.set_uuid(str_uuid);
    data.set_instance_id(UuidToString(properties_.instance_id));

    data.set_service_type(ServiceInstanceTypesMapping::IntServiceTypeToStr(
                    static_cast<ServiceType>(properties_.service_type)));
    data.set_virtualization_type(
                    ServiceInstanceTypesMapping::IntVirtualizationTypeToStr(
                    static_cast<VirtualizationType>(
                                    properties_.virtualization_type)));

    data.set_vmi_inside(UuidToString(properties_.vmi_inside));
    data.set_vmi_outside(UuidToString(properties_.vmi_outside));

    Agent *agent = Agent::GetInstance();
    DBTableBase *si_table = agent->db()->FindTable("db.service-instance.0");
    assert(si_table);

    InstanceManager *manager = agent->oper_db()->instance_manager();
    assert(manager);

    InstanceState *state = manager->GetState(const_cast<ServiceInstance *>(this));
    if (state != NULL) {
        NamespaceStateSandeshData state_data;

        state_data.set_cmd(state->cmd());
        state_data.set_errors(state->errors());
        state_data.set_pid(state->pid());
        state_data.set_status(state->status());
        state_data.set_status_type(state->status_type());

        data.set_ns_state(state_data);
    }

    std::vector<ServiceInstanceSandeshData> &list =
            const_cast<std::vector<ServiceInstanceSandeshData>&>
            (resp->get_service_instance_list());
    list.push_back(data);

    return true;
}

bool ServiceInstance::IsUsable() const {
    return properties_.Usable();
}

void ServiceInstanceReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentServiceInstanceSandesh(context(),
                                                         get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr ServiceInstanceTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context){
    return AgentSandeshPtr
        (new AgentServiceInstanceSandesh(context, args->GetString("name")));
}

/*
 * ServiceInstanceTable class
 */
ServiceInstanceTable::ServiceInstanceTable(DB *db, const std::string &name)
        : AgentDBTable(db, name),
          graph_(NULL), dependency_manager_(NULL) {
}

std::auto_ptr<DBEntry> ServiceInstanceTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new ServiceInstance());
    entry->SetKey(key);
    return entry;
}

bool ServiceInstanceTable::HandleAddChange(ServiceInstance
        **svc_instancep, const DBRequest *request) {

    ServiceInstanceCreate *data =
            static_cast<ServiceInstanceCreate *>(request->data.get());
    if (!data)
        return false;

    IFMapNode *node = data->node();
    ServiceInstance *svc_instance = *svc_instancep;

    assert(graph_);
    ServiceInstance::Properties properties;
    properties.Clear();
    CalculateProperties(graph_, node, &properties);

    assert(dependency_manager_);

    if (!svc_instance) {
        svc_instance = new ServiceInstance();
        *svc_instancep = svc_instance;
    }

    if (!svc_instance->ifmap_node()) {
        svc_instance->SetKey(request->key.get());
        svc_instance->SetIFMapNodeState
                    (dependency_manager_->SetState(node));
        dependency_manager_->SetObject(node, svc_instance);
    }

    if (properties.CompareTo(svc_instance->properties()) == 0)
        return false;

    if (svc_instance->properties().Usable() != properties.Usable()) {
        LOG(DEBUG, "service-instance properties change" <<
                svc_instance->properties().DiffString(properties));
    }

    svc_instance->set_properties(properties);
    return true;
}


DBEntry *ServiceInstanceTable::Add(const DBRequest *request) {
    ServiceInstance *svc_instance = NULL;
    HandleAddChange(&svc_instance, request);
    return svc_instance;
}

bool ServiceInstanceTable::Delete(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance  = static_cast<ServiceInstance *>(entry);
    assert(dependency_manager_);
    if (svc_instance->ifmap_node()) {
        dependency_manager_->SetObject(svc_instance->ifmap_node(), NULL);
        svc_instance->SetIFMapNodeState(NULL);
    }
    return true;
}

bool ServiceInstanceTable::OnChange(DBEntry *entry, const DBRequest *request) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    return HandleAddChange(&svc_instance, request);
}

void ServiceInstanceTable::Initialize(
    DBGraph *graph, IFMapDependencyManager *dependency_manager) {

    graph_ = graph;
    dependency_manager_ = dependency_manager;

    dependency_manager_->Register(
        "service-instance",
        boost::bind(&ServiceInstanceTable::ChangeEventHandler, this, _1, _2));
}

bool ServiceInstanceTable::IFNodeToReq(IFMapNode *node, DBRequest
        &request, const boost::uuids::uuid &id) {

    assert(!id.is_nil());

    request.key.reset(new ServiceInstanceKey(id));
    if ((request.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    request.data.reset(new ServiceInstanceCreate(node));
    return true;
}

void ServiceInstanceTable::CalculateProperties(
    DBGraph *graph, IFMapNode *node, ServiceInstance::Properties *properties) {

    properties->Clear();

    if (node->IsDeleted()) {
        return;
    }

    FindAndSetTypes(graph, node, properties);

    /*
     * The vrouter agent is only interest in the properties of service
     * instances that are implemented as a network-namespace.
     */
    if ((properties->virtualization_type != ServiceInstance::NetworkNamespace) &&
            (properties->virtualization_type != ServiceInstance::VRouterInstance)) {
        return;
    }

    IFMapNode *vm_node = FindAndSetVirtualMachine(graph, node, properties);
    if (vm_node == NULL) {
        return;
    }

    autogen::ServiceInstance *svc_instance =
                 static_cast<autogen::ServiceInstance *>(node->GetObject());
    properties->instance_kvps = svc_instance->bindings();
    FindAndSetInterfaces(graph, vm_node, svc_instance, properties);

    if (properties->service_type == ServiceInstance::LoadBalancer) {
        FindAndSetLoadbalancer(properties);
    }
}

bool ServiceInstanceTable::IFNodeToUuid(IFMapNode *node, uuid &idperms_uuid) {
    autogen::ServiceInstance *svc_instance =
                static_cast<autogen::ServiceInstance *>(node->GetObject());
    const autogen::IdPermsType &id = svc_instance->id_perms();
    idperms_uuid = IdPermsGetUuid(id);
    return true;
}
void ServiceInstanceTable::ChangeEventHandler(IFMapNode *node, DBEntry *entry) {

    DBRequest req;
    boost::uuids::uuid new_uuid;
    IFNodeToUuid(node, new_uuid);
    IFMapNodeState *state = dependency_manager_->IFMapNodeGet(node);
    boost::uuids::uuid old_uuid = state->uuid();

    if (!node->IsDeleted()) {
        if (entry) {
            if ((old_uuid != new_uuid)) {
                if (old_uuid != boost::uuids::nil_uuid()) {
                    req.oper = DBRequest::DB_ENTRY_DELETE;
                    if (IFNodeToReq(node, req, old_uuid) == true) {
                        assert(req.oper == DBRequest::DB_ENTRY_DELETE);
                        Enqueue(&req);
                    }
                }
            }
        }
        assert(new_uuid != boost::uuids::nil_uuid());
        state->set_uuid(new_uuid);
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    } else {
        if (old_uuid == boost::uuids::nil_uuid()) {
            //Node was never added so no point sending delete
            return;
        }
        req.oper = DBRequest::DB_ENTRY_DELETE;
        new_uuid = old_uuid;
    }

    if (IFNodeToReq(node, req, new_uuid) == true) {
        Enqueue(&req);
    }
}

DBTableBase *ServiceInstanceTable::CreateTable(
    DB *db, const std::string &name) {
    ServiceInstanceTable *table = new ServiceInstanceTable(db, name);
    table->Init();
    return table;
}

/*
 * ServiceInstanceTypeMapping class
 */
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::service_type_map_ = InitServiceTypeMap();
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::virtualization_type_map_ = InitVirtualizationTypeMap();
ServiceInstanceTypesMapping::StrTypeToIntMap
ServiceInstanceTypesMapping::vrouter_instance_type_map_ = InitVRouterInstanceTypeMap();
const std::string ServiceInstanceTypesMapping::kOtherType = "Other";

int ServiceInstanceTypesMapping::StrServiceTypeToInt(const std::string &type) {
    StrTypeToIntMap::const_iterator it = service_type_map_.find(type);
    if (it != service_type_map_.end()) {
        return it->second;
    }
    return 0;
}

int ServiceInstanceTypesMapping::StrVirtualizationTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = virtualization_type_map_.find(type);
    if (it != virtualization_type_map_.end()) {
        return it->second;
    }
    return 0;
}

int ServiceInstanceTypesMapping::StrVRouterInstanceTypeToInt(
        const std::string &type) {
    StrTypeToIntMap::const_iterator it = vrouter_instance_type_map_.find(type);
    if (it != vrouter_instance_type_map_.end()) {
        return it->second;
    }
    return 0;
}

const std::string &ServiceInstanceTypesMapping::IntServiceTypeToStr(
    const ServiceInstance::ServiceType &type) {
    for (StrTypeToIntMap::const_iterator it = service_type_map_.begin();
         it != service_type_map_.end(); ++it) {
        if (it->second == type) {
            return it->first;
        }
    }
    return kOtherType;
}

const std::string &ServiceInstanceTypesMapping::IntVirtualizationTypeToStr(
    const ServiceInstance::VirtualizationType &type) {
    for (StrTypeToIntMap::const_iterator it = virtualization_type_map_.begin();
         it != virtualization_type_map_.end(); ++it) {
        if (it->second == type) {
            return it->first;
        }
    }
    return kOtherType;
}

const std::string &ServiceInstanceTypesMapping::IntVRouterInstanceTypeToStr(
    const ServiceInstance::VRouterInstanceType &type) {
    for (StrTypeToIntMap::const_iterator it = vrouter_instance_type_map_.begin();
         it != virtualization_type_map_.end(); ++it) {
        if (it->second == type) {
            return it->first;
        }
    }
    return kOtherType;
}
