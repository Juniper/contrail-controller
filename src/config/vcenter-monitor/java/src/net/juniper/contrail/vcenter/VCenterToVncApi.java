package net.juniper.contrail.vcenter;

import static com.vmware.vim.cf.NullObject.NULL;

import java.io.IOException;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.ListIterator;
import java.util.Map;
import java.util.UUID;

import org.apache.log4j.BasicConfigurator;
import org.apache.log4j.Logger;

import net.juniper.contrail.api.ApiConnector;
import net.juniper.contrail.api.ApiConnectorFactory;
import net.juniper.contrail.api.ApiPropertyBase;
import net.juniper.contrail.api.ObjectReference;
import net.juniper.contrail.api.types.InstanceIp;
import net.juniper.contrail.api.types.MacAddressesType;
import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualMachineInterface;
import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;

import com.vmware.vim.cf.CacheInstance;
import com.vmware.vim25.ArrayOfManagedObjectReference;
import com.vmware.vim25.DVPortgroupConfigInfo;
import com.vmware.vim25.DynamicProperty;
import com.vmware.vim25.IpPool;
import com.vmware.vim25.ManagedObjectReference;
import com.vmware.vim25.NetworkSummary;
import com.vmware.vim25.VMwareDVSPortSetting;
import com.vmware.vim25.VirtualDevice;
import com.vmware.vim25.VirtualE1000;
import com.vmware.vim25.VirtualEthernetCardDistributedVirtualPortBackingInfo;
import com.vmware.vim25.VirtualHardware;
import com.vmware.vim25.VirtualMachinePowerState;
import com.vmware.vim25.VirtualMachineSummary;
import com.vmware.vim25.VmwareDistributedVirtualSwitchPvlanSpec;
import com.vmware.vim25.VmwareDistributedVirtualSwitchVlanSpec;
import com.vmware.vim25.mo.Datacenter;
import com.vmware.vim25.mo.DistributedVirtualPortgroup;
import com.vmware.vim25.mo.DistributedVirtualSwitch;
import com.vmware.vim25.mo.Folder;
import com.vmware.vim25.mo.InventoryNavigator;
import com.vmware.vim25.mo.IpPoolManager;
import com.vmware.vim25.mo.ManagedEntity;
import com.vmware.vim25.mo.Network;
import com.vmware.vim25.mo.ServiceInstance;
import com.vmware.vim25.mo.VmwareDistributedVirtualSwitch;

import org.apache.commons.net.util.SubnetUtils;
import org.apache.commons.net.util.SubnetUtils.SubnetInfo;

class UserVNInfo {
	public int vlanId;
	public Map<String,String> vms;
	public IpPool ipp;
}

class VncApiVNInfo {
	public VnSubnetsType vst;
	// TODO: Need to move VirtualMachine outside VM to accomodate multiple interface VMs
	public Map<String,VirtualMachine> vms;
	public Map<String,VirtualMachineInterface> vmis;
}

public class VCenterToVncApi
{
  private static final Logger s_logger =
            Logger.getLogger(VCenterToVncApi.class);
  /**
   * Check VN returned from VncApi.
   * Delete if it is not in vCenter.
   *
   * @return whether the VncApi VN needs a deeper comparison with vCenter
   */  
  static boolean ProcessVncVN(Map<String, UserVNInfo> items, ApiConnector apic, VirtualNetwork lvn) throws IOException {
  	// TODO: Check for domain and project
  	String nm = lvn.getQualifiedName().get(2);
  	  	
  	if (nm.equals("__link_local__") || nm.equals("default-virtual-network") || nm.equals("ip-fabric")) {
  		s_logger.info("ignore " + nm);
		return false;
	}
	if (items.containsKey(nm)) {
		s_logger.info("vnc_api matched " + nm);
		return true;
	} else {
		s_logger.info("vnc_api not matched " + nm);
		DeleteVncVN(apic, lvn);
		return false;
	}	  
  }

  /**
   * Check VM returned from VncApi.
   * Delete if it is not in vCenter.
   *
   * @return whether the VncApi VM needs a deeper comparison with vCenter
   */  
  static boolean ProcessVncVMI(UserVNInfo uvi, ApiConnector apic, VirtualMachineInterface lvmi) throws IOException {
  	
  	String nm = lvmi.getQualifiedName().get(1);
  	String vmname = nm.substring(4);  	
    if (uvi.vms.containsKey(vmname)) {
    	s_logger.info("vnc_api matched vmi " + nm);
		return true;
	} else {
		s_logger.info("vnc_api not matched vmi " + nm);
		DeleteVncVMI(apic, lvmi);
		return false;
	}	  
  }
  
