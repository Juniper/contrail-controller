/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.io.IOException;
import java.net.InetAddress;
import java.util.Map;
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
import net.juniper.contrail.contrail_vrouter_api.ContrailVRouterApi;

public class VncDB {
    private static final Logger s_logger = 
            Logger.getLogger(VncDB.class);
    private static final int vrouterApiPort = 5059;
    private final String apiServerAddress;
    private final int apiServerPort;
    
    private ApiConnector apiConnector;
    
    public VncDB(String apiServerAddress, int apiServerPort) {
        this.apiServerAddress = apiServerAddress;
        this.apiServerPort = apiServerPort;
    }
    
    public void Initialize() {
        apiConnector = ApiConnectorFactory.build(apiServerAddress,
                apiServerPort);
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
        String vmUuid = vmInterface.getParentUuid();   
        VirtualMachine vm = (VirtualMachine) apiConnector.findById(
                VirtualMachine.class, vmUuid);
        boolean deleteVm = false;
        if (vm.getVirtualMachineInterfaces().size() == 1) {
            deleteVm = true;
        }
        // Extract VRouter IP address from display name
        String vrouterIpAddress = vm.getDisplayName();
        s_logger.info("Delete virtual machine interface: " + 
                vmInterface.getName());
        String vmInterfaceUuid = vmInterface.getUuid();
        apiConnector.delete(VirtualMachineInterface.class,
                vmInterfaceUuid);
        if (deleteVm) {
            s_logger.info("Delete virtual machine: " + vm.getName());
            apiConnector.delete(VirtualMachine.class, vmUuid);      
        }
        // Unplug notification to vrouter
        if (vrouterIpAddress == null) {
            s_logger.info("Virtual machine interface: " + vmInterfaceUuid + 
                    " delete notification NOT sent");
            return;
        }
        ContrailVRouterApi vrouterApi = new ContrailVRouterApi(
                InetAddress.getByName(vrouterIpAddress), 
                vrouterApiPort, true);
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
            ContrailVRouterApi vrouterApi = new ContrailVRouterApi(
                    InetAddress.getByName(vrouterIpAddress), 
                    vrouterApiPort, true);
            vrouterApi.DeletePort(UUID.fromString(vmInterfaceUuid));
        }
        apiConnector.delete(VirtualMachine.class, vmUuid);
        s_logger.info("Delete virtual machine: " + vmUuid);
    }
    
    public void CreateVirtualMachine(String vnUuid, String vmUuid,
            String macAddress, String vmName, String vrouterIpAddress,
            String hostName) throws IOException {
        VirtualNetwork network = (VirtualNetwork) apiConnector.findById(
                VirtualNetwork.class, vnUuid);
        apiConnector.read(network);
        // Virtual machine
        VirtualMachine vm = new VirtualMachine();
        vm.setName(vmName);
        vm.setUuid(vmUuid);
        // Encode VRouter IP address in display name
        if (vrouterIpAddress != null) {
            vm.setDisplayName(vrouterIpAddress);
        }
        apiConnector.create(vm);
        s_logger.info("Create virtual machine: " + vmName);
        // Virtual machine interface
        VirtualMachineInterface vmInterface = new VirtualMachineInterface();
        vmInterface.setParent(vm);
        String vmInterfaceName = "vmi-" + vmName;
        vmInterface.setName(vmInterfaceName);
        vmInterface.setVirtualNetwork(network);
        MacAddressesType macAddrType = new MacAddressesType();
        macAddrType.addMacAddress(macAddress);
        vmInterface.setMacAddresses(macAddrType);
        apiConnector.create(vmInterface);
        String vmInterfaceUuid = apiConnector.findByName(
                VirtualMachineInterface.class, vm, vmInterfaceName);
        s_logger.info("Create virtual machine interface:" + vmInterfaceName + 
                ": " + vmInterfaceUuid);
        // Instance Ip
        InstanceIp instanceIp = new InstanceIp();
        String instanceIpName = "ip-" + vmName;
        instanceIp.setName(instanceIpName);
        instanceIp.setVirtualNetwork(network);
        instanceIp.setVirtualMachineInterface(vmInterface);
        apiConnector.create(instanceIp);
        // Read back to get assigned IP address
        instanceIp = (InstanceIp) apiConnector.findByFQN(InstanceIp.class,
                instanceIpName);
        apiConnector.read(instanceIp);
        String vmIpAddress = instanceIp.getAddress();
        s_logger.info("Create instance IP:" + instanceIpName + ": " + 
                vmIpAddress);
        // Plug notification to vrouter
        if (vrouterIpAddress == null) {
            s_logger.info("Virtual machine: " + vmName + " host: " + hostName
                + " create notification NOT sent");
            return;
        }
        ContrailVRouterApi vrouterApi = new ContrailVRouterApi(
                InetAddress.getByName(vrouterIpAddress), 
                vrouterApiPort, true);
        vrouterApi.AddPort(UUID.fromString(vmInterfaceUuid),
                UUID.fromString(vmUuid), vmInterfaceName,
                InetAddress.getByName(vmIpAddress),
                Utils.parseMacAddress(macAddress),
                UUID.fromString(vnUuid)); 
    }
    
    public void CreateVirtualNetwork(String vnUuid, String vnName,
            String subnetAddr, String subnetMask, String gatewayAddr,
            SortedMap<String, VmwareVirtualMachineInfo> vmMapInfos) throws
            IOException {
        VirtualNetwork vn = new VirtualNetwork();
        vn.setName(vnName);
        vn.setUuid(vnUuid);
        String ipamUuid = apiConnector.findByName(NetworkIpam.class, null, 
                "default-network-ipam");
        NetworkIpam ipam = (NetworkIpam) apiConnector.findById(
                NetworkIpam.class, ipamUuid);
        SubnetUtils subnetUtils = new SubnetUtils(subnetAddr, subnetMask);  
        String cidr = subnetUtils.getInfo().getCidrSignature();
        VnSubnetsType subnet = new VnSubnetsType();
        String[] addr_pair = cidr.split("\\/");
        subnet.addIpamSubnets(new SubnetType(addr_pair[0], 
                Integer.parseInt(addr_pair[1])), gatewayAddr,
                UUID.randomUUID().toString());
        vn.setNetworkIpam(ipam, subnet);
        apiConnector.create(vn); 
        s_logger.info("Create virtual network: " + vnName);
        for (Map.Entry<String, VmwareVirtualMachineInfo> vmMapInfo :
            vmMapInfos.entrySet()) {
            String vmUuid = vmMapInfo.getKey();
            VmwareVirtualMachineInfo vmInfo = vmMapInfo.getValue();
            String macAddress = vmInfo.getMacAddress();
            String vmName = vmInfo.getName();
            String vrouterIpAddr = vmInfo.getVrouterIpAddress();
            String hostName = vmInfo.getHostName();
            CreateVirtualMachine(vnUuid, vmUuid, macAddress, vmName,
                    vrouterIpAddr, hostName);
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
                name.equals("ip-fabric")) {
            return true;
        }
        return false;
    }
    
    @SuppressWarnings("unchecked")
    public SortedMap<String, VncVirtualNetworkInfo> populateVirtualNetworkInfo() 
        throws IOException {
        // Extract list of virtual networks
        List<VirtualNetwork> networks = (List<VirtualNetwork>) 
                apiConnector.list(VirtualNetwork.class, null);
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
                String vmUuid = vmInterface.getParentUuid();
                VirtualMachine vm = (VirtualMachine) apiConnector.findById(
                        VirtualMachine.class, vmUuid);
                apiConnector.read(vm);
                VncVirtualMachineInfo vmInfo = new VncVirtualMachineInfo(
                        vm, vmInterface);
                vmInfos.put(vmUuid, vmInfo);
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
