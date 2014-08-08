/**
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.contrail_vrouter_api;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.net.InetAddress;
import java.nio.ByteBuffer;

import org.apache.log4j.Logger;
import org.apache.thrift.TException;
import org.apache.thrift.protocol.TProtocol;
import org.apache.thrift.protocol.TBinaryProtocol;
import org.apache.thrift.transport.TTransport;
import org.apache.thrift.transport.TSocket;
import org.apache.thrift.transport.TFramedTransport;
import org.apache.thrift.transport.TTransportException;

import net.juniper.contrail.contrail_vrouter_api.InstanceService;
import net.juniper.contrail.contrail_vrouter_api.Port;
import net.juniper.contrail.contrail_vrouter_api.ReconnectingThriftClient;

public class ContrailVRouterApi {
    private static final Logger s_logger =
            Logger.getLogger(ContrailVRouterApi.class);

    private final InetAddress rpc_address;
    private final int rpc_port;
    private Map<UUID, Port> ports;
    private InstanceService.Iface client;
    private boolean oneShot;

    public ContrailVRouterApi(InetAddress ip, int port, boolean oneShot) {
        this.rpc_address = ip;
        this.rpc_port = port;
        this.ports = new HashMap<UUID, Port>();
        this.client = null;
        this.oneShot = oneShot;
    }

    private static List<Short> UUIDToArray(UUID uuid) {
        ByteBuffer bb = ByteBuffer.wrap(new byte[16]);
        bb.putLong(uuid.getMostSignificantBits());
        bb.putLong(uuid.getLeastSignificantBits());
        byte[] buuid = bb.array();
        Short[] suuid = new Short[16];
        for (int i  = 0; i < buuid.length; i++) {
            suuid[i] = (short) buuid[i];
        }
        return Arrays.asList(suuid);
    }

    private static String MacAddressToString(byte[] mac) {
        StringBuilder sb = new StringBuilder(18);
        for (byte b : mac) {
            if (sb.length() > 0)
                sb.append(':');
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }

    InstanceService.Iface CreateRpcClient() {
        TSocket socket = new TSocket(rpc_address.getHostAddress(), rpc_port);
        TTransport transport = new TFramedTransport(socket);
        try {
            transport.open();
        } catch (TTransportException tte) {
            s_logger.error(rpc_address + ":" + rpc_port + 
                    " Create TTransportException: " + tte.getMessage());
            return null;
        }
        TProtocol protocol = new TBinaryProtocol(transport);
        InstanceService.Iface iface = (InstanceService.Iface)ReconnectingThriftClient.wrap(
                new InstanceService.Client(protocol));
        return iface;
    }

    private boolean CreateAndResynchronizeRpcClient() {
        client = CreateRpcClient();
        if (client == null) {
            return false;
        }
        if (oneShot) {
            return true;
        }
        try {
            client.Connect();
        } catch (TException te) {
            s_logger.error(rpc_address + ":" + rpc_port +
                    " Connect TException: " + te.getMessage());
            client = null;
            return false;
        }
        return Resynchronize();
    }

    /**
     * Add all the active ports to the agent
     * @return true on success, false otherwise
     */
    private boolean Resynchronize() {
        List<Port> lports = new ArrayList<Port>(ports.values());
        try {
            client.AddPort(lports);
            return true;
        } catch (TException te) {
            s_logger.error(rpc_address + ":" + rpc_port + 
                    " Resynchronize TException: " + te.getMessage());
            client = null;
            return false;
        }
    }

    /**
     * Get current list of ports
     * @return Port Map
     */
    public Map<UUID, Port> getPorts() {
        return ports;
    }

    /**
     * Add a port to the agent. The information is stored in the ports
     * map since the vrouter agent may not be running at the
     * moment or the RPC may fail.
     * 
     * @param vif_uuid         UUID of the VIF/Port
     * @param vm_uuid          UUID of the instance
     * @param interface_name   Name of the VIF/Port
     * @param interface_ip     IP address associated with the VIF
     * @param mac_address      MAC address of the VIF
     * @param network_uuid     UUID of the associated virtual network
     */
    public void AddPort(UUID vif_uuid, UUID vm_uuid, String interface_name,
            InetAddress interface_ip, byte[] mac_address, UUID network_uuid, short vlanId, short primaryVlanId) {
        Port aport = new Port(UUIDToArray(vif_uuid), UUIDToArray(vm_uuid),
                interface_name, interface_ip.getHostAddress(),
                UUIDToArray(network_uuid), MacAddressToString(mac_address));
        aport.setVlan_id(primaryVlanId);
        aport.setIsolated_vlan_id(vlanId);
        ports.put(vif_uuid, aport);
        if (client == null) {
            if (!CreateAndResynchronizeRpcClient()) {
                s_logger.error(rpc_address + ":" + rpc_port + 
                        " AddPort: " + vif_uuid + "(" + interface_name +
                        ") FAILED"); 
                return;
            }
        } else {
            List<Port> aports = new ArrayList<Port>();
            aports.add(aport);
            try {
                client.AddPort(aports);
            } catch (TException te) {
                s_logger.error(rpc_address + ":" + rpc_port + 
                        " AddPort: " + vif_uuid + "(" +
                        interface_name + ") TException: " + te.getMessage());
                client = null;
            }
        }	
    }

    /**
     * Delete a port from the agent. The port is first removed from the
     * internal ports map
     *   
     * @param vif_uuid  UUID of the VIF/Port
     */
    public void DeletePort(UUID vif_uuid) {
        ports.remove(vif_uuid);
        if (client == null) {
            if (!CreateAndResynchronizeRpcClient()) {
                s_logger.error(rpc_address + ":" + rpc_port + 
                        " DeletePort: " + vif_uuid + " FAILED");
                return;
            }
        } else {
            try {
                client.DeletePort(UUIDToArray(vif_uuid));
            } catch (TException te) {
                s_logger.error(rpc_address + ":" + rpc_port + 
                        " AddPort: " + vif_uuid + 
                        " TException: " + te.getMessage());
                client = null;
            }
        }
    }

    /**
     * Periodically check if the connection to the agent is valid.
     * It is the API client's responsibility to periodically invoke this
     * method
     */
    public void PeriodicConnectionCheck() {
        if (client == null) {
            if (!CreateAndResynchronizeRpcClient()) {
                s_logger.error(rpc_address + ":" + rpc_port + 
                        " PeriodicConnectionCheck: FAILED");
                return;
            }
        }
        try {
            client.KeepAliveCheck();
        } catch (TException te) {
            s_logger.error(rpc_address + ":" + rpc_port + 
                    " KeepAliveCheck: TException: " +
                    te.getMessage());
            client = null;
        }		
    }
}

