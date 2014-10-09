/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.vcenter;

import java.io.IOException;
import java.io.File;
import java.io.FileInputStream;
import java.util.Map;
import java.util.Map.Entry;
import java.util.SortedMap;
import java.util.Iterator;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.UUID;

import org.apache.log4j.BasicConfigurator;
import org.apache.log4j.Logger;

import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualMachineInterface;

class VmwareVirtualMachineInfo {
    private String hostName;
    private String vrouterIpAddress;
    private String macAddress;
    private String name;
    
    public VmwareVirtualMachineInfo(String name, String hostName, 
            String vrouterIpAddress, String macAddress) {
        this.name = name;
        this.hostName = hostName;
        this.vrouterIpAddress = vrouterIpAddress;
        this.macAddress = macAddress;
    }

    public String getHostName() {
        return hostName;
    }

    public void setHostName(String hostName) {
        this.hostName = hostName;
    }

    public String getVrouterIpAddress() {
        return vrouterIpAddress;
    }

    public void setVrouterIpAddress(String vrouterIpAddress) {
        this.vrouterIpAddress = vrouterIpAddress;
    }

    public String getMacAddress() {
        return macAddress;
    }

    public void setMacAddress(String macAddress) {
        this.macAddress = macAddress;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }
}

class VmwareVirtualNetworkInfo {
    private String name;
    private short isolatedVlanId;
    private short primaryVlanId;
    private SortedMap<String, VmwareVirtualMachineInfo> vmInfo;
    private String subnetAddress;
    private String subnetMask;
    private String gatewayAddress;
    
    public VmwareVirtualNetworkInfo(String name, short isolatedVlanId,
            short primaryVlanId, SortedMap<String, VmwareVirtualMachineInfo> vmInfo,
            String subnetAddress, String subnetMask, String gatewayAddress) {
        this.name = name;
        this.isolatedVlanId = isolatedVlanId;
        this.primaryVlanId = primaryVlanId;
        this.vmInfo = vmInfo;
        this.subnetAddress = subnetAddress;
        this.subnetMask = subnetMask;
        this.gatewayAddress = gatewayAddress;
    }
    
    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public short getIsolatedVlanId() {
        return isolatedVlanId;
    }

    public void setIsolatedVlanId(short vlanId) {
        this.isolatedVlanId = vlanId;
    }

    public short getPrimaryVlanId() {
        return primaryVlanId;
    }

    public void setPrimaryVlanId(short vlanId) {
        this.primaryVlanId = vlanId;
    }

    public SortedMap<String, VmwareVirtualMachineInfo> getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(SortedMap<String, VmwareVirtualMachineInfo> vmInfo) {
        this.vmInfo = vmInfo;
    }

    public String getSubnetAddress() {
        return subnetAddress;
    }

    public void setSubnetAddress(String subnetAddress) {
        this.subnetAddress = subnetAddress;
    }

    public String getSubnetMask() {
        return subnetMask;
    }

    public void setSubnetMask(String subnetMask) {
        this.subnetMask = subnetMask;
    }

    public String getGatewayAddress() {
        return gatewayAddress;
    }

    public void setGatewayAddress(String gatewayAddress) {
        this.gatewayAddress = gatewayAddress;
    }
}

class VncVirtualMachineInfo {
    private VirtualMachine vmInfo;
    private VirtualMachineInterface vmInterfaceInfo;
    
    public VncVirtualMachineInfo(VirtualMachine vmInfo,
            VirtualMachineInterface vmInterfaceInfo) {
        this.vmInfo = vmInfo;
        this.vmInterfaceInfo = vmInterfaceInfo;
    }

    public VirtualMachine getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(VirtualMachine vmInfo) {
        this.vmInfo = vmInfo;
    }

    public VirtualMachineInterface getVmInterfaceInfo() {
        return vmInterfaceInfo;
    }

    public void setVmInterfaceInfo(VirtualMachineInterface vmInterfaceInfo) {
        this.vmInterfaceInfo = vmInterfaceInfo;
    }
}

class VncVirtualNetworkInfo {
    private String name;
    private SortedMap<String, VncVirtualMachineInfo> vmInfo;
    
