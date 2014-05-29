/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.net.URL;
import java.util.TreeMap;
import java.util.SortedMap;
import java.util.UUID;

import org.apache.log4j.Logger;

import com.vmware.vim25.DVPortSetting;
import com.vmware.vim25.DVPortgroupConfigInfo;
import com.vmware.vim25.IpPool;
import com.vmware.vim25.IpPoolIpPoolConfigInfo;
import com.vmware.vim25.ManagedObjectReference;
import com.vmware.vim25.NetworkSummary;
import com.vmware.vim25.VMwareDVSPortSetting;
import com.vmware.vim25.VirtualDevice;
import com.vmware.vim25.VirtualDeviceBackingInfo;
import com.vmware.vim25.VirtualEthernetCard;
import com.vmware.vim25.VirtualEthernetCardDistributedVirtualPortBackingInfo;
import com.vmware.vim25.VirtualMachineConfigInfo;
import com.vmware.vim25.VirtualMachinePowerState;
import com.vmware.vim25.VirtualMachineRuntimeInfo;
import com.vmware.vim25.VmwareDistributedVirtualSwitchPvlanSpec;
import com.vmware.vim25.VmwareDistributedVirtualSwitchVlanSpec;
import com.vmware.vim25.mo.Datacenter;
import com.vmware.vim25.mo.DistributedVirtualPortgroup;
import com.vmware.vim25.mo.Folder;
import com.vmware.vim25.mo.HostSystem;
import com.vmware.vim25.mo.InventoryNavigator;
import com.vmware.vim25.mo.IpPoolManager;
import com.vmware.vim25.mo.ServiceInstance;
import com.vmware.vim25.mo.VirtualMachine;
import com.vmware.vim25.mo.VmwareDistributedVirtualSwitch;

public class VCenterDB {
    private static final Logger s_logger =
            Logger.getLogger(VCenterDB.class);
    private static final String contrailDvSwitchName = "dvSwitch";
    private static final String contrailDataCenterName = "Datacenter";
    private static final String contrailVRouterVmNamePrefix = "contrailVM";
    private static final String vcenterDomainName = "default-domain";
    private static final String vcenterProjectName = "default-project";
    
    private final String vcenterUrl;
    private final String vcenterUsername;
    private final String vcenterPassword;
    
    private ServiceInstance serviceInstance;
    private Folder rootFolder;
    private InventoryNavigator inventoryNavigator;
    private IpPoolManager ipPoolManager;
    
    public VCenterDB(String vcenterUrl, String vcenterUsername,
            String vcenterPassword) {
        this.vcenterUrl = vcenterUrl;
        this.vcenterUsername = vcenterUsername;
        this.vcenterPassword = vcenterPassword;
    }
    
    public void Initialize() throws Exception {
        // Connect to VCenter
        serviceInstance = new ServiceInstance(new URL(vcenterUrl),
                vcenterUsername, vcenterPassword, true);
        rootFolder = serviceInstance.getRootFolder();
        inventoryNavigator = new InventoryNavigator(rootFolder);
        ipPoolManager = serviceInstance.getIpPoolManager();
    }
    
    private static IpPool getIpPool(
            DistributedVirtualPortgroup portGroup, IpPool[] ipPools) {
        NetworkSummary summary = portGroup.getSummary();
        Integer poolid = summary.getIpPoolId();
        if (poolid == null) {
            s_logger.info("dvPg: " + portGroup.getName() + 
                    " IpPool NOT configured");
            return null;
        }
        // Validate that the IpPool id exists
        for (IpPool pool : ipPools) {
            if (pool.id == poolid.intValue()) {
                return pool;
            }
        }
        s_logger.error("dvPg: " + portGroup.getName() + 
                " INVALID IpPoolId " + poolid);
        return null;
    }
    