  static void DeleteVncVN(ApiConnector apic, VirtualNetwork lvn) throws IOException {
	
	s_logger.info("Delete VN: " + lvn.getName());
	apic.read(lvn);
	List<ObjectReference<ApiPropertyBase>> lvmis = lvn.getVirtualMachineInterfaceBackRefs();
	for (ObjectReference<ApiPropertyBase> vmiRef : lvmis) {
		VirtualMachineInterface vmi = (VirtualMachineInterface)apic.findById(VirtualMachineInterface.class, vmiRef.getUuid());
		DeleteVncVMI(apic, vmi);
	}
	apic.delete(VirtualNetwork.class, lvn.getUuid());	  
  }
  
  static void CreateVncVN(ApiConnector apic, String name, UserVNInfo uvi) throws IOException {
	VirtualNetwork vn = new VirtualNetwork();
	vn.setName(name);
	
	String ipam_id = apic.findByName(NetworkIpam.class, null, "default-network-ipam");
	NetworkIpam ipam = (NetworkIpam)apic.findById(NetworkIpam.class, ipam_id);
	
	SubnetUtils sus = new SubnetUtils(uvi.ipp.ipv4Config.getSubnetAddress(), uvi.ipp.ipv4Config.getNetmask());	
	String cidr = sus.getInfo().getCidrSignature();
	
	VnSubnetsType subnet = new VnSubnetsType();
	String[] addr_pair = cidr.split("\\/");
	subnet.addIpamSubnets(new SubnetType(addr_pair[0], Integer.parseInt(addr_pair[1])), uvi.ipp.ipv4Config.getGateway(),
            UUID.randomUUID().toString());
	
	vn.setNetworkIpam(ipam, subnet);
	apic.create(vn); 
	
    Iterator it = uvi.vms.entrySet().iterator();
    while (it.hasNext()) {
        Map.Entry pairs = (Map.Entry)it.next();
        String vmn = (String) pairs.getKey();
        String mac = (String) pairs.getValue();
        
		CreateVncVM(apic, vmn, mac, vn);
		s_logger.info("Create vm: " + vmn + " mac: " + mac);
	}	
  }
  
  static void DeleteVncVMI(ApiConnector apic, VirtualMachineInterface vmi) throws IOException {
	  
	  // TODO : UnPlug VM Notification
	  
	  List<ObjectReference<ApiPropertyBase>> lips = vmi.getInstanceIpBackRefs();
	  if (lips != null) {
		  for (ObjectReference<ApiPropertyBase> instIp : lips) {
			  s_logger.info("Delete IP: " + instIp.getReferredName());
			  apic.delete(InstanceIp.class, instIp.getUuid());
		  }
	  }
	  
	  // There should only be one VM hanging off the VMI
	  String svm = vmi.getParentUuid();	
	  VirtualMachine vm = (VirtualMachine)apic.findById(VirtualMachine.class, svm);
	  boolean del_vm = false;
	  if (vm.getVirtualMachineInterfaces().size()==1) {
		  del_vm = true;
	  }
	  
	  s_logger.info("Delete VMI: " + vmi.getName());
	  apic.delete(VirtualMachineInterface.class, vmi.getUuid());
	  
	  
	  if (del_vm) {
		  s_logger.info("Delete VM: " + vm.getName());
	      apic.delete(VirtualMachine.class, vm.getUuid());	  
	  }
  }
  
  static void CreateVncVM(ApiConnector apic, String vmname, String mac, VirtualNetwork vn) throws IOException {
	  VirtualMachine vm = new VirtualMachine();
	  vm.setName(vmname);
	  apic.create(vm);
	  
	  VirtualMachineInterface vmi = new VirtualMachineInterface();
	  vmi.setParent(vm);
	  vmi.setName("vmi-" + vmname);
	  vmi.setVirtualNetwork(vn);
	  
	  MacAddressesType maca = new MacAddressesType();
	  maca.addMacAddress(mac);
	  vmi.setMacAddresses(maca);
	  
	  apic.create(vmi);
	
	  InstanceIp ip_obj = new InstanceIp();
	  ip_obj.setName("ip-"+ vmname);
	  ip_obj.setVirtualNetwork(vn);
	  ip_obj.setVirtualMachineInterface(vmi);
	  apic.create(ip_obj);
	  
	  // TODO: Plug Notification
  }
  
