/*
 *  * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 *   */
package net.juniper.contrail.api;

import static org.junit.Assert.*;
import junit.framework.TestCase;

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

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.Ignore;
import org.apache.log4j.Logger;
import org.apache.commons.exec.DefaultExecutor;
import org.apache.commons.exec.CommandLine;
import org.apache.commons.exec.PumpStreamHandler;
import org.apache.commons.exec.ExecuteResultHandler;
import java.io.ByteArrayOutputStream;

public class ContrailApiTest {
    public static ApiTestCommon _apiTest;
    private static final Logger s_logger =
        Logger.getLogger(ContrailApiTest.class);
        
    @Before
    public void setUp() throws Exception {

        if (_apiTest != null ) return;

        int  port = ApiTestCommon.findFreePort();
        ApiTestCommon.launchContrailServer(port);
        s_logger.debug("test api server launched <localhost" + ", " + port + ">");
        _apiTest = new ApiTestCommon(ApiConnectorFactory.build("localhost", port));
    }

    @After
    public void tearDown() throws Exception {
        _apiTest.tearDown();
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