    private static int getVlanId(
            DistributedVirtualPortgroup portGroup) {
        DVPortgroupConfigInfo configInfo = portGroup.getConfig();
        DVPortSetting portSetting = configInfo.getDefaultPortConfig();
        if (portSetting instanceof VMwareDVSPortSetting) {
            VMwareDVSPortSetting vPortSetting = 
                    (VMwareDVSPortSetting) portSetting;
            VmwareDistributedVirtualSwitchVlanSpec vlanSpec = 
                    vPortSetting.getVlan();
            if (vlanSpec instanceof VmwareDistributedVirtualSwitchPvlanSpec) {
                VmwareDistributedVirtualSwitchPvlanSpec pvlanSpec = 
                        (VmwareDistributedVirtualSwitchPvlanSpec) vlanSpec;
                return pvlanSpec.getPvlanId();
            } else {
                s_logger.error("dvPg: " + portGroup.getName() + 
                        " port setting: " +  vPortSetting + 
                        ": INVALID vlan spec: " + vlanSpec);
                return -1;
            }
        } else {
            s_logger.error("dvPg: " + portGroup.getName() + 
                    " INVALID port setting: " + portSetting);
            return -1;
        }
    }
    
    private static String getVirtualMachineMacAddress(
            VirtualMachineConfigInfo vmConfigInfo,
            DistributedVirtualPortgroup portGroup) {
        VirtualDevice devices[] = vmConfigInfo.getHardware().getDevice();
        for (VirtualDevice device : devices) {
            // XXX Assuming only one interface
            if (device instanceof VirtualEthernetCard) {
                VirtualDeviceBackingInfo backingInfo = 
                        device.getBacking();
                // Is it backed by the distributed virtual port group? 
                if (backingInfo instanceof 
                    VirtualEthernetCardDistributedVirtualPortBackingInfo) {
                    VirtualEthernetCardDistributedVirtualPortBackingInfo
                    dvpBackingInfo = 
                    (VirtualEthernetCardDistributedVirtualPortBackingInfo)
                    backingInfo;
                    if (dvpBackingInfo.getPort().getPortgroupKey().
                            equals(portGroup.getKey())) {
                        String vmMac = ((VirtualEthernetCard) device).
                                getMacAddress();
                        return vmMac;
                    }
                }
            } 
        }
        s_logger.error("dvPg: " + portGroup.getName() + " vmConfig: " + 
                vmConfigInfo + " MAC Address NOT found");
        return null;
    }
    
    private static String getVirtualMachineIpAddress(HostSystem host,
            String vmNamePrefix) throws Exception {
        com.vmware.vim25.mo.VirtualMachine[] vms = host.getVms();
        for (com.vmware.vim25.mo.VirtualMachine vm : vms) {
            String vmName = vm.getName();
            if (!vmName.toLowerCase().contains(vmNamePrefix.toLowerCase())) {
                // XXX Assumption here is that VMware Tools are installed
                // and IP address is available
                String ipAddress = vm.getSummary().getGuest().getIpAddress();
                return ipAddress;
            }
        }
        return null;
    }
    
