/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

package net.juniper.contrail.api;

import java.io.IOException;
import java.lang.reflect.Field;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;

import org.apache.commons.lang.StringUtils;
import org.apache.http.ConnectionReuseStrategy;
import org.apache.http.HttpEntity;
import org.apache.http.HttpHost;
import org.apache.http.HttpRequestInterceptor;
import org.apache.http.HttpResponse;
import org.apache.http.HttpStatus;
import org.apache.http.HttpVersion;
import org.apache.http.client.methods.HttpDelete;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.client.methods.HttpPut;
import org.apache.http.entity.ContentType;
import org.apache.http.entity.StringEntity;
import org.apache.http.impl.DefaultConnectionReuseStrategy;
import org.apache.http.impl.DefaultHttpClientConnection;
import org.apache.http.message.BasicHttpEntityEnclosingRequest;
import org.apache.http.params.HttpParams;
import org.apache.http.params.HttpProtocolParams;
import org.apache.http.params.SyncBasicHttpParams;
import org.apache.http.protocol.BasicHttpContext;
import org.apache.http.protocol.BasicHttpProcessor;
import org.apache.http.protocol.ExecutionContext;
import org.apache.http.protocol.HttpContext;
import org.apache.http.protocol.HttpProcessor;
import org.apache.http.protocol.HttpRequestExecutor;
import org.apache.http.protocol.ImmutableHttpProcessor;
import org.apache.http.protocol.RequestConnControl;
import org.apache.http.protocol.RequestContent;
import org.apache.http.protocol.RequestDate;
import org.apache.http.protocol.RequestExpectContinue;
import org.apache.http.protocol.RequestTargetHost;
import org.apache.http.protocol.RequestUserAgent;
import org.apache.http.util.EntityUtils;
import org.apache.log4j.Logger;

import com.google.common.collect.ImmutableList;

class ApiConnectorImpl implements ApiConnector {
    private static final Logger s_logger =
            Logger.getLogger(ApiConnector.class);

    private String _api_hostname;
    private int _api_port;
    private ApiBuilder _apiBuilder;

    // HTTP Connection parameters
    private HttpParams _params;
    private HttpProcessor _httpproc;
    private HttpRequestExecutor _httpexecutor;
    private HttpContext _httpcontext;
    private HttpHost _httphost;
    private DefaultHttpClientConnection _connection;
    private ConnectionReuseStrategy _connectionStrategy;

    ApiConnectorImpl(String hostname, int port) {
        _api_hostname = hostname;
        _api_port = port;
        initHttpClient();
        initHttpServerParams(hostname, port); 
        _apiBuilder = new ApiBuilder();
    }
    
    
    private void initHttpClient() {
        _params = new SyncBasicHttpParams();
        HttpProtocolParams.setVersion(_params, HttpVersion.HTTP_1_1);
        HttpProtocolParams.setContentCharset(_params, "UTF-8");
        HttpProtocolParams.setUseExpectContinue(_params, false);
        HttpProtocolParams.setHttpElementCharset(_params, "UTF-8");

        _httpproc = new ImmutableHttpProcessor(new HttpRequestInterceptor[] {
                // Required protocol interceptors
                new BasicHttpProcessor(),
                new RequestConnControl(),
                new RequestContent(),
                new RequestDate(),
                new RequestTargetHost(),
                // Recommended protocol interceptors
                new RequestUserAgent(),
                new RequestExpectContinue()
        });
        _httpexecutor = new HttpRequestExecutor();
        _httpcontext = new BasicHttpContext(null);
        _connection = new DefaultHttpClientConnection();
        _connectionStrategy = new DefaultConnectionReuseStrategy();
    }

    public void initHttpServerParams(String hostname, int port) {

        _httphost = new HttpHost(hostname, port);
        _httpcontext.setAttribute(ExecutionContext.HTTP_CONNECTION, _connection);
        _httpcontext.setAttribute(ExecutionContext.HTTP_TARGET_HOST, _httphost);

    }

    public void initApiServerInfo(String hostname, int port) {

        _api_hostname = hostname;
        _api_port = port;
        return;
    }

