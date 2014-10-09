/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.io.IOException;
import java.net.InetAddress;
import java.util.Map;
import java.util.HashMap;
import java.util.TreeMap;
import java.util.List;
import java.util.SortedMap;
import java.util.UUID;

import org.apache.log4j.Logger;
import org.apache.commons.net.util.SubnetUtils;

import net.juniper.contrail.api.ApiConnector;
import net.juniper.contrail.api.ApiConnectorFactory;
import net.juniper.contrail.api.ApiPropertyBase;
import net.juniper.contrail.api.ObjectReference;
import net.juniper.contrail.api.types.InstanceIp;
import net.juniper.contrail.api.types.MacAddressesType;
import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualMachineInterface;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.types.Project;
import net.juniper.contrail.contrail_vrouter_api.ContrailVRouterApi;

public class VncDB {
    private static final Logger s_logger = 
            Logger.getLogger(VncDB.class);
    private static final int vrouterApiPort = 9090;
    private final String apiServerAddress;
    private final int apiServerPort;
    private HashMap<String, ContrailVRouterApi> vrouterApiMap;
    
    private ApiConnector apiConnector;
    private Project vCenterProject;
    private NetworkIpam vCenterIpam;

    public static final String VNC_ROOT_DOMAIN     = "default-domain";
    public static final String VNC_VCENTER_PROJECT = "vCenter";
    public static final String VNC_VCENTER_IPAM    = "vCenter-ipam";
    
    public VncDB(String apiServerAddress, int apiServerPort) {
        this.apiServerAddress = apiServerAddress;
        this.apiServerPort = apiServerPort;
        vrouterApiMap = new HashMap<String, ContrailVRouterApi>();
    }
    
    public void Initialize() throws IOException {
        apiConnector = ApiConnectorFactory.build(apiServerAddress,
                apiServerPort);

        // Check if Vmware Project exists on VNC. If not, create one.
        vCenterProject = (Project) apiConnector.findByFQN(Project.class, 
                                        VNC_ROOT_DOMAIN + ":" + VNC_VCENTER_PROJECT);
        if (vCenterProject == null) {
            s_logger.info(" vCenter project not present, creating ");
            vCenterProject = new Project();
            vCenterProject.setName("vCenter");
            if (!apiConnector.create(vCenterProject)) {
              s_logger.error("Unable to create project: " + vCenterProject.getName());
            }
        } else {
            s_logger.info(" vCenter project present, continue ");
        }

        // Check if VMWare vCenter-ipam exist on VNC. If not, create one.
        vCenterIpam = (NetworkIpam) apiConnector.findByFQN(Project.class, 
                       VNC_ROOT_DOMAIN + ":" + VNC_VCENTER_PROJECT + ":" + VNC_VCENTER_IPAM);
        if (vCenterIpam == null) {
            s_logger.info(" vCenter Ipam not present, creating ...");
            vCenterIpam = new NetworkIpam();
            vCenterIpam.setParent(vCenterProject);
            vCenterIpam.setName("vCenter-ipam");
            if (!apiConnector.create(vCenterIpam)) {
              s_logger.error("Unable to create Ipam: " + vCenterIpam.getName());
            }
        } else {
            s_logger.info(" vCenter Ipam present, continue ");
        }
    }
    
