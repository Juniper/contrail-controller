/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.api;

import java.io.IOException;
import java.util.List;

public interface ApiConnector {
    boolean create(ApiObjectBase obj) throws IOException;
    boolean read(ApiObjectBase obj) throws IOException;
    boolean update(ApiObjectBase obj) throws IOException;
    void delete(ApiObjectBase obj) throws IOException;
    void delete(Class<? extends ApiObjectBase> cls, String uuid) throws IOException;
    ApiObjectBase findById(Class<? extends ApiObjectBase> cls, String uuid) throws IOException;
    /**
     * Query the api-server name-to-uuid mappings.
     * 
     * @param cls the class of the api object.
     * @param parent parent object. If null the default parent for this object type is used.
     * @param name unqualified object name.
     * @return the uuid of the specified object, if found.
     * @throws IOException
     */
    String findByName(Class<? extends ApiObjectBase> cls, ApiObjectBase parent, String name) throws IOException;
    /**
     * Query the api-server name-to-uuid mappings.
     * 
     * @param cls the class of the api object.
     * @param fqn fully qualified name as a list of strings.
     * @return the uuid of the specified object, if found.
     */
    String findByName(Class<? extends ApiObjectBase> cls, List<String> fqn) throws IOException;
    ApiObjectBase find(Class<? extends ApiObjectBase> cls, ApiObjectBase parent, String name) throws IOException;
    ApiObjectBase findByFQN(Class<? extends ApiObjectBase> cls, String fullName) throws IOException;
    List<? extends ApiObjectBase> list(Class <? extends ApiObjectBase> cls, List<String> parent) throws IOException;
    public <T extends ApiPropertyBase> List<? extends ApiObjectBase> getObjects(Class<? extends ApiObjectBase> cls,
            List<ObjectReference<T>> refList) throws IOException;
}