    private void checkResponseKeepAliveStatus(HttpResponse response) throws IOException {

        if (!_connectionStrategy.keepAlive(response, _httpcontext)) {
            _connection.close();
        }
    }

    private void checkConnection() throws IOException {
        if (!_connection.isOpen()) {
            s_logger.info("http connection <" + _httphost.getHostName() + ", " + 
                    _httphost.getPort() + "> does not exit");
            Socket socket = new Socket(_httphost.getHostName(), _httphost.getPort());
            _connection.bind(socket, _params);
            s_logger.info("http connection <" + _httphost.getHostName() + ", " + 
                    _httphost.getPort() + "> established");
        }
        return;
    }

    protected void finalize() throws Throwable {
        try {
            if (_connection.isOpen()) {
                _connection.close();        // close server connection
            }    
        } finally {
            super.finalize();
        }
    }


    public String getHostName() {
        return _api_hostname;
    }

    public int getPort() {
        return  _api_port;
    }

    public HttpResponse execute(String method, String uri, StringEntity entity) throws IOException {

        checkConnection();

        BasicHttpEntityEnclosingRequest request = new BasicHttpEntityEnclosingRequest(method, uri);
        if (entity != null) {
            request.setEntity(entity);
            s_logger.info(">> Request: " + method + ", " + request.getRequestLine().getUri() +
                    ", " + EntityUtils.toString(entity));
        } else {
            s_logger.info(">> Request: " + method + ", " + request.getRequestLine().getUri());
        }
        HttpResponse response  = null;
        request.setParams(_params);
        try {
            _httpexecutor.preProcess(request, _httpproc, _httpcontext);
            response = _httpexecutor.execute(request, _connection, _httpcontext);
            response.setParams(_params);
            _httpexecutor.postProcess(response, _httpproc, _httpcontext);
        } catch (Exception e) {
            e.printStackTrace();
        }

        s_logger.info("<< Response Status: " + response.getStatusLine());
        return response;
    }

    private String getResponseData(HttpResponse response) {
        HttpEntity entity = response.getEntity();
        if (entity == null) {
            return null;
        }
        String data;
        try {
            data = EntityUtils.toString(entity);
            EntityUtils.consumeQuietly(entity);
        } catch (Exception ex) {
            s_logger.warn("Unable to read http response", ex);
            return null;
        }
        return data;
    }

    private void updateObject(ApiObjectBase obj, ApiObjectBase resp) {
        Class<?> cls = obj.getClass();
        for (Field f : cls.getDeclaredFields()) {
            f.setAccessible(true);
            final Object nv;
            try {
                nv = f.get(resp);
            } catch (Exception ex) {
                s_logger.warn("Unable to read new value for " + f.getName() + ": " + ex.getMessage());
                continue;
            }
            final Object value;
            try {
                value = f.get(obj);
            } catch (Exception ex) {
                s_logger.warn("Unable to read current value of " + f.getName() + ": " + ex.getMessage());
                continue;
            }
            if (value == null && nv != null) {
                try {
                    f.set(obj, nv);
                } catch (Exception ex) {
                    s_logger.warn("Unable to set " + f.getName() + ": " + ex.getMessage());
                }
            }
        }
    }

    @Override
    public synchronized boolean create(ApiObjectBase obj) throws IOException {
        final String typename = _apiBuilder.getTypename(obj.getClass());
        final String jsdata = ApiSerializer.serializeObject(typename, obj);
        final HttpResponse response = execute(HttpPost.METHOD_NAME, "/" + typename + "s", 
                new StringEntity(jsdata, ContentType.APPLICATION_JSON));

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {

            s_logger.error("create api request failed: " + response.getStatusLine().getReasonPhrase());
            if (response.getStatusLine().getStatusCode() != HttpStatus.SC_NOT_FOUND) {
                s_logger.error("Failure message: " + getResponseData(response));
            }
            checkResponseKeepAliveStatus(response);
            return false;
        }

        ApiObjectBase resp = _apiBuilder.jsonToApiObject(getResponseData(response), obj.getClass());
        if (resp == null) {
            s_logger.error("Unable to decode Create response");
            checkResponseKeepAliveStatus(response);
            return false;
        }

        String uuid = obj.getUuid();
        if (uuid == null) {
            obj.setUuid(resp.getUuid());
        } else if (!uuid.equals(resp.getUuid())) {
            s_logger.warn("Response contains unexpected uuid: " + resp.getUuid());
            checkResponseKeepAliveStatus(response);
            return false;
        }
        s_logger.debug("Create " + typename + " uuid: " + obj.getUuid());
        checkResponseKeepAliveStatus(response);
        return true;
    }

