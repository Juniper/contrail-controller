/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
package net.juniper.contrail.api;

import java.util.List;
import java.util.UUID;

import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.Project;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualMachineInterface;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.types.VnSubnetsType.IpamSubnetType;
import junit.framework.TestCase;

import org.junit.Test;

import com.google.gson.Gson;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

public class ObjectReferenceTest extends TestCase {
    /**
     * API server requires the attr element in an ObjectReference to be present, even when null.
     */
    @Test
    public void testNullAttr() {
        VirtualMachineInterface vmi = new VirtualMachineInterface();
        VirtualNetwork vn = new VirtualNetwork();
        vn.setName("testnet");
        vn.setUuid(UUID.randomUUID().toString());
        vmi.setName("x-0");
        vmi.setVirtualNetwork(vn);
        String jsdata = ApiSerializer.serializeObject("virtual-machine-interface", vmi);
        assertNotSame(jsdata, -1, jsdata.indexOf("\"attr\":null"));
    }
    
    @Test
    public void testAttr() {
        VirtualNetwork vn = new VirtualNetwork();
        vn.setName("testnet");
        NetworkIpam ipam = new NetworkIpam();
        ipam.setName("testipam");
        VnSubnetsType subnets = new VnSubnetsType();
        subnets.addIpamSubnets(new VnSubnetsType.IpamSubnetType(new SubnetType("192.168.0.0", 24), "192.168.0.254", UUID.randomUUID().toString(), false, null, null, false, null, null, vn.getName() + "-subnet"));
        vn.setNetworkIpam(ipam, subnets);
        String jsdata = ApiSerializer.serializeObject("virtual-network", vn);
        assertNotSame(jsdata, -1, jsdata.indexOf("192.168.0.0"));
        
        final JsonParser parser = new JsonParser();
        final JsonObject js_obj = parser.parse(jsdata).getAsJsonObject();
        final JsonElement element = js_obj.get("virtual-network");
        VirtualNetwork result = (VirtualNetwork) ApiSerializer.deserialize(element.toString(), VirtualNetwork.class);
        List<IpamSubnetType> iplist = result.getNetworkIpam().get(0).attr.getIpamSubnets();
        assertSame(1, iplist.size());
        assertEquals("192.168.0.0", iplist.get(0).getSubnet().getIpPrefix());
    }
    @Test
    public void testVoidReference() {
        String voidref = "{\"network_policys\": [{\"to\": [\"default-domain\", \"testProject\", \"testPolicy\"], \"href\": \"http://localhost:53730/network-policy/4e4b0486-e56f-4bfe-8716-afc1a76ad106\", \"uuid\": \"4e4b0486-e56f-4bfe-8716-afc1a76ad106\"}]}";
        final JsonParser parser = new JsonParser();
        final JsonObject js_obj = parser.parse(voidref).getAsJsonObject();
        final JsonElement element = js_obj.get("network_policys");
        JsonArray items = element.getAsJsonArray();
        JsonElement item = items.get(0);
        Gson json = ApiSerializer.getDeserializer();
        ObjectReference<?> result = json.fromJson(item.toString(), ObjectReference.class);
	assertNotNull(result);
	assertEquals("testPolicy", result.to.get(2));
    }
    @Test
    /**
     * API generator adds an "s" at the end of the children name. Thus "network-policy" becomes "network-policys".
     */
    public void testVoidAttrType() {
        String content = "{\"project\": {\"network_policys\": [{\"to\": [\"default-domain\", \"testProject\", \"testPolicy\"], \"href\": \"http://localhost:53730/network-policy/4e4b0486-e56f-4bfe-8716-afc1a76ad106\", \"uuid\": \"4e4b0486-e56f-4bfe-8716-afc1a76ad106\"}], \"fq_name\": [\"default-domain\", \"testProject\"], \"uuid\": \"7a6580ac-d7dc-4363-a342-47a473a32884\"}}";
        final JsonParser parser = new JsonParser();
        final JsonObject js_obj = parser.parse(content).getAsJsonObject();
        final JsonElement element = js_obj.get("project");
        Project result = (Project) ApiSerializer.deserialize(element.toString(), Project.class);
        assertEquals("testProject", result.getName());
        assertNotNull(result.getNetworkPolicys());
    }
}