    private static SortedMap<String, VmwareVirtualMachineInfo> 
        populateVirtualMachineInfo(
                DistributedVirtualPortgroup portGroup) throws Exception {
        // Get list of virtual machines connected to the port group
        VirtualMachine[] vms = portGroup.getVms();
        if (vms == null || vms.length == 0) {
            s_logger.error("dvPg: " + portGroup.getName() + 
                    " NO virtual machines connected");
            return null;
        }
        SortedMap<String, VmwareVirtualMachineInfo> vmInfos = 
                new TreeMap<String, VmwareVirtualMachineInfo>();
        for (VirtualMachine vm : vms) {
            // Is it powered on?
            VirtualMachineRuntimeInfo vmRuntimeInfo = vm.getRuntime();
            VirtualMachinePowerState powerState = 
                    vmRuntimeInfo.getPowerState();
            if (powerState != VirtualMachinePowerState.poweredOn) {
                s_logger.debug("dvPg: " + portGroup.getName() + " vm: " + 
                        vm.getName() + " Power State: " + powerState);
                continue;
            }
            // Name
            String vmName = vm.getName();
            // Extract configuration info
            VirtualMachineConfigInfo vmConfigInfo = vm.getConfig();
            // Extract MAC address
            String vmMac = getVirtualMachineMacAddress(vmConfigInfo,
                    portGroup);
            if (vmMac == null) {
                s_logger.error("dvPg: " + portGroup.getName() + " vm: " + 
                        vm.getName() + " MAC Address NOT found");
                continue;
            }
            // Get instance UUID
            String instanceUuid = vmConfigInfo.getInstanceUuid();
            // Get host information
            ManagedObjectReference hmor = vmRuntimeInfo.getHost();
            HostSystem host = new HostSystem(
                vm.getServerConnection(), hmor);
            String hostName = host.getName();
            // Get Contrail VRouter virtual machine information from the host
            String vrouterIpAddress = getVirtualMachineIpAddress(host,
                    contrailVRouterVmNamePrefix);
            if (vrouterIpAddress == null) {
                s_logger.error("dvPg: " + portGroup.getName() + " host: " + 
                        hostName + " Contrail VRouter VM: " + 
                        contrailVRouterVmNamePrefix + " NOT found");
            }
            VmwareVirtualMachineInfo vmInfo = new 
                    VmwareVirtualMachineInfo(vmName, hostName, 
                            vrouterIpAddress, vmMac); 
            vmInfos.put(instanceUuid, vmInfo);
        }
        if (vmInfos.size() == 0) {
            return null;
        }
        return vmInfos;
    }
    
    public SortedMap<String, VmwareVirtualNetworkInfo> 
        populateVirtualNetworkInfo() throws Exception {
        // Search contrailDvSwitch
        VmwareDistributedVirtualSwitch contrailDvs = 
                (VmwareDistributedVirtualSwitch) 
                inventoryNavigator.searchManagedEntity(
                        "VmwareDistributedVirtualSwitch",
                        contrailDvSwitchName);
        if (contrailDvs == null) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName + 
                    " NOT configured");
            return null;
        }
        // Extract distributed virtual port groups 
        DistributedVirtualPortgroup[] dvPgs = contrailDvs.getPortgroup();
        if (dvPgs == null || dvPgs.length == 0) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName + 
                    " Distributed portgroups NOT configured");
            return null;
        }
        // Extract IP Pools
        Datacenter contrailDC = (Datacenter) inventoryNavigator.
                searchManagedEntity(
                "Datacenter",
                contrailDataCenterName);
        IpPool[] ipPools = ipPoolManager.queryIpPools(contrailDC);
        if (ipPools == null || ipPools.length == 0) {
            s_logger.error("dvSwitch: " + contrailDvSwitchName +
                    " Datacenter: " + contrailDC.getName() + " IP Pools NOT " +
                    "configured");
            return null;
        }
        // Populate VMware Virtual Network Info
        SortedMap<String, VmwareVirtualNetworkInfo> vnInfos =
                new TreeMap<String, VmwareVirtualNetworkInfo>();
        for (DistributedVirtualPortgroup dvPg : dvPgs) {
            // Find associated IP Pool
            IpPool ipPool = getIpPool(dvPg, ipPools);
            if (ipPool == null) {
                continue;
            }
            byte[] vnKeyBytes = dvPg.getKey().getBytes();
            String vnUuid = UUID.nameUUIDFromBytes(vnKeyBytes).toString();
            String vnName = dvPg.getName();
            IpPoolIpPoolConfigInfo ipConfigInfo = ipPool.getIpv4Config();
            // Find associated VLAN Id
            int vlanId = getVlanId(dvPg);
            // Populate associated VMs
            SortedMap<String, VmwareVirtualMachineInfo> vmInfo = 
                    populateVirtualMachineInfo(dvPg);
            VmwareVirtualNetworkInfo vnInfo = new
                    VmwareVirtualNetworkInfo(vnName, vlanId, vmInfo,
                            ipConfigInfo.getSubnetAddress(),
                            ipConfigInfo.getNetmask(),
                            ipConfigInfo.getGateway());
            vnInfos.put(vnUuid, vnInfo);
        }
        return vnInfos;
    }   
}