    @Override
    public synchronized boolean update(ApiObjectBase obj) throws IOException {
        final String typename = _apiBuilder.getTypename(obj.getClass());
        final String jsdata = ApiSerializer.serializeObject(typename, obj);
        final HttpResponse response = execute(HttpPut.METHOD_NAME, "/" + typename + '/' + obj.getUuid(), 
                new StringEntity(jsdata, ContentType.APPLICATION_JSON));
        boolean success = (response.getStatusLine().getStatusCode() == HttpStatus.SC_OK);
        if (!success) {
            s_logger.warn("<< Response:" + response.getStatusLine().getReasonPhrase());
        }

        EntityUtils.consumeQuietly(response.getEntity());  
        checkResponseKeepAliveStatus(response);
        return success;
    }

    @Override
    public synchronized boolean read(ApiObjectBase obj) throws IOException {
        final String typename = _apiBuilder.getTypename(obj.getClass());
        final HttpResponse response = execute(HttpGet.METHOD_NAME, 
                "/" + typename + '/' + obj.getUuid(), null);
        ApiObjectBase resp = null;

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {
            s_logger.warn("GET failed: " + response.getStatusLine().getReasonPhrase());
            if (response.getStatusLine().getStatusCode() != HttpStatus.SC_NOT_FOUND) {
                s_logger.error("Failure message: " + getResponseData(response));
            }
            checkResponseKeepAliveStatus(response);
            return false;
        } else {
            s_logger.debug("Response: " + response);
            resp = _apiBuilder.jsonToApiObject(getResponseData(response), obj.getClass());
            if (resp == null) {
                s_logger.warn("Unable to decode GET response");
            }
        }

        if (resp == null) {
            checkResponseKeepAliveStatus(response);
            return false;
        }
        updateObject(obj, resp);
        checkResponseKeepAliveStatus(response);
        return true;
    }

    @Override
    public void delete(ApiObjectBase obj) throws IOException {
        delete(obj.getClass(), obj.getUuid());
    }

    @Override
    public synchronized void delete(Class<? extends ApiObjectBase> cls, String uuid) throws IOException {
        
        try {
            if (findById(cls, uuid) == null) {
                return;
            }
        } catch (IOException ex) {
            return;
        }
        
        final String typename = _apiBuilder.getTypename(cls);
        final HttpResponse response = execute(HttpDelete.METHOD_NAME, 
                "/" + typename +  '/' + uuid, null);

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {
            s_logger.warn("Delete failed: " + response.getStatusLine().getReasonPhrase());
            if (response.getStatusLine().getStatusCode() != HttpStatus.SC_NOT_FOUND) {
                s_logger.error("Failure message: " + getResponseData(response));
            }
            checkResponseKeepAliveStatus(response);
            return;
        }
        EntityUtils.consumeQuietly(response.getEntity());  
        checkResponseKeepAliveStatus(response);
    }

    @Override
    public synchronized ApiObjectBase find(Class<? extends ApiObjectBase> cls, ApiObjectBase parent, String name) throws IOException {
        String uuid = findByName(cls, parent, name);
        if (uuid == null) {
            return null;
        }
        ApiObjectBase obj = findById(cls, uuid);
        return obj;
    }

    @Override
    public ApiObjectBase findByFQN(Class<? extends ApiObjectBase> cls, String fullName) throws IOException {
        List<String> fqn = ImmutableList.copyOf(StringUtils.split(fullName, ':'));
        String uuid = findByName(cls, fqn);
        if (uuid == null) {
             return null;
        }
        return findById(cls, uuid);
    }