    private void DeleteVirtualMachineInternal(
            VirtualMachineInterface vmInterface) throws IOException {
        List<ObjectReference<ApiPropertyBase>> instanceIpRefs = 
                vmInterface.getInstanceIpBackRefs();
        for (ObjectReference<ApiPropertyBase> instanceIpRef : 
            Utils.safe(instanceIpRefs)) {
            s_logger.info("Delete instance IP: " + 
                instanceIpRef.getReferredName());
            apiConnector.delete(InstanceIp.class, 
                    instanceIpRef.getUuid());
        }
        // There should only be one virtual machine hanging off the virtual
        // machine interface
        //String vmUuid = vmInterface.getParentUuid();   
        List<ObjectReference<ApiPropertyBase>> vmRefs = vmInterface.getVirtualMachine();
        if (vmRefs == null || vmRefs.size() == 0) {
            s_logger.error("Virtual Machine Interface : " + vmInterface.getDisplayName() + 
                    " NO associated virtual machine ");
        }
        if (vmRefs.size() > 1) {
            s_logger.error("Virtual Machine Interface : " + vmInterface.getDisplayName() + 
                           "(" + vmRefs.size() + ")" + " associated virtual machines ");
        }

        ObjectReference<ApiPropertyBase> vmRef = vmRefs.get(0);
        VirtualMachine vm = (VirtualMachine) apiConnector.findById(
                VirtualMachine.class, vmRef.getUuid());
        boolean deleteVm = false;
        //if (vm.getVirtualMachineInterfaces().size() == 1) {
            deleteVm = true;
        //}
        // Extract VRouter IP address from display name
        String vrouterIpAddress = vm.getDisplayName();
        s_logger.info("Delete virtual machine interface: " + 
                vmInterface.getName());
        String vmInterfaceUuid = vmInterface.getUuid();
        apiConnector.delete(VirtualMachineInterface.class,
                vmInterfaceUuid);
        if (deleteVm) {
            s_logger.info("Delete virtual machine: " + vm.getName());
            apiConnector.delete(VirtualMachine.class, vmRef.getUuid());      
        }
        // Unplug notification to vrouter
        if (vrouterIpAddress == null) {
            s_logger.info("Virtual machine interface: " + vmInterfaceUuid + 
                    " delete notification NOT sent");
            return;
        }
        ContrailVRouterApi vrouterApi = vrouterApiMap.get(vrouterIpAddress);
        if (vrouterApi == null) {
            vrouterApi = new ContrailVRouterApi(
                    InetAddress.getByName(vrouterIpAddress), 
                    vrouterApiPort, false);
            vrouterApiMap.put(vrouterIpAddress, vrouterApi);
        }
        vrouterApi.DeletePort(UUID.fromString(vmInterfaceUuid));
    }

    public void DeleteVirtualMachine(VncVirtualMachineInfo vmInfo) 
            throws IOException {
        DeleteVirtualMachineInternal(vmInfo.getVmInterfaceInfo());
    }
    
    public void DeleteVirtualMachine(String vmUuid) throws IOException {        
        VirtualMachine vm = (VirtualMachine) apiConnector.findById(
                VirtualMachine.class, vmUuid);
        apiConnector.read(vm);
        // Extract VRouter IP address from display name
        String vrouterIpAddress = vm.getDisplayName();
        List<ObjectReference<ApiPropertyBase>> vmInterfaceRefs =
                vm.getVirtualMachineInterfaces();
        assert vmInterfaceRefs.size() == 1 : "Virtual machine: " + vmUuid +
                " associated with more than one virtual machine interface";
        for (ObjectReference<ApiPropertyBase> vmInterfaceRef :
            vmInterfaceRefs) {
            String vmInterfaceUuid = vmInterfaceRef.getUuid();
            VirtualMachineInterface vmInterface = (VirtualMachineInterface)
                    apiConnector.findById(VirtualMachineInterface.class, 
                            vmInterfaceUuid);
            apiConnector.read(vmInterface);
            List<ObjectReference<ApiPropertyBase>> instanceIpRefs = 
                    vmInterface.getInstanceIpBackRefs();
            for (ObjectReference<ApiPropertyBase> instanceIpRef : 
                Utils.safe(instanceIpRefs)) {
                s_logger.info("Delete instance IP: " + 
                        instanceIpRef.getReferredName());
                apiConnector.delete(InstanceIp.class, 
                        instanceIpRef.getUuid());
            }
            s_logger.info("Delete virtual machine interface: " + 
                    vmInterface.getName());
            apiConnector.delete(VirtualMachineInterface.class,
                    vmInterfaceUuid);
            // Unplug notification to vrouter
            if (vrouterIpAddress == null) {
                s_logger.info("Virtual machine interace: " + vmInterfaceUuid + 
                        " delete notification NOT sent");
                continue;
            }
            ContrailVRouterApi vrouterApi = vrouterApiMap.get(vrouterIpAddress);
            if (vrouterApi == null) {
                vrouterApi = new ContrailVRouterApi(
                        InetAddress.getByName(vrouterIpAddress), 
                        vrouterApiPort, false);
                vrouterApiMap.put(vrouterIpAddress, vrouterApi);
            }
            vrouterApi.DeletePort(UUID.fromString(vmInterfaceUuid));
        }
        apiConnector.delete(VirtualMachine.class, vmUuid);
        s_logger.info("Delete virtual machine: " + vmUuid);
    }
    