  public static void main(String[] args) throws Exception
  {
	final String kDvSwitch = "dvSwitch";
	final String kDataCenter = "Datacenter";
	final String kVncIp = "10.84.13.40";
	
	ServiceInstance si = new ServiceInstance(new URL("https://10.84.24.111/sdk"), "admin", "Contrail123!", true);
    Folder rootFolder = si.getRootFolder();
    ManagedEntity[] vms = new InventoryNavigator(rootFolder).searchManagedEntities("VirtualMachine");
    
    IpPoolManager ipm = si.getIpPoolManager();
    Datacenter dc = (Datacenter) new InventoryNavigator(rootFolder).searchManagedEntity("Datacenter", kDataCenter);
    IpPool[] ipa = ipm.queryIpPools(dc);
    
    for(int ii=0; ii<vms.length; ii++) {
    	com.vmware.vim25.mo.VirtualMachine vx = (com.vmware.vim25.mo.VirtualMachine) vms[ii];
    	Network[] nett = vx.getNetworks();
    	if ((nett.length == 1) && (nett[0] instanceof DistributedVirtualPortgroup)) {
    	    System.out.println("vm: " + vx.getName() + " nets: " + nett.length);
    	}
    }
    
    Map<String, UserVNInfo> items = new HashMap<String,UserVNInfo>();
    
    VmwareDistributedVirtualSwitch dvs = (VmwareDistributedVirtualSwitch) new InventoryNavigator(rootFolder).searchManagedEntity("VmwareDistributedVirtualSwitch", kDvSwitch);
    DistributedVirtualPortgroup[] dvpgs = dvs.getPortgroup();
    for(int jj=0; jj<dvpgs.length; jj++) {
    	DVPortgroupConfigInfo dci = dvpgs[jj].getConfig();
    	NetworkSummary ns = dvpgs[jj].getSummary();
    	Integer poolid = ns.getIpPoolId();
    	VMwareDVSPortSetting vdvs = (VMwareDVSPortSetting)dci.getDefaultPortConfig();
    	VmwareDistributedVirtualSwitchVlanSpec svs = vdvs.getVlan();
    	if ((svs instanceof VmwareDistributedVirtualSwitchPvlanSpec) && (poolid != null)){
    		UserVNInfo uvi = new UserVNInfo();
    		uvi.ipp = null;
        	for (int pp=0;pp<ipa.length; pp++) {
        		if ( ipa[pp].id == poolid.intValue()) {
        			uvi.ipp = ipa[pp];
        		}
        	}    		
        	if (uvi.ipp == null) continue;
        	
    		VmwareDistributedVirtualSwitchPvlanSpec psvs = (VmwareDistributedVirtualSwitchPvlanSpec) svs;
            System.out.println("dvpg: " + dvpgs[jj].getName() + " vlan: " + psvs.getPvlanId() + " subnet: " + uvi.ipp.getIpv4Config().getSubnetAddress());
            uvi.vlanId = psvs.getPvlanId();
            
            com.vmware.vim25.mo.VirtualMachine[] nvms = dvpgs[jj].getVms();
            HashMap<String,String> avms = new HashMap<String,String>();
            for (int kk=0; kk<nvms.length; kk++) {
            	
            	String vmac = null;
            	VirtualDevice vhs[] = nvms[kk].getConfig().getHardware().getDevice();
            	for (VirtualDevice vd : vhs) {
            		// TODO: Assuming only one interface
            		if (vd instanceof VirtualE1000) {
            			VirtualEthernetCardDistributedVirtualPortBackingInfo vebi = (VirtualEthernetCardDistributedVirtualPortBackingInfo)vd.getBacking();
            			if (vebi.getPort().getPortgroupKey().equals(dvpgs[jj].getKey())) {
            				vmac = ((VirtualE1000) vd).getMacAddress();
            			}
            		}
            	}
            	
            	if (nvms[kk].getRuntime().getPowerState() == VirtualMachinePowerState.poweredOn) {
            		System.out.println("  vm: " + nvms[kk].getName() + " state: " + nvms[kk].getRuntime().getPowerState() + " mac: " + vmac);
            		avms.put(nvms[kk].getName(), vmac);
            	}
            }
            
            uvi.vms = avms;
            
            items.put(dvpgs[jj].getName(), uvi);
    	}
    }
    BasicConfigurator.configure();
    
    ApiConnector apic = ApiConnectorFactory.build(kVncIp,8082);
    List<VirtualNetwork> lvn = (List<VirtualNetwork>) apic.list(VirtualNetwork.class, null);
    System.out.println("vnc_api VNs: " + lvn.size());
    
    // Delete the VncAPI VNs are are not present in the vCenter
    // Build the "vitems" list of VncAPI VNs that are also present in vCenter
    Map<String, VncApiVNInfo> vitems = new HashMap<String,VncApiVNInfo>();
    for (ListIterator<VirtualNetwork> iter = lvn.listIterator(lvn.size()); iter.hasPrevious();) {
    	VirtualNetwork ivn = iter.previous();
    	apic.read(ivn);
    	
    	List<ObjectReference<VnSubnetsType>> lsn = ivn.getNetworkIpam();
    	VnSubnetsType vnSubnetType = null;
    	if (lsn != null) {
    	   vnSubnetType = lsn.get(0).getAttr();
    	}
    	
    	List<ObjectReference<ApiPropertyBase>> lr = ivn.getVirtualMachineInterfaceBackRefs();
    	
    	HashMap<String,VirtualMachine> mvm = new HashMap<String,VirtualMachine>();
    	HashMap<String,VirtualMachineInterface> mvmi = new HashMap<String,VirtualMachineInterface>();
    	if (lr != null) {
	        for (ObjectReference<ApiPropertyBase> vmiRef : lr) {
	            VirtualMachineInterface vmi = (VirtualMachineInterface)apic.findById(VirtualMachineInterface.class, vmiRef.getUuid());
	            apic.read(vmi);
	            String vmuuid = vmi.getParentUuid();
	            net.juniper.contrail.api.types.VirtualMachine avm = (VirtualMachine)apic.findById(VirtualMachine.class, vmuuid);
	            apic.read(avm);
	            mvm.put(avm.getDisplayName(), avm);
	            mvmi.put(vmi.getName(), vmi);
	        }    	
    	}
        if (ProcessVncVN(items, apic, ivn)) {
        	VncApiVNInfo vai = new VncApiVNInfo();
        	vai.vms = mvm;
        	vai.vmis = mvmi;
        	vai.vst = vnSubnetType;
        	vitems.put(ivn.getQualifiedName().get(2), vai);
        }
    }
    
    // The "items" list contains vCenter VNs that need to get reflected in the VncAPI 
    Iterator it = items.entrySet().iterator();
    while (it.hasNext()) {
        Map.Entry pairs = (Map.Entry)it.next();
        String name = (String) pairs.getKey();
        UserVNInfo uvi = (UserVNInfo) pairs.getValue();
        if (vitems.containsKey(name)) {
        	
        	// The VN matched
        	// We need to compare vCenter VN with VncApi and update VncApi
        	
        	Map<String,VirtualMachineInterface> vmis = vitems.get(name).vmis;
        	Iterator vit =  vmis.entrySet().iterator();
        	while (vit.hasNext()) {
        		Map.Entry vpairs = (Map.Entry)vit.next();
                String iname = (String) vpairs.getKey();
                VirtualMachineInterface vmi = (VirtualMachineInterface) vpairs.getValue();  
                if (!ProcessVncVMI(uvi,apic,vmi)) {
                	vit.remove();
                }
        	}
        	
        	// We need the VirtualNetwork object to create VMs
        	List<String> vns = new ArrayList<String>(3);
        	vns.add(0,"default-domain");
        	vns.add(1,"default-project");
        	vns.add(2,name);
        	String vnid = apic.findByName(VirtualNetwork.class, vns);
        	VirtualNetwork fvn = (VirtualNetwork) apic.findById(VirtualNetwork.class, vnid);
        	apic.read(fvn);
        	
        	Iterator vt = uvi.vms.entrySet().iterator();
        	while (vt.hasNext()) {
        		Map.Entry tpairs = (Map.Entry)vt.next();
        		String vcmname = (String) tpairs.getKey();
        		String mac = (String) tpairs.getValue();
        		if (vmis.containsKey("vmi-"+vcmname)) {
        			// The VM matched. Not action is needed
        		} else {
        			CreateVncVM(apic, vcmname, mac, fvn);
        		}
        	}
        	
        } else {
        	CreateVncVN(apic, name, uvi);
        }
    }
  }
}