    @Override
    public synchronized ApiObjectBase findById(Class<? extends ApiObjectBase> cls, String uuid) throws IOException {
        final String typename = _apiBuilder.getTypename(cls);
        final HttpResponse response = execute(HttpGet.METHOD_NAME, 
                '/' + typename + '/' + uuid, null);
        ApiObjectBase object = null;

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {
            EntityUtils.consumeQuietly(response.getEntity());  
            checkResponseKeepAliveStatus(response);
        } else {
            object = _apiBuilder.jsonToApiObject(getResponseData(response), cls);
            if (object == null) {
                s_logger.warn("Unable to decode find response");
            }
        }
        checkResponseKeepAliveStatus(response);
        return object;
    }

    @Override
    public String findByName(Class<? extends ApiObjectBase> cls, ApiObjectBase parent, String name) throws IOException {
        List<String> name_list = new ArrayList<String>();
        if (parent != null) {
            name_list.addAll(parent.getQualifiedName());
        } else {
            try {
                name_list.addAll(cls.newInstance().getDefaultParent());
            } catch (Exception ex) {
                // Instantiation or IllegalAccess
                s_logger.error("Failed to instantiate object of class " + cls.getName(), ex);
                return null;
            }
        }
        name_list.add(name);
        return findByName(cls, name_list);
    }

    @Override
    // POST http://hostname:port/fqname-to-id
    // body: {"type": class, "fq_name": [parent..., name]}
    public synchronized String findByName(Class<? extends ApiObjectBase> cls, List<String> name_list) throws IOException {
        String jsonStr = _apiBuilder.buildFqnJsonString(cls, name_list);
        final HttpResponse response = execute(HttpPost.METHOD_NAME, "/fqname-to-id",  
                new StringEntity(jsonStr, ContentType.APPLICATION_JSON));

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {
            EntityUtils.consumeQuietly(response.getEntity());  
            checkResponseKeepAliveStatus(response);
            return null;
        }

        String data = getResponseData(response);
        if (data == null) {
            checkResponseKeepAliveStatus(response);
            return null;
        }
        s_logger.debug("<< Response Data: " + data);

        String uuid = _apiBuilder.getUuid(data);
        if (uuid == null) {
            s_logger.warn("Unable to parse response");
            checkResponseKeepAliveStatus(response);
            return null;
        }
        checkResponseKeepAliveStatus(response);
        return uuid;
    }

    @Override
    public synchronized List<? extends ApiObjectBase> list(Class<? extends ApiObjectBase> cls, List<String> parent) throws IOException {
        final String typename = _apiBuilder.getTypename(cls);
        final HttpResponse response = execute(HttpGet.METHOD_NAME, '/' + typename + 's', null);

        if (response.getStatusLine().getStatusCode() != HttpStatus.SC_OK) {
            s_logger.warn("list failed with :" + response.getStatusLine().getReasonPhrase());
            EntityUtils.consumeQuietly(response.getEntity());  
            checkResponseKeepAliveStatus(response);
            return null;
        }

        String data = getResponseData(response);
        if (data == null) {
            checkResponseKeepAliveStatus(response);
            return null;
        }
        List<? extends ApiObjectBase> list = _apiBuilder.jsonToApiObjects(data, cls, parent);
        if (list == null) {
            s_logger.warn("Unable to parse/deserialize response: " + data);
        }
        checkResponseKeepAliveStatus(response);
        return list;
    }

    
    @Override
    public <T extends ApiPropertyBase> List<? extends ApiObjectBase>
        getObjects(Class<? extends ApiObjectBase> cls, List<ObjectReference<T>> refList) throws IOException {
        
        List<ApiObjectBase> list = new ArrayList<ApiObjectBase>();
        for (ObjectReference<T> ref : refList) {
            ApiObjectBase obj = findById(cls, ref.getUuid());
            if (obj == null) {
                s_logger.warn("Unable to find element with uuid: " + ref.getUuid());
                continue;
            }
            list.add(obj);
        }
        return list;
    }    
}