    public void CreateVirtualMachine(String vnUuid, String vmUuid,
            String macAddress, String vmName, String vrouterIpAddress,
            String hostName, short isolatedVlanId, short primaryVlanId) throws IOException {
        s_logger.info("CreateVirtualMachine : " + vmUuid + ", vrouterIpAddress: " + vrouterIpAddress + ", vlan: " + isolatedVlanId + "/" + primaryVlanId);
        VirtualNetwork network = (VirtualNetwork) apiConnector.findById(
                VirtualNetwork.class, vnUuid);
        apiConnector.read(network);
        // Virtual machine
        VirtualMachine vm = new VirtualMachine();
        vm.setName(vmUuid);
        vm.setUuid(vmUuid);
        // Encode VRouter IP address in display name
        if (vrouterIpAddress != null) {
            vm.setDisplayName(vrouterIpAddress);
        }
        apiConnector.create(vm);
        apiConnector.read(vm);

        // Virtual machine interface
        String vmInterfaceName = "vmi-" + vmName;
        String vmiUuid = UUID.randomUUID().toString();
        VirtualMachineInterface vmInterface = new VirtualMachineInterface();
        vmInterface.setDisplayName(vmInterfaceName);
        vmInterface.setUuid(vmiUuid);
        vmInterface.setParent(vCenterProject);
        vmInterface.setName(vmiUuid);
        vmInterface.setVirtualNetwork(network);
        vmInterface.addVirtualMachine(vm);
        MacAddressesType macAddrType = new MacAddressesType();
        macAddrType.addMacAddress(macAddress);
        vmInterface.setMacAddresses(macAddrType);
        apiConnector.create(vmInterface);
        String vmInterfaceUuid = apiConnector.findByName(
                VirtualMachineInterface.class, vm, vmInterface.getName());
        s_logger.info("Create virtual machine interface:" + vmInterfaceName + 
                ": " + vmInterfaceUuid + "vmiUuid :" + vmiUuid);

        // Instance Ip
        String instanceIpName = "ip-" + vmName;
        String instIpUuid = UUID.randomUUID().toString();
        InstanceIp instanceIp = new InstanceIp();
        //instanceIp.setParent(vm);   SAS_FIXME
        instanceIp.setDisplayName(instanceIpName);
        instanceIp.setUuid(instIpUuid);
        instanceIp.setName(instIpUuid);
        instanceIp.setVirtualNetwork(network);
        instanceIp.setVirtualMachineInterface(vmInterface);
        apiConnector.create(instanceIp);

        // Read back to get assigned IP address
        //instanceIp = (InstanceIp) apiConnector.find(InstanceIp.class, vm, 
         //                                           instanceIp.getName());
        apiConnector.read(instanceIp);
        String vmIpAddress = instanceIp.getAddress();
        s_logger.info("Create instance IP:" + instanceIp.getName() + ": " + 
                vmIpAddress);

        // Plug notification to vrouter
        if (vrouterIpAddress == null) {
            s_logger.info("Virtual machine: " + vmName + " host: " + hostName
                + " create notification NOT sent");
            return;
        }
        try {
            ContrailVRouterApi vrouterApi = vrouterApiMap.get(vrouterIpAddress);
            if (vrouterApi == null) {
                   vrouterApi = new ContrailVRouterApi(
                         InetAddress.getByName(vrouterIpAddress), 
                         vrouterApiPort, false);
                   vrouterApiMap.put(vrouterIpAddress, vrouterApi);
            }
            vrouterApi.AddPort(UUID.fromString(vmiUuid),
                                         UUID.fromString(vmUuid), vmInterface.getName(),
                                         InetAddress.getByName(vmIpAddress),
                                         Utils.parseMacAddress(macAddress),
                                         UUID.fromString(vnUuid), isolatedVlanId, primaryVlanId);
            s_logger.debug("VRouterAPi Add Port success - port name: " + vmInterface.getName() + "(" + vmInterface.getDisplayName() + ")");
        }catch(Throwable e) {
            s_logger.error("Exception : " + e);
            e.printStackTrace();
        }
    }
    
