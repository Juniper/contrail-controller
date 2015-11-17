/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

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

/*
 * ServiceInstanceTable update requests contain ServiceInstance Properties.
 */
class ServiceInstanceUpdate : public AgentData {
  public:
    typedef ServiceInstance::Properties Properties;
    ServiceInstanceUpdate(Properties &properties) :
            properties_(properties) {
    }
    const Properties &properties() { return properties_; }

  private:
    Properties properties_;
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

static IFMapNode *FindNetworkSubnets(DBGraph *graph, IFMapNode *vn_node) {
    for (DBGraphVertex::adjacency_iterator iter = vn_node->begin(graph);
             iter != vn_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        if (IsNodeType(adj, "virtual-network-network-ipam")) {
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

static IFMapNode *FindServiceTemplateNode(DBGraph *graph, IFMapNode *si_node) {
    for (DBGraphVertex::adjacency_iterator iter = si_node->begin(graph);
         iter != si_node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "service-template")) {
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

static int GetInterfaceInOrder(
        const std::string &network_name,
        const std::string &interface_type,
        const autogen::ServiceInstanceType &service_instance_type,
        const autogen::ServiceTemplateType &service_template_type,
        const std::vector<ServiceInstance::InterfaceData> &interfaces) {
    const std::vector<autogen::ServiceInstanceInterfaceType> *si_interfaces =
            &service_instance_type.interface_list;
    const std::vector<autogen::ServiceTemplateInterfaceType> *tmpl_interfaces =
            &service_template_type.interface_type;
    bool order_interfaces = service_template_type.ordered_interfaces &&
            si_interfaces->size() == si_interfaces->size();

    if (order_interfaces == true) {
        for (unsigned i = 0; i < si_interfaces->size(); ++i) {
            if (!interfaces[i].vmi_uuid.is_nil())
                continue;

            std::string virtual_network = si_interfaces->at(i).virtual_network;
            std::string type = tmpl_interfaces->at(i).service_interface_type;

            if (virtual_network == network_name && type == interface_type) {
                return (int)i;
            }
        }
    }

    for (unsigned i = 0; i < tmpl_interfaces->size(); ++i) {
        std::string type = tmpl_interfaces->at(i).service_interface_type;
        if (type == interface_type && interfaces[i].vmi_uuid.is_nil())
            return (int)i;
    }
    return -1;
}

static void FindAndSetInterfaces(
    DBGraph *graph, IFMapNode *vm_node,
    const autogen::ServiceInstance *svc_instance,
    const autogen::ServiceTemplateType &svc_template_props,
    ServiceInstance::Properties *properties) {

    /*
     * The outside virtual-network is always specified (by the
     * process that creates the service-instance).
     * The inside virtual-network is optional for loadbalancer.
     */
    properties->interfaces.clear();
    properties->interfaces.resize(svc_template_props.interface_type.size());

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
        std::string network_name = vn_node->name();
        int inderface_data_index = GetInterfaceInOrder(
            network_name, vmi_props.service_interface_type,
            svc_instance->properties(), svc_template_props,
            properties->interfaces
        );
        if (inderface_data_index < 0) {
            continue;
        }

        ServiceInstance::InterfaceData *interface_data =
            &properties->interfaces[inderface_data_index];

        if (vmi->mac_addresses().size())
            interface_data->mac_addr = vmi->mac_addresses().at(0);
        interface_data->vmi_uuid = IdPermsGetUuid(vmi->id_perms());
        interface_data->ip_addr = FindInterfaceIp(graph, adj);
        interface_data->ip_prefix_len = -1;
        interface_data->intf_type = vmi_props.service_interface_type;

        IFMapNode *ipam_node = FindNetworkSubnets(graph, vn_node);
        if (ipam_node == NULL) {
            continue;
        }

        autogen::VirtualNetworkNetworkIpam *ipam =
            static_cast<autogen::VirtualNetworkNetworkIpam *> (ipam_node->GetObject());
        const autogen::VnSubnetsType &subnets = ipam->data();

        for (unsigned int i = 0; i < subnets.ipam_subnets.size(); ++i) {
            int prefix_len = subnets.ipam_subnets[i].subnet.ip_prefix_len;
            std::string default_gw = subnets.ipam_subnets[i].default_gateway;

            if (!SubNetContainsIpv4(subnets.ipam_subnets[i],
                                    interface_data->ip_addr)) {
                continue;
            }
            interface_data->ip_prefix_len = prefix_len;
            if (properties->service_type == ServiceInstance::SourceNAT &&
                    interface_data->intf_type == "left")
                properties->gw_ip = default_gw;
            if (properties->service_type == ServiceInstance::LoadBalancer &&
                    interface_data->intf_type == "right")
                properties->gw_ip = default_gw;
        }
    }
}

/*
 * Walks through the graph in order to get the template associated to the
 * Service Instance Node and set the types in the ServiceInstanceData object.
 */
static void FindAndSetTypes(DBGraph *graph, IFMapNode *si_node,
                            ServiceInstance::Properties *properties,
                            autogen::ServiceTemplateType *svc_template_props) {
    properties->service_type =
            ServiceInstanceTypesMapping::StrServiceTypeToInt(
                svc_template_props->service_type);

    properties->virtualization_type =
            ServiceInstanceTypesMapping::StrVirtualizationTypeToInt(
                svc_template_props->service_virtualization_type);

    properties->vrouter_instance_type =
            ServiceInstanceTypesMapping::StrVRouterInstanceTypeToInt(
                svc_template_props->vrouter_instance_type);

    properties->image_name = svc_template_props->image_name;
    properties->instance_data = svc_template_props->instance_data;
}

static void FindAndSetLoadbalancer(DBGraph *graph, IFMapNode *node,
                                   ServiceInstance::Properties *properties) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (IsNodeType(adj, "loadbalancer-pool")) {
            autogen::LoadbalancerPool *pool =
                    static_cast<autogen::LoadbalancerPool *>(adj->GetObject());
            properties->pool_id = IdPermsGetUuid(pool->id_perms());
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

    interfaces.clear();

    gw_ip.clear();
    image_name.clear();
    instance_data.clear();
    
    pool_id = boost::uuids::nil_uuid();
}

const ServiceInstance::InterfaceData* ServiceInstance::Properties::GetIntfByType(
        const std::string &type) const {
    const InterfaceData *intf_data = NULL;

    for (unsigned i = 0; i < interfaces.size(); ++i) {
        intf_data = &interfaces[i];
        if (intf_data->intf_type == type)
            return intf_data;
    }
    return NULL;
}

const ServiceInstance::InterfaceData* ServiceInstance::Properties::GetIntfByUuid(
        const boost::uuids::uuid &uuid) const {
    const InterfaceData *intf_data = NULL;

    for (unsigned i = 0; i < interfaces.size(); ++i) {
        intf_data = &interfaces[i];
        if (intf_data->vmi_uuid == uuid)
            return intf_data;
    }
    return NULL;
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
    cmp = compare(interfaces.size(), rhs.interfaces.size());
    if (cmp != 0) {
        return cmp;
    }

    const InterfaceData *intf_other;
    const InterfaceData *intf_data;
    for (unsigned i = 0; i < rhs.interfaces.size(); ++i) {
        intf_other = &rhs.interfaces[i];
        intf_data = GetIntfByUuid(intf_other->vmi_uuid);
        if (intf_data == NULL)
            return -1;
        cmp = compare(intf_other->intf_type, intf_data->intf_type);
        if (cmp != 0) {
            return cmp;
        }
        cmp = compare(intf_other->ip_addr, intf_data->ip_addr);
        if (cmp != 0) {
            return cmp;
        }
        cmp = compare(intf_other->ip_prefix_len, intf_data->ip_prefix_len);
        if (cmp != 0) {
            return cmp;
        }
        cmp = compare(intf_other->mac_addr, intf_data->mac_addr);
        if (cmp != 0) {
            return cmp;
        }
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

    cmp = compare(pool_id, rhs.pool_id);
    if (cmp == 0) {
        if (!pool_id.is_nil())
            return !cmp;
    }
    return cmp;
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
    ss << " interfaces: [";

    const InterfaceData *intf_other;
    const InterfaceData *intf_data;
    for (unsigned i = 0; i < rhs.interfaces.size(); ++i) {
        intf_other = &rhs.interfaces[i];
        intf_data = GetIntfByUuid(intf_other->vmi_uuid);
        if (intf_data == NULL) {
            ss << " DEL [vmi: " << intf_other->vmi_uuid << " mac: "
               << intf_other->mac_addr << " ip: " << intf_other->ip_addr
               << " pfx: " << intf_other->ip_prefix_len
               << " intf-type: " << intf_other->intf_type << "]";
            continue;
        }
        ss << " CHANGE [";
        if (compare(intf_data->vmi_uuid, intf_other->vmi_uuid)) {
            ss << " [vmi: -" << intf_data->vmi_uuid
               << " +" << intf_other->vmi_uuid;
        }
        if (compare(intf_data->ip_addr, intf_other->ip_addr)) {
            ss << " ip: -" << intf_data->ip_addr
               << " +" << intf_other->ip_addr;
        }
        if (compare(intf_data->mac_addr, intf_other->mac_addr)) {
            ss << " mac: -" << intf_data->mac_addr
               << " +" << intf_other->mac_addr;
        }
        if (compare(intf_data->ip_prefix_len, intf_other->ip_prefix_len)) {
            ss << " pfx: -" << intf_data->ip_prefix_len
               << " +" << intf_other->ip_prefix_len;
        }
        if (compare(intf_data->intf_type, intf_other->intf_type)) {
            ss << " type: -" << intf_data->intf_type
               << " +" << intf_other->intf_type;
        }
        ss << "]";
    }
    for (unsigned i = 0; i < interfaces.size(); ++i) {
        intf_data = &interfaces[i];
        intf_other = rhs.GetIntfByUuid(intf_data->vmi_uuid);
        if (intf_other == NULL) {
            ss << " NEW [vmi: " << intf_data->vmi_uuid << " mac: "
               << intf_data->mac_addr << " ip: " << intf_data->ip_addr
               << " pfx: " << intf_data->ip_prefix_len
               << " intf-type: " << intf_data->intf_type << "]";
        }
    }
    ss << "]";

    if (compare(pool_id, rhs.pool_id)) {
        ss << " pool_id: -" << pool_id << " +" << rhs.pool_id;
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
    return ss.str();
}

bool ServiceInstance::Properties::Usable() const {
    const InterfaceData *intf_outside = GetIntfByType("right");
    const InterfaceData *intf_inside = GetIntfByType("left");

    if (instance_id.is_nil()) {
        return false;
    }

    for (unsigned i = 0; i < interfaces.size(); ++i) {
        if (interfaces[i].vmi_uuid.is_nil())
            return false;
    }

    if (virtualization_type == ServiceInstance::VRouterInstance) {
        //TODO: investigate for docker
        return true;
    }

    bool common = (intf_outside != NULL &&
                   !intf_outside->vmi_uuid.is_nil() &&
                   !intf_outside->ip_addr.empty() &&
                   (intf_outside->ip_prefix_len >= 0));
    if (!common) {
        return false;
    }

    if (service_type == SourceNAT || interfaces.size() == 2) {
        bool inside = (intf_inside != NULL &&
                       !intf_inside->vmi_uuid.is_nil() &&
                       !intf_inside->ip_addr.empty() &&
                       (intf_inside->ip_prefix_len >= 0));
        if (!inside) {
            return false;
        }
    }

    if (gw_ip.empty())
        return false;

    if (service_type == LoadBalancer) {
        return (!pool_id.is_nil());
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

    const InterfaceData *outside = properties_.GetIntfByType("right");
    const InterfaceData *inside = properties_.GetIntfByType("left");
    if (inside == NULL)
        data.set_vmi_inside("");
    else
        data.set_vmi_inside(UuidToString(inside->vmi_uuid));
    if (outside == NULL)
        data.set_vmi_outside("");
    else
        data.set_vmi_outside(UuidToString(outside->vmi_uuid));

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

DBEntry *ServiceInstanceTable::Add(const DBRequest *request) {
    ServiceInstance *svc_instance = new ServiceInstance();
    svc_instance->SetKey(request->key.get());
    ServiceInstanceCreate *data =
            static_cast<ServiceInstanceCreate *>(request->data.get());
    assert(dependency_manager_);
    svc_instance->SetIFMapNodeState
        (dependency_manager_->SetState(data->node()));
    dependency_manager_->SetObject(data->node(), svc_instance);

    ServiceInstance::Properties properties;
    properties.Clear();
    assert(graph_);
    CalculateProperties(graph_, svc_instance->ifmap_node(), &properties);
    svc_instance->set_properties(properties);

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
    /*
     * FIX(safchain), get OnChange with another object than ServiceInstanceUpdate
     * when restarting agent with a registered instance
     */
    if (dynamic_cast<ServiceInstanceUpdate*>(request->data.get()) != NULL) {
        ServiceInstanceUpdate *data =
                static_cast<ServiceInstanceUpdate *>(request->data.get());
        svc_instance->set_properties(data->properties());
    } else {
        ServiceInstanceCreate *data = dynamic_cast<ServiceInstanceCreate*>(request->data.get());
        if (data) {
            assert(graph_);
            if (!svc_instance->ifmap_node()) {
                ServiceInstance::Properties properties;
                svc_instance->SetKey(request->key.get());
                properties.Clear();
                CalculateProperties(graph_, data->node(), &properties);
                assert(dependency_manager_);
                svc_instance->set_properties(properties);
                svc_instance->SetIFMapNodeState
                            (dependency_manager_->SetState(data->node()));
                dependency_manager_->SetObject(data->node(), svc_instance);
            }
        }
    }
    return true;
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
    IFMapNodeState *state = dependency_manager_->IFMapNodeGet(node);
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>
                                                    (state->object());

    if (!svc_instance || svc_instance->uuid() != id) {
        request.data.reset(new ServiceInstanceCreate(node));
        return true;
    } else {
        assert(graph_);
        ServiceInstance::Properties properties;
        CalculateProperties(graph_, node, &properties);

        if (properties.CompareTo(svc_instance->properties()) == 0)
            return false;

        if (svc_instance->properties().Usable() != properties.Usable()) {
            LOG(DEBUG, "service-instance properties change"
                        << svc_instance->properties().DiffString(properties));
        }
        request.data.reset(new ServiceInstanceUpdate(properties));
    }
    return true;
}

void ServiceInstanceTable::CalculateProperties(
    DBGraph *graph, IFMapNode *node, ServiceInstance::Properties *properties) {

    properties->Clear();

    if (node->IsDeleted()) {
        return;
    }

    IFMapNode *st_node = FindServiceTemplateNode(graph, node);
    if (st_node == NULL) {
        return;
    }
    autogen::ServiceTemplate *svc_template =
        static_cast<autogen::ServiceTemplate *>(st_node->GetObject());
    autogen::ServiceTemplateType svc_template_props =
        svc_template->properties();
    FindAndSetTypes(graph, node, properties, &svc_template_props);

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
    FindAndSetInterfaces(graph, vm_node, svc_instance, svc_template_props, properties);

    if (properties->service_type == ServiceInstance::LoadBalancer) {
        FindAndSetLoadbalancer(graph, node, properties);
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
        state->set_uuid(new_uuid);
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    } else {
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
    LOG(ERROR, "Unkown type: " << type);
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