    public VncVirtualNetworkInfo(String name,
            SortedMap<String, VncVirtualMachineInfo> vmInfo) {
        this.name = name;
        this.vmInfo = vmInfo;
    }

    public String getName() {
        return name;
    }
    
    public void setName(String name) {
        this.name = name;
    }
    
    public SortedMap<String, VncVirtualMachineInfo> getVmInfo() {
        return vmInfo;
    }

    public void setVmInfo(SortedMap<String, VncVirtualMachineInfo> vmInfo) {
        this.vmInfo = vmInfo;
    }
}

class VCenterMonitorTask implements Runnable {
    private static Logger s_logger = Logger.getLogger(VCenterMonitorTask.class);
    private VCenterDB vcenterDB;
    private VncDB vncDB;
    
    public VCenterMonitorTask(String vcenterURL, String vcenterUsername,
            String vcenterPassword, String apiServerIpAddress, 
            int apiServerPort) throws Exception {
        vcenterDB = new VCenterDB(vcenterURL, vcenterUsername,
                vcenterPassword);
        vncDB = new VncDB(apiServerIpAddress, apiServerPort);
        // Initialize the databases
        vcenterDB.Initialize();
        vncDB.Initialize();
    }
    
    private void syncVirtualMachines(String vnUuid, 
            VmwareVirtualNetworkInfo vmwareNetworkInfo,
            VncVirtualNetworkInfo vncNetworkInfo) throws IOException {
        String vncVnName = vncNetworkInfo.getName();
        String vmwareVnName = vmwareNetworkInfo.getName();
        s_logger.info("Syncing virtual machines in network: " + vnUuid + 
                " across Vnc(" + vncVnName + ") and VCenter(" +
                vmwareVnName + ") DBs");
        SortedMap<String, VmwareVirtualMachineInfo> vmwareVmInfos =
                vmwareNetworkInfo.getVmInfo();
        SortedMap<String, VncVirtualMachineInfo> vncVmInfos =
                vncNetworkInfo.getVmInfo();
        Iterator<Entry<String, VmwareVirtualMachineInfo>> vmwareIter = 
                (vmwareVmInfos != null ? vmwareVmInfos.entrySet().iterator() : null);
        Iterator<Entry<String, VncVirtualMachineInfo>> vncIter =
                (vncVmInfos != null ? vncVmInfos.entrySet().iterator() : null);
        s_logger.info("VMs vmware size: " + ((vmwareVmInfos != null) ? vmwareVmInfos.size():0) + ", vnc size: " + 
                                                                 ((vncVmInfos != null) ? vncVmInfos.size():0));
        Map.Entry<String, VmwareVirtualMachineInfo> vmwareItem = null;
        if (vmwareIter != null && vmwareIter.hasNext()) {
                vmwareItem = (Entry<String, VmwareVirtualMachineInfo>)vmwareIter.next();
        } 
        Map.Entry<String, VncVirtualMachineInfo> vncItem = null;
        if (vncIter != null && vncIter.hasNext()) {
                vncItem = (Entry<String, VncVirtualMachineInfo>)vncIter.next();
        }
        
        while (vmwareItem != null && vncItem != null) {
            // Do Vmware and Vnc virtual machines match?
            String vmwareVmUuid = vmwareItem.getKey();
            String vncVmUuid = vncItem.getKey();
            Integer cmp = vmwareVmUuid.compareTo(vncVmUuid);

            if (cmp == 0) {
                // Match found, advance Vmware and Vnc iters
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
                vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
            } else if (cmp > 0){
                // Delete Vnc virtual machine
                vncDB.DeleteVirtualMachine(vncItem.getValue());
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
            } else if (cmp < 0){
                // create VMWare virtual machine in VNC
                VmwareVirtualMachineInfo vmwareVmInfo = vmwareItem.getValue();
                vncDB.CreateVirtualMachine(vnUuid, vmwareVmUuid,
                        vmwareVmInfo.getMacAddress(),
                        vmwareVmInfo.getName(),
                        vmwareVmInfo.getVrouterIpAddress(),
                        vmwareVmInfo.getHostName(), 
                        vmwareNetworkInfo.getIsolatedVlanId(),
                        vmwareNetworkInfo.getPrimaryVlanId());
                vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
            }
        }       
        while (vmwareItem != null) {
            // Create
            String vmwareVmUuid = vmwareItem.getKey();
            VmwareVirtualMachineInfo vmwareVmInfo = vmwareItem.getValue();
            vncDB.CreateVirtualMachine(vnUuid, vmwareVmUuid,
                    vmwareVmInfo.getMacAddress(),
                    vmwareVmInfo.getName(),
                    vmwareVmInfo.getVrouterIpAddress(),
                    vmwareVmInfo.getHostName(), 
                    vmwareNetworkInfo.getIsolatedVlanId(),
                    vmwareNetworkInfo.getPrimaryVlanId());
            vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
        }
        while (vncItem != null) {
            // Delete
            vncDB.DeleteVirtualMachine(vncItem.getValue());
            vncItem = vncIter.hasNext() ? vncIter.next() : null;
        }        
    }
    
