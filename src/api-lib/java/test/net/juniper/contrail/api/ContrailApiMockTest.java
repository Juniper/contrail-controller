/*
 *  * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 *   */
package net.juniper.contrail.api;

import static org.junit.Assert.*;
import junit.framework.TestCase;

import java.net.ServerSocket;
import java.io.IOException;
import java.util.List;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.UUID;
import java.io.File;
import java.io.OutputStream;
import java.io.ByteArrayOutputStream;
import java.io.ObjectOutputStream;
import java.io.FileOutputStream;
 
import net.juniper.contrail.api.ApiConnectorFactory;
import net.juniper.contrail.api.ApiObjectBase;
import net.juniper.contrail.api.types.Domain;
import net.juniper.contrail.api.types.InstanceIp;
import net.juniper.contrail.api.types.NetworkIpam;
import net.juniper.contrail.api.types.SubnetType;
import net.juniper.contrail.api.types.VirtualMachine;
import net.juniper.contrail.api.types.VirtualMachineInterface;
import net.juniper.contrail.api.types.VirtualNetwork;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.types.NetworkPolicy;
import net.juniper.contrail.api.types.Project;
import net.juniper.contrail.api.types.FloatingIp;
import net.juniper.contrail.api.types.VnSubnetsType;
import net.juniper.contrail.api.types.ServiceInstance;
import net.juniper.contrail.api.ApiConnector;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.Ignore;
import org.apache.log4j.Logger;
import org.apache.commons.exec.DefaultExecutor;
import org.apache.commons.exec.CommandLine;
import org.apache.commons.exec.PumpStreamHandler;
import org.apache.commons.exec.ExecuteResultHandler;
import org.apache.commons.lang.StringUtils;

public class ContrailApiMockTest {
    public static ApiTestCommon _apiTest;
    private static final Logger s_logger =
        Logger.getLogger(ContrailApiMockTest.class);

    public static final String defaultConfigFile = "test/resources/default_config";
        
    @Before
    public void setUp() throws Exception {

        if (_apiTest != null ) return;

        initDefaultConfig(); 
        ApiConnector api = ApiConnectorFactory.build(null, 0);
        ((ApiConnectorMock)api).dumpConfig(VirtualNetwork.class);
        _apiTest = new ApiTestCommon(api);

    }

    public void initDefaultConfig() throws Exception {
        int  port = ApiTestCommon.findFreePort();
        ApiTestCommon.launchContrailServer(port);
        s_logger.debug("initDefaultConfig: test api server launched <localhost" + ", " + port + ">");
        ApiConnector api = ApiConnectorFactory.build("localhost", port);

        Class<?extends ApiObjectBase>[] vncClasses =  new Class[] {
                Domain.class,
                VirtualNetwork.class,
                VirtualMachine.class,
                NetworkIpam.class,
                InstanceIp.class,
                ServiceInstance.class,
                FloatingIp.class,
                NetworkPolicy.class,
                Project.class
        };

        HashMap<Class<?extends ApiObjectBase>, List<HashMap<String, ApiObjectBase>>> map = new HashMap<Class<?extends ApiObjectBase>, List<HashMap<String, ApiObjectBase>>>();
        for (Class<?extends ApiObjectBase> cls: vncClasses) {
            List<?extends ApiObjectBase> vncList = (List<?extends ApiObjectBase>)api.list(cls, null);
            List<HashMap<String, ApiObjectBase>> objList = new ArrayList<HashMap<String, ApiObjectBase>>();
            HashMap<String, ApiObjectBase> uuidMap = new HashMap<String, ApiObjectBase>();
            HashMap<String, ApiObjectBase> fqnMap = new HashMap<String, ApiObjectBase>();
            objList.add(uuidMap);
            objList.add(fqnMap);
            for (ApiObjectBase obj:vncList) {
               api.read(obj);
               uuidMap.put(obj.getUuid(), obj);
               fqnMap.put(StringUtils.join(obj.getQualifiedName(), ':'), obj);
            }
            map.put(cls, objList);
        }

        FileOutputStream fout = new FileOutputStream (defaultConfigFile);
        ObjectOutputStream objOut = new ObjectOutputStream(fout);
        objOut.writeObject(map); 
        objOut.close();
        return;
    }


    @After
    public void tearDown() throws Exception {
    }

    @Test
    public void testNetwork() {
        _apiTest.testNetwork();
    }
   
    @Test
    public void testDeserializeReferenceList() {
        _apiTest.testDeserializeReferenceList();
    }

    @Test
    public void testAddressAllocation() {
        _apiTest.testAddressAllocation();
    }
}
