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

import net.juniper.contrail.api.ApiConnector;
import net.juniper.contrail.api.ApiConnectorFactory;
import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;

import com.vmware.vim.cf.CacheInstance;
import com.vmware.vim25.ArrayOfManagedObjectReference;
import com.vmware.vim25.DVPortgroupConfigInfo;
import com.vmware.vim25.IpPool;
import com.vmware.vim25.ManagedObjectReference;
import com.vmware.vim25.NetworkSummary;
import com.vmware.vim25.VMwareDVSPortSetting;
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
import com.vmware.vim25.mo.VirtualMachine;
import com.vmware.vim25.mo.VmwareDistributedVirtualSwitch;

import org.apache.commons.net.util.SubnetUtils;
import org.apache.commons.net.util.SubnetUtils.SubnetInfo;

class UserVNInfo {
	public int vlanId;
	public String[] vms;
	public IpPool ipp;
}

public class VCenterToVncApi
{
  /**
   * Check VN returned from VncApi.
   * Delete if it is not in vCenter.
   *
   * @return whether the VncApi VN needs a deeper comparison with vCenter
   */  
  static boolean ProcessVncVN(Map<String, UserVNInfo> items, ApiConnector apic, VirtualNetwork lvn) throws IOException {
  	// TODO: Check for domain and project
  	String nm = lvn.getQualifiedName().get(2);
  	
  	List<String> matched_vn;
  	
  	if (nm.equals("__link_local__") || nm.equals("default-virtual-network") || nm.equals("ip-fabric")) {
  		System.out.println("ignore " + nm);
		return false;
	}
	if (items.containsKey(nm)) {
		System.out.println("vnc_api matched " + nm);
		return true;
	} else {
		System.out.println("vnc_api not matched " + nm);
		apic.delete(VirtualNetwork.class, lvn.getUuid());
		return false;
	}	  
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
	
	// TODO: Create VMI/VM
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
    	VirtualMachine vx = (VirtualMachine) vms[ii];
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
            
            VirtualMachine[] nvms = dvpgs[jj].getVms();
            ArrayList<String> avms = new ArrayList();
            for (int kk=0; kk<nvms.length; kk++) {
            	if (nvms[kk].getRuntime().getPowerState() == VirtualMachinePowerState.poweredOn) {
            		System.out.println("  vm: " + nvms[kk].getName() + " state: " + nvms[kk].getRuntime().getPowerState());
            		avms.add(nvms[kk].getName());
            	}
            }
            
            String[] ss = new String[avms.size()];
            uvi.vms = (String[]) avms.toArray(ss);
            
            items.put(dvpgs[jj].getName(), uvi);
    	}
    }
    BasicConfigurator.configure();
    
    ApiConnector apic = ApiConnectorFactory.build(kVncIp,8082);
    List<VirtualNetwork> lvn = (List<VirtualNetwork>) apic.list(VirtualNetwork.class, null);
    System.out.println("vnc_api VNs: " + lvn.size());
    
    Map<String, VirtualNetwork> vitems = new HashMap<String,VirtualNetwork>();
    for (ListIterator<VirtualNetwork> iter = lvn.listIterator(lvn.size()); iter.hasPrevious();) {
    	VirtualNetwork ivn = iter.previous();
        if (ProcessVncVN(items, apic, ivn)) {
        	vitems.put(ivn.getQualifiedName().get(2), ivn);
        }
    }
    
    Iterator it = items.entrySet().iterator();
    while (it.hasNext()) {
        Map.Entry pairs = (Map.Entry)it.next();
        String name = (String) pairs.getKey();
        UserVNInfo uvi = (UserVNInfo) pairs.getValue();
        if (vitems.containsKey(name)) {
        	// TODO: the VN matched
        	// We need to compare vCenter VN with VncApi and update VncApi
        } else {
        	CreateVncVN(apic, name, uvi);
        }
    }
  }
}