    public void CreateVirtualNetwork(String vnUuid, String vnName,
            String subnetAddr, String subnetMask, String gatewayAddr, 
            short isolatedVlanId, short primaryVlanId,
            SortedMap<String, VmwareVirtualMachineInfo> vmMapInfos) throws
            IOException {
        s_logger.info("Create VN: Uuid:" + vnUuid + " , vnName:" + vnName);
        VirtualNetwork vn = new VirtualNetwork();
        vn.setName(vnName);
        vn.setDisplayName(vnName);
        vn.setUuid(vnUuid);
        vn.setParent(vCenterProject);
        String ipamUuid = apiConnector.findByName(NetworkIpam.class, null, 
                "default-network-ipam");
        NetworkIpam ipam = (NetworkIpam) apiConnector.findById(
                NetworkIpam.class, ipamUuid);
        SubnetUtils subnetUtils = new SubnetUtils(subnetAddr, subnetMask);  
        String cidr = subnetUtils.getInfo().getCidrSignature();
        VnSubnetsType subnet = new VnSubnetsType();
        String[] addr_pair = cidr.split("\\/");
        /*subnet.addIpamSubnets(new SubnetType(addr_pair[0], 
                Integer.parseInt(addr_pair[1])), gatewayAddr,
                UUID.randomUUID().toString());*/
        subnet.addIpamSubnets(new VnSubnetsType.IpamSubnetType(new SubnetType(addr_pair[0], Integer.parseInt(addr_pair[1])), gatewayAddr, UUID.randomUUID().toString(), true, null, null, false, null, null, vn.getName() + "-subnet"));

        vn.setNetworkIpam(vCenterIpam, subnet);
        apiConnector.create(vn); 
        if (vmMapInfos == null)
            return;

        for (Map.Entry<String, VmwareVirtualMachineInfo> vmMapInfo :
            vmMapInfos.entrySet()) {
            String vmUuid = vmMapInfo.getKey();
            VmwareVirtualMachineInfo vmInfo = vmMapInfo.getValue();
            String macAddress = vmInfo.getMacAddress();
            String vmName = vmInfo.getName();
            String vrouterIpAddr = vmInfo.getVrouterIpAddress();
            String hostName = vmInfo.getHostName();
            CreateVirtualMachine(vnUuid, vmUuid, macAddress, vmName,
                    vrouterIpAddr, hostName, isolatedVlanId, primaryVlanId);
        }  
    }
    
    public void DeleteVirtualNetwork(String uuid) 
            throws IOException {
        s_logger.info("Delete virtual network: " + uuid);
        VirtualNetwork network = (VirtualNetwork) apiConnector.findById(
                VirtualNetwork.class, uuid);
        apiConnector.read(network);
        List<ObjectReference<ApiPropertyBase>> vmInterfaceRefs = 
                network.getVirtualMachineInterfaceBackRefs();
        for (ObjectReference<ApiPropertyBase> vmInterfaceRef : 
                Utils.safe(vmInterfaceRefs)) {
            VirtualMachineInterface vmInterface = (VirtualMachineInterface)
                    apiConnector.findById(VirtualMachineInterface.class,
                            vmInterfaceRef.getUuid());
            DeleteVirtualMachineInternal(vmInterface);
        }
        apiConnector.delete(VirtualNetwork.class, network.getUuid());     
    }
    
