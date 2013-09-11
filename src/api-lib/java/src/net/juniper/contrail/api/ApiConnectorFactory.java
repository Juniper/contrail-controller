/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.api;

public class ApiConnectorFactory {
    /**
     * Create an ApiConnector object.
     * @param hostname name or IP address of contrail VNC api server.
     * @port  api server port.
     * @return ApiConnector implementation.
     */
    public static ApiConnector build(String hostname, int port) {
        if (hostname == null || hostname.length() == 0 || port == 0) {
            return new ApiConnectorMock();
        }
        return new ApiConnectorImpl(hostname, port);
    }
}
