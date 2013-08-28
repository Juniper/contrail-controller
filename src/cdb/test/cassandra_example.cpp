/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "gen-cpp/Cassandra.h"

#include <protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace org::apache::cassandra;
using namespace boost;

static string host("127.0.0.1");
static int port= 9160;

int64_t getTS(){
    /* If you're doing things quickly, you may want to make use of tv_usec 
     * or something here instead
     */
    time_t ltime;
    ltime=time(NULL);
    return (int64_t)ltime;


}

int main(){
    shared_ptr<TTransport> socket(new TSocket(host, port));
    shared_ptr<TTransport> transport(new TFramedTransport(socket));
    shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    CassandraClient client(protocol);

    const string& key="your_key";

    ColumnPath cpath;
    ColumnParent cp;
    
    ColumnOrSuperColumn csc;
    Column c;

    c.name.assign("column_name");
    c.value.assign("Data for our key to go into column_name");
    c.__isset.value = true;
    c.timestamp = getTS();
    c.__isset.timestamp = true;
    c.ttl = 300;
    c.__isset.ttl = true;



    cp.column_family.assign("nm_cfamily");
    cp.super_column.assign("");

    cpath.column_family.assign("nm_cfamily");
    /* This is required - thrift 'feature' */
    cpath.__isset.column = true;
    cpath.column="column_name";


    try {
        transport->open();
        cout << "Set keyspace to 'nm_example'.." << endl;
        client.set_keyspace("nm_example");
        
        cout << "Insert key '" << key << "' in column '" << c.name << "' in column family '" << cp.column_family << "' with timestamp " << c.timestamp << "..." << endl;
        client.insert(key, cp, c, org::apache::cassandra::ConsistencyLevel::ONE);
        
        cout << "Retrieve key '" << key << "' from column '" << cpath.column << "' in column family '" << cpath.column_family << "' again..." << endl;
        client.get(csc, key, cpath, org::apache::cassandra::ConsistencyLevel::ONE);     
        cout << "Value read is '" << csc.column.value << "'..." << endl;

        c.timestamp++;
        c.value.assign("Updated data going into column_name");
        cout << "Update key '" << key << "' in column with timestamp " << c.timestamp << "..." << endl;
        client.insert(key, cp, c, org::apache::cassandra::ConsistencyLevel::ONE);

        cout << "Retrieve updated key '" << key << "' from column '" << cpath.column << "' in column family '" << cpath.column_family << "' again..." << endl;
        client.get(csc, key, cpath, org::apache::cassandra::ConsistencyLevel::ONE);
        cout << "Updated value is: '" << csc.column.value << "'" << endl; 

        cout << "Remove the key '" << key << "' we just retrieved. Value '" << csc.column.value << "' timestamp " << csc.column.timestamp << " ..." << endl;
        client.remove(key, cpath, csc.column.timestamp, org::apache::cassandra::ConsistencyLevel::ONE);

        transport->close();
    }
    catch (NotFoundException &nf){
        cerr << "NotFoundException ERROR: "<< nf.what() << endl;
    }
    catch (InvalidRequestException &re) {
        cerr << "InvalidRequest ERROR: " << re.why << endl;
    }
    catch (TException &tx) {
        cerr << "TException ERROR: " << tx.what() << endl;
    }
    return 0;
}