    private static boolean doIgnoreVirtualNetwork(String name) {
        // Ignore default, fabric, and link-local networks
        if (name.equals("__link_local__") || 
                name.equals("default-virtual-network") || 
                name.equals("public") || 
                name.equals("ip-fabric")) {
            return true;
        }
        return false;
    }
    
    @SuppressWarnings("unchecked")
    public SortedMap<String, VncVirtualNetworkInfo> populateVirtualNetworkInfo() 
        throws Exception {
        // Extract list of virtual networks
        List<VirtualNetwork> networks = null;
        try {
        networks = (List<VirtualNetwork>) 
                apiConnector.list(VirtualNetwork.class, null);
        } catch (Exception ex) {
            s_logger.error("Exception in api.list: " + ex);
            ex.printStackTrace();
        }
        if (networks == null || networks.size() == 0) {
            s_logger.info("NO virtual networks FOUND");
            return null;
        }
        SortedMap<String, VncVirtualNetworkInfo> vnInfos =
                new TreeMap<String, VncVirtualNetworkInfo>();
        for (VirtualNetwork network : networks) {
            // Read in the virtual network
            apiConnector.read(network);
            String vnName = network.getName();
            String vnUuid = network.getUuid();
            // Ignore network ?
            if (doIgnoreVirtualNetwork(vnName)) {
                continue;
            }
            // Extract virtual machine interfaces
            List<ObjectReference<ApiPropertyBase>> vmInterfaceRefs = 
                    network.getVirtualMachineInterfaceBackRefs();
            if (vmInterfaceRefs == null || vmInterfaceRefs.size() == 0) {
                s_logger.info("Virtual network: " + network + 
                        " NO associated virtual machine interfaces");
            }
            SortedMap<String, VncVirtualMachineInfo> vmInfos = 
                    new TreeMap<String, VncVirtualMachineInfo>();
            for (ObjectReference<ApiPropertyBase> vmInterfaceRef :
                Utils.safe(vmInterfaceRefs)) {
                VirtualMachineInterface vmInterface =
                        (VirtualMachineInterface) apiConnector.findById(
                                VirtualMachineInterface.class,
                                vmInterfaceRef.getUuid());
                apiConnector.read(vmInterface);
                //String vmUuid = vmInterface.getParentUuid();
                List<ObjectReference<ApiPropertyBase>> vmRefs = vmInterface.getVirtualMachine();
                if (vmRefs == null || vmRefs.size() == 0) {
                    s_logger.error("Virtual Machine Interface : " + vmInterface.getDisplayName() + 
                            " NO associated virtual machine ");
                }
                if (vmRefs.size() > 1) {
                    s_logger.error("Virtual Machine Interface : " + vmInterface.getDisplayName() + 
                                   "(" + vmRefs.size() + ")" + " associated virtual machines ");
                }

                ObjectReference<ApiPropertyBase> vmRef = vmRefs.get(0);
                VirtualMachine vm = (VirtualMachine) apiConnector.findById(
                        VirtualMachine.class, vmRef.getUuid());
                apiConnector.read(vm);
                VncVirtualMachineInfo vmInfo = new VncVirtualMachineInfo(
                        vm, vmInterface);
                vmInfos.put(vm.getUuid(), vmInfo);
            }
            VncVirtualNetworkInfo vnInfo = 
                    new VncVirtualNetworkInfo(vnName, vmInfos);
            vnInfos.put(vnUuid, vnInfo);
        }
        if (vnInfos.size() == 0) {
            s_logger.info("NO virtual networks found");
        }
        return vnInfos;
    }
}