    private void syncVirtualNetworks() throws Exception {
        s_logger.info("Syncing Vnc and VCenter DBs");
        SortedMap<String, VmwareVirtualNetworkInfo> vmwareVirtualNetworkInfos =
                vcenterDB.populateVirtualNetworkInfo();
        SortedMap<String, VncVirtualNetworkInfo> vncVirtualNetworkInfos =
                vncDB.populateVirtualNetworkInfo();
        s_logger.info("VNs vmware size: " + vmwareVirtualNetworkInfos.size() + ", vnc size: " + vncVirtualNetworkInfos.size());

        Iterator<Entry<String, VmwareVirtualNetworkInfo>> vmwareIter = null;
        if (vmwareVirtualNetworkInfos != null && vmwareVirtualNetworkInfos.size() > 0 && vmwareVirtualNetworkInfos.entrySet() != null) {
            vmwareIter = vmwareVirtualNetworkInfos.entrySet().iterator();
        }
        Map.Entry<String, VmwareVirtualNetworkInfo> vmwareItem = null;
        if (vmwareIter != null) { 
                vmwareItem = (Entry<String, VmwareVirtualNetworkInfo>) 
                (vmwareIter.hasNext() ? vmwareIter.next() : null);
        }

        Iterator<Entry<String, VncVirtualNetworkInfo>> vncIter = null;
        if (vncVirtualNetworkInfos != null && vncVirtualNetworkInfos.size() > 0 && vncVirtualNetworkInfos.entrySet() != null) {
                vncIter = vncVirtualNetworkInfos.entrySet().iterator();
        }
        Map.Entry<String, VncVirtualNetworkInfo> vncItem = null;
        if (vncIter != null) { 
                vncItem = (Entry<String, VncVirtualNetworkInfo>) 
                (vncIter.hasNext() ? vncIter.next() : null);
        }


        while (vmwareItem != null && vncItem != null) {
            // Do Vmware and Vnc networks match?
            String vmwareVnUuid = vmwareItem.getKey();
            String vncVnUuid = vncItem.getKey();
            if (!vmwareVnUuid.equals(vncVnUuid)) {
                // Delete Vnc network
                vncDB.DeleteVirtualNetwork(vncVnUuid);
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
            } else {
                // Sync
                syncVirtualMachines(vncVnUuid, vmwareItem.getValue(),
                        vncItem.getValue());
                // Advance
                vncItem = vncIter.hasNext() ? vncIter.next() : null;
                vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
            }
        }
        while (vmwareItem != null) {
            // Create
            String vmwareVnUuid = vmwareItem.getKey();
            VmwareVirtualNetworkInfo vnInfo = vmwareItem.getValue();
            SortedMap<String, VmwareVirtualMachineInfo> vmInfos = vnInfo.getVmInfo();
            String subnetAddr = vnInfo.getSubnetAddress();
            String subnetMask = vnInfo.getSubnetMask();
            String gatewayAddr = vnInfo.getGatewayAddress();
            String vmwareVnName = vnInfo.getName();
            short isolatedVlanId = vnInfo.getIsolatedVlanId();
            short primaryVlanId = vnInfo.getPrimaryVlanId();
            vncDB.CreateVirtualNetwork(vmwareVnUuid, vmwareVnName, subnetAddr,
                    subnetMask, gatewayAddr, isolatedVlanId, primaryVlanId, vmInfos);
            vmwareItem = vmwareIter.hasNext() ? vmwareIter.next() : null;
        }
        while (vncItem != null) {
            // Delete
            vncDB.DeleteVirtualNetwork(vncItem.getKey());
            vncItem = vncIter.hasNext() ? vncIter.next() : null;
        }
    }
    
