/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fstream>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include "base/logging.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "base/timer.h"
#include "base/connection_info.h"
#include "io/event_manager.h"
#include "QEOpServerProxy.h"
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include "analytics_types.h"
#include "nodeinfo_types.h"
#include "query_engine/options.h"
#include "query.h"
#include "qe_sandesh.h"
#include <base/misc_utils.h>
#include <query_engine/buildinfo.h>
#include <sandesh/sandesh_http.h>
using std::auto_ptr;
using std::string;
using std::vector;
using std::map;
using std::vector;
using boost::assign::list_of;
using boost::system::error_code;
using namespace boost::asio;
using namespace std;
using process::ConnectionStateManager;
using process::GetProcessStateCb;
using process::ConnectionType;
using process::ConnectionTypeName;
using process::g_process_info_constants;
// This is to force qed to wait for a gdbattach
// before proceeding.
// It will make it easier to debug qed during systest
volatile int gdbhelper = 1;
bool QedVersion(std::string &version) {
    return MiscUtils::GetBuildInfo(MiscUtils::Analytics, BuildInfo, version);
}

#include <csignal>

static EventManager * pevm = NULL;
static Options options;
static TaskTrigger *qe_dbstats_task_trigger;
static Timer *qe_dbstats_timer;

static void WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }    
}

