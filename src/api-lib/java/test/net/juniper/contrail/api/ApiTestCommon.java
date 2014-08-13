/*
 *  * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 *   */
package net.juniper.contrail.api;

import static org.junit.Assert.*;

import java.net.ServerSocket;
import java.io.IOException;
import java.util.List;
import java.util.ArrayList;
import java.util.UUID;
import java.io.File;

import net.juniper.contrail.api.ApiConnectorFactory;
import net.juniper.contrail.api.ApiObjectBase;
import net.juniper.contrail.api.types.InstanceIp;
import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualMachineInterface;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.types.NetworkPolicy;
import net.juniper.contrail.api.types.Project;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.ApiConnector;

import org.apache.log4j.Logger;
import org.apache.commons.exec.DefaultExecutor;
import org.apache.commons.exec.CommandLine;
import org.apache.commons.exec.PumpStreamHandler;
import org.apache.commons.exec.ExecuteResultHandler;
import java.io.ByteArrayOutputStream;

public class ApiTestCommon {
    public static ApiConnector _api;
    private static final Logger s_logger =
        Logger.getLogger(ApiTestCommon.class);

    ApiTestCommon(ApiConnector api) {
        _api = api;
    }
        
    public static int findFreePort() throws Exception {
        int port;
        ServerSocket socket= new ServerSocket(0);
        port = socket.getLocalPort();
        socket.close(); 
        return port;    
    }

    public static CommandLine buildServerLaunchCmd(int port) {
        CommandLine cmdLine = new CommandLine("fab");
        cmdLine.addArgument("-f");
        cmdLine.addArgument("fab_tasks.py");
        cmdLine.addArgument("run_api_srv:listen_port=" + port);
        return cmdLine;
    }

    public static void launchContrailServer(int port) throws Exception {
        try {
            DefaultExecutor exec = new DefaultExecutor();
            int exitValues[] = {1};
            exec.setExitValues(exitValues);
             
            String workingDir = System.getProperty("user.dir");
            String path = workingDir + "/../../config/api-server/tests/";
            File f = new File(path);
            exec.setWorkingDirectory(f);
            exec.setStreamHandler(new PumpStreamHandler(new ByteArrayOutputStream()));
            CommandLine cmd = buildServerLaunchCmd(port);
            ExecuteResultHandler handler = null;
            exec.execute(cmd, handler);
            /* sleep 5 seconds for server to get started */
            Thread.sleep(5000);
        } catch (Exception e) {
            s_logger.debug(e);
            String cause = e.getMessage();
            if (cause.equals("python: not found"))
                System.out.println("No python interpreter found.");
            throw e; 
        }
    }

    public void setUp() throws Exception {

        String  hostname = "localhost";
        int     port = findFreePort();
        launchContrailServer(port);
        s_logger.debug("test api server launched <" + hostname + ", " + port + ">");
    }

    public void tearDown() throws Exception {
    }