    @Override
    public void run() {
        try {
            syncVirtualNetworks();
        } catch (Exception e) {
            s_logger.error("Error while syncVirtualNetworks: " + e); 
            e.printStackTrace();
        }
    }
}

class ExecutorServiceShutdownThread extends Thread {
    private static final long timeoutValue = 60;
    private static final TimeUnit timeoutUnit = TimeUnit.SECONDS;
    private static Logger s_logger = Logger.getLogger(ExecutorServiceShutdownThread.class);
    private ExecutorService es;
        
    public ExecutorServiceShutdownThread(ExecutorService es) {
        this.es = es;
    }
    
    @Override
    public void run() {
        es.shutdown();
        try {
            if (!es.awaitTermination(timeoutValue, timeoutUnit)) {
                es.shutdownNow();
                if (!es.awaitTermination(timeoutValue, timeoutUnit)) {
                    s_logger.error("ExecutorSevice: " + es + 
                            " did NOT terminate");
                }
            }
        } catch (InterruptedException e) {
            s_logger.info("ExecutorServiceShutdownThread: " + 
                Thread.currentThread() + " ExecutorService: " + e + 
                " interrupted : " + e);
        }
        
    }
}

public class VCenterMonitor {
    private static ScheduledExecutorService scheduledTaskExecutor = 
            Executors.newScheduledThreadPool(1);
    private static Logger s_logger = Logger.getLogger(VCenterMonitor.class);
    private static String _configurationFile = "vcenter_monitor.properties";
    private static String _vcenterURL = "https://10.84.24.111/sdk";
    private static String _vcenterUsername = "admin";
    private static String _vcenterPassword = "Contrail123!";
    private static String _apiServerAddress = "10.84.13.23";
    private static int _apiServerPort = 8082;
    
    private static boolean configure() {

        File configFile = new File(_configurationFile);
        FileInputStream fileStream = null;
        try {
            String hostname = null;
            int port = 0;
            if (configFile == null) {
                return false;
            } else {
                final Properties configProps = new Properties();
                fileStream = new FileInputStream(configFile);
                configProps.load(fileStream);

                _vcenterURL = configProps.getProperty("vcenter.url");
                _vcenterUsername = configProps.getProperty("vcenter.username");
                _vcenterPassword = configProps.getProperty("vcenter.password");
                _apiServerAddress = configProps.getProperty("api.hostname");
                String portStr = configProps.getProperty("api.port");
                if (portStr != null && portStr.length() > 0) {
                    _apiServerPort = Integer.parseInt(portStr);
                }
            }
        } catch (IOException ex) {
            s_logger.warn("Unable to read " + _configurationFile, ex);
        } catch (Exception ex) {
            s_logger.debug("Exception in configure: " + ex);
            ex.printStackTrace();
        } finally {
            //IOUtils.closeQuietly(fileStream);
        }
        return true;
    }

    public static void main(String[] args) throws Exception {
        BasicConfigurator.configure();
        configure();
        s_logger.debug("Config params  vcenter url: " + _vcenterURL + ", _vcenterUsername: " + _vcenterUsername + ", api server: " + _apiServerAddress);
        // Launch the periodic VCenterMonitorTask
        VCenterMonitorTask monitorTask = new VCenterMonitorTask(_vcenterURL, 
                _vcenterUsername, _vcenterPassword, _apiServerAddress,
                _apiServerPort);
        scheduledTaskExecutor.scheduleWithFixedDelay(monitorTask, 0, 30,
                TimeUnit.SECONDS);
        Runtime.getRuntime().addShutdownHook(
                new ExecutorServiceShutdownThread(scheduledTaskExecutor));
    }
}