static bool OptionsParse(Options &options, EventManager &evm,
                         int argc, char *argv[]) {
    try {
        options.Parse(evm, argc, argv);
        return true;
    } catch (boost::program_options::error &e) {
        cout << "Error " << e.what() << endl;
    } catch (...) {
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return false;
}

static void ShutdownQe() {
    WaitForIdle();
    if (qe_dbstats_timer) {
        TimerManager::DeleteTimer(qe_dbstats_timer);
        delete qe_dbstats_task_trigger;
        qe_dbstats_timer = NULL;
    }
    Sandesh::Uninit();
    ConnectionStateManager::
        GetInstance()->Shutdown();
    WaitForIdle();
}

static void terminate_qe (int param)
{
  ShutdownQe();
  pevm->Shutdown();
}

static void reconfig_qe(int signum) {
    options.ParseReConfig();
}

static bool QEDbStatsTrigger() {
    qe_dbstats_task_trigger->Set();
    return false;
}

/*
 * Send the database stats to the collector periodically
 */
static bool SendQEDbStats(QESandeshContext &ctx) {
    // DB stats
    std::vector<GenDb::DbTableInfo> vdbti, vstats_dbti;
    GenDb::DbErrors dbe;
    QueryEngine *qe = ctx.QE();
    if ((qe->GetDbHandler()).get() != NULL) {
        qe->GetDiffStats(&vdbti, &dbe, &vstats_dbti);
    }
    map<string,GenDb::DbTableStat> mtstat, msstat;

    for (size_t idx=0; idx<vdbti.size(); idx++) {
        GenDb::DbTableStat dtis;
        dtis.set_reads(vdbti[idx].get_reads());
        dtis.set_read_fails(vdbti[idx].get_read_fails());
        dtis.set_writes(vdbti[idx].get_writes());
        dtis.set_write_fails(vdbti[idx].get_write_fails());
        dtis.set_write_back_pressure_fails(vdbti[idx].get_write_back_pressure_fails());
        mtstat.insert(make_pair(vdbti[idx].get_table_name(), dtis));
    }

    for (size_t idx=0; idx<vstats_dbti.size(); idx++) {
        GenDb::DbTableStat dtis;
        dtis.set_reads(vstats_dbti[idx].get_reads());
        dtis.set_read_fails(vstats_dbti[idx].get_read_fails());
        dtis.set_writes(vstats_dbti[idx].get_writes());
        dtis.set_write_fails(vstats_dbti[idx].get_write_fails());
        dtis.set_write_back_pressure_fails(
            vstats_dbti[idx].get_write_back_pressure_fails());
        msstat.insert(make_pair(vstats_dbti[idx].get_table_name(), dtis));
    }

    QEDbStats qe_db_stats;
    qe_db_stats.set_table_info(mtstat);
    qe_db_stats.set_errors(dbe);
    qe_db_stats.set_stats_info(msstat);

    cass::cql::DbStats cql_stats;
    if (qe->GetCqlStats(&cql_stats)) {
        qe_db_stats.set_cql_stats(cql_stats);
    }
    qe_db_stats.set_name(Sandesh::source());
    QEDbStatsUve::Send(qe_db_stats);
    qe_dbstats_timer->Cancel();
    qe_dbstats_timer->Start(60*1000, boost::bind(&QEDbStatsTrigger),
                               NULL);
    return true;
}

int
main(int argc, char *argv[]) {
    EventManager evm;
    pevm = &evm;

    srand(unsigned(time(NULL)));

    // Increase max number of threads available by a factor of 4
    TaskScheduler::SetThreadAmpFactor( 
        QEOpServerProxy::nThreadCountMultFactor);

    if (!OptionsParse(options, evm, argc, argv)) {
        exit(-1);
    }

    while (gdbhelper==0) {
        usleep(1000);
    }

    Module::type module = Module::QUERY_ENGINE;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;

    std::string log_property_file = options.log_property_file();
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    }
    else {
        LoggingInit(options.log_file(), options.log_file_size(),
                    options.log_files_count(), options.use_syslog(),
                    options.syslog_facility(), module_name,
                    SandeshLevelTolog4Level(
                        Sandesh::StringToLevel(options.log_level())));
    }

    int max_tasks = options.max_tasks();
    // Tune max_tasks 
    if (max_tasks == 0)
    {
        // no command line option was specified to tune the max # of tasks
        max_tasks = QEOpServerProxy::nMaxChunks;

        if (max_tasks*2 > TaskScheduler::GetThreadCount())
        {
            // avoid creating too many tasks for one query
            max_tasks = TaskScheduler::GetThreadCount()/2;
            // make sure atleast we have one task
            max_tasks = (max_tasks > 1) ? max_tasks : 1;
        }
    }

    LOG(INFO, "http-server-port " << options.http_server_port());
    LOG(INFO, "Max-tasks " << max_tasks);
    LOG(INFO, "Max-slice " << options.max_slice());
    BOOST_FOREACH(std::string collector_ip, options.collector_server_list()) {
        LOG(INFO, "Collectors  " << collector_ip);
    }

    // Initialize Sandesh
    NodeType::type node_type = 
        g_vns_constants.Module2NodeType.find(module)->second;
    std::string instance_id(g_vns_constants.INSTANCE_ID_DEFAULT);
    // Determine if the number of connections is expected:
    // 1. Collector client
    // 2. Redis
    // 3. Cassandra
    // 4. Discovery (if collector list not configured)
    std::vector<ConnectionTypeName> expected_connections =
        boost::assign::list_of
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second, ""))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::REDIS_QUERY)->second, "Query"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::COLLECTOR)->second, ""));
    bool use_collector_list = true;
    if (!options.collectors_configured()) {
        use_collector_list = false;
    }
    ConnectionStateManager::
        GetInstance()->Init(*evm.io_service(),
            options.hostname(), module_name,
            instance_id,
            boost::bind(&GetProcessStateCb, _1, _2, _3,
            expected_connections), "ObjectCollectorInfo");
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
    bool success;
    // subscribe to the collector service with discovery only if the
    // collector list is not configured.
    if (use_collector_list) {
        std::vector<std::string> collectors(
            options.randomized_collector_server_list());
        if (!collectors.size()) {
            collectors = options.default_collector_server_list();
        }
        success = Sandesh::InitGenerator(module_name, options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    instance_id, &evm, options.http_server_port(),
                    collectors, NULL, Sandesh::DerivedStats(),
                    options.sandesh_config());
    } else {
        const std::vector<std::string> collectors;
        success = Sandesh::InitGenerator(module_name, options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    instance_id, &evm, options.http_server_port(),
                    collectors, NULL, Sandesh::DerivedStats(),
                    options.sandesh_config());
    }
    if (!success) {
        LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
        ShutdownQe();
        exit(1);
    }

    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    vector<string> cassandra_servers(options.cassandra_server_list());
    vector<string> cassandra_ips;
    vector<int> cassandra_ports;
    for (vector<string>::const_iterator it = cassandra_servers.begin();
         it != cassandra_servers.end(); it++) {
        string cassandra_server(*it);
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(cassandra_server, sep);
        tokenizer::iterator tit = tokens.begin();
        string cassandra_ip(*tit);
        cassandra_ips.push_back(cassandra_ip);
        ++tit;
        string port(*tit);
        int cassandra_port;
        stringToInteger(port, cassandra_port);
        cassandra_ports.push_back(cassandra_port);
    }
    ostringstream css;
    copy(cassandra_servers.begin(), cassandra_servers.end(),
         ostream_iterator<string>(css, " "));
    LOG(INFO, "Cassandra Servers: " << css.str());

    boost::scoped_ptr<QueryEngine> qe;

    if (cassandra_ports.size() == 1 && cassandra_ports[0] == 0) {
        qe.reset(new QueryEngine(&evm,
            options.redis_server(),
            options.redis_port(),
            options.redis_password(),
            max_tasks,
            options.max_slice(),
            options.cassandra_user(),
            options.cassandra_password()));
    } else {
        qe.reset(new QueryEngine(&evm,
            cassandra_ips,
            cassandra_ports,
            options.redis_server(),
            options.redis_port(),
            options.redis_password(),
            max_tasks,
            options.max_slice(),
            options.cassandra_user(),
            options.cassandra_password(),
            options.cluster_id()));
    }
    QESandeshContext qec(qe.get());
    Sandesh::set_client_context(&qec);
    qe_dbstats_task_trigger =
        new TaskTrigger(boost::bind(&SendQEDbStats, qec),
                    TaskScheduler::GetInstance()->GetTaskId("QE::DBStats"), 0);
    qe_dbstats_timer = TimerManager::CreateTimer(*evm.io_service(),
        "QE Db stats timer",
        TaskScheduler::GetInstance()->GetTaskId("QE::DBStats"), 0);
    qe_dbstats_timer->Start(5*1000, boost::bind(&QEDbStatsTrigger), NULL);

    signal(SIGTERM, terminate_qe);
    signal(SIGHUP, reconfig_qe);
    evm.Run();

    return 0;
}