    public void testNetwork() {
        String uuid1 = UUID.randomUUID().toString();
        VirtualNetwork net1 = new VirtualNetwork();
        net1.setName("test-network");
        net1.setUuid(uuid1);
        try {
            s_logger.info("create '<name=test-network, uuid=" +
                    uuid1 + ">' Virtual Network");
            assertTrue(_api.create(net1));
        } catch (IOException ex) {
            s_logger.warn("create test-network io exception " + ex.getMessage());
            fail(ex.getMessage());
        } catch (Exception ex) {
            s_logger.warn("create test-network http exception " + ex.getMessage());
            fail(ex.getMessage());
        }
                
        VirtualNetwork net2 = new VirtualNetwork();
        net2.setName("srv-id-assign");
        try {
            s_logger.info("create '<name=srv-id-assign, uuid=empty" +
                    ">' Virtual Network");
            assertTrue(_api.create(net2));
        } catch (IOException ex) {
            s_logger.warn("create srv-id-assign exception " + ex.getMessage());
            fail(ex.getMessage());
        }
                
        assertNotNull(net2.getUuid());
        ApiObjectBase net3 = null;
        try {
            net3 = _api.findById(VirtualNetwork.class, net2.getUuid());
            assertNotNull(net3);
        } catch (IOException ex) {
            fail(ex.getMessage());
        }
        assertEquals(net2.getUuid(), net3.getUuid());

        try {
            String uuid_1 = _api.findByName(VirtualNetwork.class, null, "test-network");
            assertNotNull(uuid_1);
            assertEquals(net1.getUuid(), uuid_1);
                
            List<? extends ApiObjectBase> list = _api.list(VirtualNetwork.class, null);
            assertNotNull(list);
            assertTrue(list.size() >= 2);
        } catch (IOException ex) {
            fail(ex.getMessage());
        }

        try {
            s_logger.info("delete '<name=test-network, uuid=" +
                    uuid1 + ">' Virtual Network");
            _api.delete(net1);
            s_logger.info("delete '<name=srv-id-assign, uuid=" +
                    net2.getUuid() + ">' Virtual Network");
            _api.delete(net2);
        } catch (IOException ex) {
            fail(ex.getMessage());
        }
    }
   
    public void testDeserializeReferenceList() {
        Project project = new Project();
        project.setName("testProject");
        project.setUuid(UUID.randomUUID().toString());
        try {
            assertTrue(_api.create(project));
        } catch (IOException ex) {
            fail(ex.getMessage());
        }
        NetworkPolicy policy = new NetworkPolicy();
        policy.setParent(project);
        policy.setName("testPolicy");
        try {
            assertTrue(_api.create(policy));
        } catch (IOException ex) {
            fail(ex.getMessage());
        }
        try {
            assertTrue(_api.read(project));
        } catch (IOException ex) {
            fail(ex.getMessage());
        }
        
        List<ObjectReference<ApiPropertyBase>> policyList = project.getNetworkPolicys();
        assertTrue(policyList != null);  
        assertTrue(policyList.size() != 0);  
    }

    public void testAddressAllocation() {
        VirtualNetwork net = new VirtualNetwork();
        net.setName("test");
        net.setUuid(UUID.randomUUID().toString());

        NetworkIpam ipam = null;
        try {
            // Find default-network-ipam
            String ipam_id = _api.findByName(NetworkIpam.class, null, "default-network-ipam");
            assertNotNull(ipam_id);
            ipam = (NetworkIpam) _api.findById(NetworkIpam.class, ipam_id);
            assertNotNull(ipam);
        } catch (IOException ex) {
            fail(ex.getMessage());
        }

        VnSubnetsType subnet = new VnSubnetsType();
        subnet.addIpamSubnets(new VnSubnetsType.IpamSubnetType(new SubnetType("10.0.1.0", 24), "10.0.1.254", UUID.randomUUID().toString(), false, null, null, false, null, null, net.getName() + "-subnet"));
        net.setNetworkIpam(ipam, subnet);

        VirtualMachine vm = new VirtualMachine();
        vm.setName("aa01");
        try {
            assertTrue(_api.create(vm));
        } catch (IOException ex) {
            fail(ex.getMessage());
        }

        VirtualMachineInterface vmi = new VirtualMachineInterface();
        vmi.setParent(vm);
        vmi.setName("test-vmi");

        try {
            assertTrue(_api.create(vmi));
            assertTrue(_api.create(net));
        } catch (IOException ex) {
            fail(ex.getMessage());
        }

        InstanceIp ip_obj = new InstanceIp();
        ip_obj.setName(net.getName() + ":0");
        ip_obj.setVirtualNetwork(net);
        ip_obj.setVirtualMachineInterface(vmi);
        try {
            assertTrue(_api.create(ip_obj));
            // Must perform a GET in order to update the object contents.
            assertTrue(_api.read(ip_obj));
            assertNotNull(ip_obj.getAddress());

            _api.delete(ip_obj);
            _api.delete(net);

        } catch (IOException ex) {
            fail(ex.getMessage());
        }
    }
}
