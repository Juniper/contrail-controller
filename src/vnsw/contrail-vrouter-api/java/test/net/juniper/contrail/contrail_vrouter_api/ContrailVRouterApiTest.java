/**
 * Copyright (c) 2014 Juniper Networks, Inc
 */

package net.juniper.contrail.contrail_vrouter_api;

import java.util.UUID;
import java.net.InetAddress;

import org.apache.log4j.Logger;

import static org.junit.Assert.*;

import org.junit.Before;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;

import static org.mockito.Mockito.*;

import org.mockito.runners.MockitoJUnitRunner;
import org.mockito.MockitoAnnotations;

import net.juniper.contrail.contrail_vrouter_api.ContrailVRouterApi;
import net.juniper.contrail.contrail_vrouter_api.InstanceService;
import net.juniper.contrail.contrail_vrouter_api.Port;

@RunWith(MockitoJUnitRunner.class)
public class ContrailVRouterApiTest {
    @MockitoAnnotations.Mock
    private InstanceService.Iface mockClient;
    private ContrailVRouterApi apiTest;
    private static final Logger s_logger =
        Logger.getLogger(ContrailVRouterApiTest.class);
        
    @Before
    public void setUp() throws Exception {
        s_logger.debug("Setting up ContrailVRouterApiTest");
        int port = -1;
        apiTest = spy(new ContrailVRouterApi(InetAddress.getLocalHost(),
            port, false));
        doReturn(mockClient).when(apiTest).CreateRpcClient();
    }

    @After
    public void tearDown() throws Exception {
        s_logger.debug("Tearing down ContrailVRouterApiTest");
    }

    @Test
    public void TestAddPort() throws Exception {
        // Resynchronize
        UUID vif_uuid = UUID.randomUUID();
        UUID instance_uuid = UUID.randomUUID();
        UUID network_uuid = UUID.randomUUID();
        byte[] mac = new byte[] { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
        InetAddress ip = InetAddress.getLocalHost();
        apiTest.AddPort(vif_uuid, instance_uuid, "tapX",
                        ip, mac, network_uuid, (short)1, (short)1000);
        verify(mockClient).Connect();
        verify(mockClient).AddPort(anyListOf(Port.class));
        assertTrue(apiTest.getPorts().containsKey(vif_uuid));
        // Add
        UUID vif_uuid1 = UUID.randomUUID();
        apiTest.AddPort(vif_uuid1, instance_uuid, "tapX",
                        ip, mac, network_uuid, (short)1, (short)1000);
        verify(mockClient, times(2)).AddPort(anyListOf(Port.class));
        assertTrue(apiTest.getPorts().containsKey(vif_uuid1));
    }

    @Test
    public void TestDeletePort() throws Exception {
        // Resynchronize
        UUID vif_uuid = UUID.randomUUID();
        UUID instance_uuid = UUID.randomUUID();
        UUID network_uuid = UUID.randomUUID();
        byte[] mac = new byte[] { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
        InetAddress ip = InetAddress.getLocalHost();
        apiTest.AddPort(vif_uuid, instance_uuid, "tapX",
                        ip, mac, network_uuid, (short)1, (short)1000);
        verify(mockClient).Connect();
        verify(mockClient).AddPort(anyListOf(Port.class));
        assertTrue(apiTest.getPorts().containsKey(vif_uuid));
        // Delete
        apiTest.DeletePort(vif_uuid);
        verify(mockClient).DeletePort(anyListOf(Short.class));
        assertFalse(apiTest.getPorts().containsKey(vif_uuid));
    }

    @Test
    public void TestPeriodicConnectionCheck() throws Exception {
        // Resynchronize and periodic check
        apiTest.PeriodicConnectionCheck();
        verify(mockClient).Connect();
        verify(mockClient).KeepAliveCheck();
    }
}
