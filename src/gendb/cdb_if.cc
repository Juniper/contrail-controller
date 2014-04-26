/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>

#include <base/parse_object.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "cdb_if.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::org::apache::cassandra;
namespace cassandra = ::org::apache::cassandra;
using namespace GenDb;

#define CDBIF_LOG_ERR_RETURN_FALSE(_Msg)                                  \
    do {                                                                  \
        LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ << ":" << \
            __LINE__ << ": " << _Msg);                                    \
        return false;                                                     \
    } while (false)    

#define CDBIF_LOG(_Level, _Msg)                                           \
    do {                                                                  \
        if (LoggingDisabled()) break;                                     \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();          \
        LOG4CPLUS_##_Level(logger, name_ << ": " << __func__ << ":" <<    \
            __FILE__ << ":" << __LINE__ << ": " << _Msg);                 \
    } while (false)

#define CDBIF_LOG_ERR(_Msg)                                               \
    do {                                                                  \
        LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ << ":" << \
            __LINE__ << ": " << _Msg);                                    \
    } while (false)

#define CDBIF_LOG_ERR_STATIC(_Msg)                                        \
    do {                                                                  \
        LOG(ERROR, __func__ << ":" << __FILE__ << ":" << __LINE__ << ": " \
            << _Msg);                                                     \
    } while (false)

#define CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE(cond)                         \
    do {                                                                  \
        if (!(cond)) {                                                    \
            LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ <<    \
                ":" << __LINE__ << ": (" << #cond << ") FALSE");          \
            return false;                                                 \
        }                                                                 \
    } while (false)

#define CDBIF_EXPECT_TRUE(cond)                                           \
    do {                                                                  \
        if (!(cond)) {                                                    \
            LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ <<    \
                ":" << __LINE__ << ": (" << #cond << ") FALSE");          \
        }                                                                 \
    } while (false)

#define CDBIF_BEGIN_TRY try
#define CDBIF_END_TRY_LOG_INTERNAL(msg, ignore_eexist, no_log_not_found,   \
    invoke_hdlr, err_type, cf_op)                                          \
    catch (NotFoundException &tx) {                                        \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": NotFoundException: " << tx.what();               \
        if (!(no_log_not_found)) {                                         \
            CDBIF_LOG_ERR(ostr.str());                                     \
        }                                                                  \
    } catch (SchemaDisagreementException &tx) {                            \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": SchemaDisagreementException: " << tx.what();     \
        CDBIF_LOG_ERR(ostr.str());                                         \
    } catch (InvalidRequestException &tx) {                                \
        if (ignore_eexist) {                                               \
            size_t eexist = tx.why.find(                                   \
                "Cannot add already existing column family");              \
            if (eexist == std::string::npos) {                             \
                stats_.IncrementErrors(err_type);                          \
                UpdateCfStats(cf_op, msg);                                 \
                std::ostringstream ostr;                                   \
                ostr << msg << ": InvalidRequestException: " << tx.why;    \
                CDBIF_LOG_ERR(ostr.str());                                 \
            }                                                              \
        } else {                                                           \
            stats_.IncrementErrors(err_type);                              \
            UpdateCfStats(cf_op, msg);                                     \
            std::ostringstream ostr;                                       \
            ostr << msg << ": InvalidRequestException: " << tx.why;        \
            CDBIF_LOG_ERR(ostr.str());                                     \
        }                                                                  \
    } catch (UnavailableException& ue) {                                   \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": UnavailableException: " << ue.what();            \
        CDBIF_LOG_ERR(ostr.str());                                         \
    } catch (TimedOutException& te) {                                      \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TimedOutException: " << te.what();               \
        CDBIF_LOG_ERR(ostr.str());                                         \
    } catch (TApplicationException &tx) {                                  \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TApplicationException: " << tx.what();           \
        CDBIF_LOG_ERR(ostr.str());                                         \
    } catch (TTransportException &tx) {                                    \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        if ((invoke_hdlr)) {                                               \
            errhandler_();                                                 \
        }                                                                  \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TTransportException: " << tx.what();             \
        CDBIF_LOG_ERR(ostr.str());                                         \
    } catch (TException &tx) {                                             \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TException: " << tx.what();                      \
        CDBIF_LOG_ERR(ostr.str());                                         \
    }

#define CDBIF_END_TRY_RETURN_FALSE_INTERNAL(msg, ignore_eexist,            \
    no_log_not_found, invoke_hdlr, err_type, cf_op)                        \
    catch (NotFoundException &tx) {                                        \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": NotFoundException: " << tx.what();               \
        if (!(no_log_not_found)) {                                         \
            CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                        \
        } else {                                                           \
            return false;                                                  \
        }                                                                  \
    } catch (SchemaDisagreementException &tx) {                            \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": SchemaDisagreementException: " << tx.what();     \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    } catch (InvalidRequestException &tx) {                                \
        if (ignore_eexist) {                                               \
            size_t eexist = tx.why.find(                                   \
                "Cannot add already existing column family");              \
            if (eexist == std::string::npos) {                             \
                stats_.IncrementErrors(err_type);                          \
                UpdateCfStats(cf_op, msg);                                 \
                std::ostringstream ostr;                                   \
                ostr << msg << ": InvalidRequestException: " << tx.why;    \
                CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                    \
            }                                                              \
        } else {                                                           \
            stats_.IncrementErrors(err_type);                              \
            UpdateCfStats(cf_op, msg);                                     \
            std::ostringstream ostr;                                       \
            ostr << msg << ": InvalidRequestException: " << tx.why;        \
            CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                        \
        }                                                                  \
    } catch (UnavailableException& ue) {                                   \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": UnavailableException: " << ue.what();            \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    } catch (TimedOutException& te) {                                      \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TimedOutException: " << te.what();               \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    } catch (TApplicationException &tx) {                                  \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TApplicationException: " << tx.what();           \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    } catch (TTransportException &tx) {                                    \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        if ((invoke_hdlr)) {                                               \
            errhandler_();                                                 \
        }                                                                  \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TTransportException: " << tx.what();             \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    } catch (TException &tx) {                                             \
        stats_.IncrementErrors(err_type);                                  \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TException: " << tx.what();                      \
        CDBIF_LOG_ERR_RETURN_FALSE(ostr.str());                            \
    }

#define CDBIF_END_TRY_RETURN_FALSE(msg)                                    \
    CDBIF_END_TRY_RETURN_FALSE_INTERNAL(msg, false, false, false,          \
        CdbIfStats::CDBIF_STATS_ERR_NO_ERROR,                              \
        CdbIfStats::CDBIF_STATS_CF_OP_NONE)

#define CDBIF_END_TRY_LOG(msg)                                             \
    CDBIF_END_TRY_LOG_INTERNAL(msg, false, false, false,                   \
        CdbIfStats::CDBIF_STATS_ERR_NO_ERROR,                              \
        CdbIfStats::CDBIF_STATS_CF_OP_NONE)

//
// Types supported by Cassandra are the following, but we use only a subset for
// now
//    AsciiType = 1,
//    LongType = 2,
//    BytesType = 3,
//    BooleanType = 4,
//    CounterColumnType = 5,
//    DecimalType = 6,
//    DoubleType = 7,
//    FloatType = 8,
//    Int32Type = 9,
//    UTF8Type = 10,
//    DateType = 11,
//    LexicalUUIDType = 12,
//    IntegerType = 13,
//    TimeUUIDType = 14,
//    CompositeType = 15,
//

//
// Composite Encoding and Decoding
//

// String
std::string DbEncodeStringComposite(const GenDb::DbDataValue &value) {
    std::string input;
    try {
        input = boost::get<std::string>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    int input_size = input.size();
    uint8_t *data = (uint8_t *)malloc(input_size+3);
    if (data == NULL) {
        CDBIF_LOG_ERR_STATIC("Allocation (size=" << (input_size+3) <<
            ") FAILED");
        return "";
    }
    int i = 0;
    put_value(data+i, 2, input_size);
    i += 2;
    memcpy(&data[i], input.c_str(), input_size);
    i += input_size;
    data[i++] = '\0';
    std::string output((const char *)data, i);
    free(data);
    return output;
}

GenDb::DbDataValue DbDecodeStringComposite(const char *input,
    int &used) {
    used = 0;
    uint16_t len = get_value((const uint8_t *)input, 2);
    used += 2;
    std::string output(&input[used], len);
    used += len;
    used++; // skip eoc
    return output;
}

// UUID
std::string DbEncodeUUIDComposite(const GenDb::DbDataValue &value) {
    boost::uuids::uuid u;
    try {
        u = boost::get<boost::uuids::uuid>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    uint8_t data[32];
    int i = 0;
    put_value(data+i, 2, 0x0010);
    i += 2;
    std::string u_s(u.size(), 0);
    std::copy(u.begin(), u.end(), u_s.begin());
    memcpy(data+i, u_s.c_str(), u_s.size());
    i += 16;
    data[i++] = '\0';
    std::string output((const char *)data, i);
    return output;
}

GenDb::DbDataValue DbDecodeUUIDComposite(const char *input, int &used) {
    boost::uuids::uuid u;
    used = 0;
    uint16_t len = get_value((const uint8_t *)input, 2);
    assert(len == 0x0010);
    used += 2;
    memcpy(&u, input+used, 0x0010);
    used += 0x0010;
    used++;
    return u;
}

// Double
std::string DbEncodeDoubleComposite(const GenDb::DbDataValue &value) {
    uint8_t data[16];
    double input = 0;
    try {
        input = boost::get<double>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    int size = sizeof(double);
    int i = 0;
    put_value(data+i, 2, size);
    i += 2;
    put_double(data+i, input);
    i += size;
    data[i++] = '\0';
    std::string output((const char *)data, i);
    return output;
}

GenDb::DbDataValue DbDecodeDoubleComposite(const char *input,
    int &used) {
    used = 0;
    // skip len
    used += 2;
    double output = get_double((const uint8_t *)&input[used]);
    used += sizeof(double);
    used++; // skip eoc
    return output;
}

// Integer
std::string DbEncodeIntegerCompositeInternal(uint64_t input) {
    uint8_t data[16];
    int size = 1;
    uint64_t temp_input = input >> 8;
    if (input < 256) {
        if (input > 127) size++;
    }
    while (temp_input) {
        if (temp_input < 256) {
            if (temp_input > 127) {
                size++; //additional byte to take care of unsigned-ness
            }
        }
        temp_input >>= 8;
        size++;
    }
    int i = 0;
    put_value(data+i, 2, size);
    i += 2;
    put_value(data+i, size, input);
    i += size;
    data[i++] = '\0';
    std::string output((const char *)data, i);
    return output;
}

template <typename T>
std::string DbEncodeIntegerComposite(const GenDb::DbDataValue &value) {
    uint64_t input(std::numeric_limits<T>::max());
    try {
        input = boost::get<T>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    return DbEncodeIntegerCompositeInternal(input);
}

template <typename T>
GenDb::DbDataValue DbDecodeIntegerComposite(const char *input,
    int &used) {
    used = 0;
    uint16_t len = get_value((const uint8_t *)input, 2);
    used += 2;
    T output = 0;
    for (int i = 0; i < len; i++) {
        output = (output << 8) | (uint8_t)input[used++];
    }
    used++; // skip eoc
    return output;
}

//
// Non Composite Encoding and Decoding
//

// String
std::string DbEncodeStringNonComposite(const GenDb::DbDataValue &value) {
    std::string output;
    try {
        output = boost::get<std::string>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    return output;
}

GenDb::DbDataValue DbDecodeStringNonComposite(const std::string &input) {
    return input;
}

// UUID
std::string DbEncodeUUIDNonComposite(const GenDb::DbDataValue &value) {
    boost::uuids::uuid u;
    try {
        u = boost::get<boost::uuids::uuid>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    std::string u_s(u.size(), 0);
    std::copy(u.begin(), u.end(), u_s.begin());
    return u_s;
}

GenDb::DbDataValue DbDecodeUUIDNonComposite(const std::string &input) {
    boost::uuids::uuid u;
    memcpy(&u, input.c_str(), 0x0010);
    return u;
}

// Double
std::string DbEncodeDoubleNonComposite(const GenDb::DbDataValue &value) {
    double temp = 0;
    try {
        temp = boost::get<double>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    uint8_t data[16];
    put_double(data, temp);
    std::string output((const char *)data, sizeof(double));
    return output;
}

GenDb::DbDataValue DbDecodeDoubleNonComposite(const std::string &input) {
    double output(get_double((const uint8_t *)(input.c_str())));
    return output;
}

// Integer
template <typename T>
std::string DbEncodeIntegerNonComposite(
    const GenDb::DbDataValue &value) {
    T temp(std::numeric_limits<T>::max());
    try {
        temp = boost::get<T>(value);
    } catch (boost::bad_get &ex) {
        CDBIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    uint8_t data[16];
    int size = 1;
    T temp1 = temp >> 8;
    if (temp < 256) {
        if (temp > 127) size++;
    }
    while (temp1) {
        if (temp1 < 256) {
            if (temp1 > 127) size++;
        }
        temp1 >>= 8;
        size++;
    }
    put_value(data, size, temp);
    std::string output((const char *)data, size);
    return output;
}

template <typename T>
GenDb::DbDataValue DbDecodeIntegerNonComposite(
    const std::string &input) {
    uint64_t output = get_value_unaligned((const uint8_t *)(input.c_str()),
        input.size());
    return ((T)output);
}

CdbIf::CdbIfTypeMapDef CdbIf::CdbIfTypeMap =
    boost::assign::map_list_of
        (GenDb::DbDataType::AsciiType, 
             CdbIf::CdbIfTypeInfo("AsciiType",
                 DbEncodeStringComposite, 
                 DbDecodeStringComposite,
                 DbEncodeStringNonComposite,
                 DbDecodeStringNonComposite))
        (GenDb::DbDataType::LexicalUUIDType,
            CdbIf::CdbIfTypeInfo("LexicalUUIDType",
                DbEncodeUUIDComposite,
                DbDecodeUUIDComposite,
                DbEncodeUUIDNonComposite,
                DbDecodeUUIDNonComposite))
        (GenDb::DbDataType::TimeUUIDType,
            CdbIf::CdbIfTypeInfo("TimeUUIDType",
                DbEncodeUUIDComposite,
                DbDecodeUUIDComposite,
                DbEncodeUUIDNonComposite,
                DbDecodeUUIDNonComposite))
        (GenDb::DbDataType::Unsigned8Type,
            CdbIf::CdbIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint8_t>,
                DbDecodeIntegerComposite<uint8_t>,
                DbEncodeIntegerNonComposite<uint8_t>,
                DbDecodeIntegerNonComposite<uint8_t>))
        (GenDb::DbDataType::Unsigned16Type,
            CdbIf::CdbIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint16_t>,
                DbDecodeIntegerComposite<uint16_t>,
                DbEncodeIntegerNonComposite<uint16_t>,
                DbDecodeIntegerNonComposite<uint16_t>))
        (GenDb::DbDataType::Unsigned32Type,
            CdbIf::CdbIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint32_t>,
                DbDecodeIntegerComposite<uint32_t>,
                DbEncodeIntegerNonComposite<uint32_t>,
                DbDecodeIntegerNonComposite<uint32_t>))
        (GenDb::DbDataType::Unsigned64Type,
            CdbIf::CdbIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint64_t>,
                DbDecodeIntegerComposite<uint64_t>,
                DbEncodeIntegerNonComposite<uint64_t>,
                DbDecodeIntegerNonComposite<uint64_t>))
        (GenDb::DbDataType::DoubleType,
            CdbIf::CdbIfTypeInfo("DoubleType",
                DbEncodeDoubleComposite,
                DbDecodeDoubleComposite,
                DbEncodeDoubleNonComposite,
                DbDecodeDoubleNonComposite));

class CdbIf::CleanupTask : public Task {
public:
    CleanupTask(std::string task_id, int task_instance, CdbIf *cdbif) :
        Task(TaskScheduler::GetInstance()->GetTaskId(task_id),
             task_instance),
        cdbif_(cdbif) {
    }

    virtual bool Run() {
        tbb::mutex::scoped_lock lock(cdbif_->cdbq_mutex_);
        // Return if cleanup was cancelled
        if (cdbif_->cleanup_task_ == NULL) {
            return true;
        }
        if (cdbif_->cdbq_.get() != NULL) {
            cdbif_->cdbq_->Shutdown();
            cdbif_->cdbq_.reset();
        }
        cdbif_->cleanup_task_ = NULL;
        return true;
    }

private:
    CdbIf *cdbif_;
};

class CdbIf::InitTask : public Task {
public:
    InitTask(std::string task_id, int task_instance, CdbIf *cdbif) :
        Task(TaskScheduler::GetInstance()->GetTaskId(task_id),
             task_instance),
        task_id_(task_id),
        task_instance_(task_instance),
        cdbif_(cdbif) {
    }

    virtual bool Run() {
        tbb::mutex::scoped_lock lock(cdbif_->cdbq_mutex_);
        if (cdbif_->cdbq_.get() != NULL) {
            cdbif_->cdbq_->Shutdown();
        }
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        cdbif_->cdbq_.reset(new CdbIfQueue(
            scheduler->GetTaskId(task_id_), task_instance_,
            boost::bind(&CdbIf::Db_AsyncAddColumn, cdbif_, _1)));
        cdbif_->cdbq_->SetStartRunnerFunc(
            boost::bind(&CdbIf::Db_IsInitDone, cdbif_));
        cdbif_->cdbq_->SetExitCallback(boost::bind(&CdbIf::Db_BatchAddColumn,
            cdbif_, _1));
        cdbif_->Db_SetQueueWaterMarkInternal(cdbif_->cdbq_.get(),
            cdbif_->cdbq_wm_info_);
        if (cdbif_->cleanup_task_) {
            scheduler->Cancel(cdbif_->cleanup_task_);
            cdbif_->cleanup_task_ = NULL;
        }
        cdbif_->init_task_ = NULL;
        return true;
    }

private:
    std::string task_id_;
    int task_instance_;
    CdbIf *cdbif_;
};

CdbIf::CdbIf(DbErrorHandler errhandler,
        std::string cassandra_ip, unsigned short cassandra_port, int ttl,
        std::string name, bool only_sync) :
    socket_(new TSocket(cassandra_ip, cassandra_port)),
    transport_(new TFramedTransport(socket_)),
    protocol_(new TBinaryProtocol(transport_)),
    client_(new CassandraClient(protocol_)),
    errhandler_(errhandler),
    name_(name),
    init_task_(NULL),
    cleanup_task_(NULL),
    cassandra_ttl_(ttl),
    only_sync_(only_sync),
    task_instance_(-1),
    task_instance_initialized_(false) {
    db_init_done_ = false;
}

CdbIf::CdbIf() {
}

CdbIf::~CdbIf() {
    tbb::mutex::scoped_lock lock(cdbq_mutex_);
    if (transport_) {
        transport_->close();
    }
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    if (init_task_) {
        scheduler->Cancel(init_task_);
        init_task_ = NULL;
    }
    if (cleanup_task_) {
        scheduler->Cancel(cleanup_task_);
        cleanup_task_ = NULL;
    }
}

bool CdbIf::Db_IsInitDone() const {
    return db_init_done_;
}

void CdbIf::Db_SetInitDone(bool init_done) {
    db_init_done_ = init_done;
}

bool CdbIf::Db_Init(std::string task_id, int task_instance) {
    std::ostringstream ostr;
    ostr << task_id << ":" << task_instance;
    std::string errstr(ostr.str());
    CDBIF_BEGIN_TRY {
        transport_->open();
    } CDBIF_END_TRY_RETURN_FALSE(errstr)
    if (only_sync_) {
        return true;
    }
    tbb::mutex::scoped_lock lock(cdbq_mutex_);
    // Initialize task instance
    if (!task_instance_initialized_) {
        task_instance_ = task_instance;
        task_instance_initialized_ = true;
    }
    if (!init_task_) {
        // Start init task with previous task instance to ensure
        // task exclusion with dequeue and exit callback task
        // when calling shutdown
        init_task_ = new InitTask(task_id, task_instance_, this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(init_task_);
    }
    task_instance_ = task_instance;
    return true;
}

void CdbIf::Db_Uninit(std::string task_id, int task_instance) {
    std::ostringstream ostr;
    ostr << task_id << ":" << task_instance;
    std::string errstr(ostr.str());
    CDBIF_BEGIN_TRY {
        transport_->close();
    } CDBIF_END_TRY_LOG(errstr)
    if (only_sync_) {
        return;
    }
    tbb::mutex::scoped_lock lock(cdbq_mutex_);
    if (!cleanup_task_) {
        cleanup_task_ = new CleanupTask(task_id, task_instance, this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(cleanup_task_);
    }
}

void CdbIf::Db_SetQueueWaterMarkInternal(CdbIfQueue *queue,
    DbQueueWaterMarkInfo &wmi) {
    CdbIfQueue::WaterMarkInfo wm(wmi.get<1>(), wmi.get<2>());
    if (wmi.get<0>()) {
        queue->SetHighWaterMark(wm);
    } else {
        queue->SetLowWaterMark(wm);
    }
}

void CdbIf::Db_SetQueueWaterMarkInternal(CdbIfQueue *queue,
    std::vector<DbQueueWaterMarkInfo> &vwmi) {
    for (std::vector<DbQueueWaterMarkInfo>::iterator it =
         vwmi.begin(); it != vwmi.end(); it++) {
        Db_SetQueueWaterMarkInternal(queue, *it);
    }
}

void CdbIf::Db_SetQueueWaterMark(bool high, size_t queue_count,
                                 DbQueueWaterMarkCb cb) {
    DbQueueWaterMarkInfo wm(high, queue_count, cb);
    cdbq_wm_info_.push_back(wm);
    if (cdbq_.get() == NULL) {
        return;
    }
    Db_SetQueueWaterMarkInternal(cdbq_.get(), wm);
}

void CdbIf::Db_ResetQueueWaterMarks() {
    cdbq_wm_info_.clear();
    if (cdbq_.get() != NULL) {
        cdbq_->ResetHighWaterMark();
        cdbq_->ResetLowWaterMark();
    }
}

bool CdbIf::Db_AddTablespace(const std::string& tablespace,
    const std::string& replication_factor) {
    if (!Db_FindTablespace(tablespace)) {
        KsDef ks_def;
        ks_def.__set_name(tablespace);
        ks_def.__set_strategy_class("SimpleStrategy");
        std::map<std::string, std::string> strat_options;
        strat_options.insert(std::pair<std::string,
            std::string>("replication_factor", replication_factor));
        ks_def.__set_strategy_options(strat_options);

        CDBIF_BEGIN_TRY {
            std::string retval;
            client_->system_add_keyspace(retval, ks_def);
        } CDBIF_END_TRY_RETURN_FALSE(tablespace)

        return true;
    }
    return true;
}

bool CdbIf::Db_SetTablespace(const std::string& tablespace) {
    if (!Db_FindTablespace(tablespace)) {
        CDBIF_LOG_ERR_RETURN_FALSE("Tablespace: " << tablespace << 
            "NOT FOUND");
    }
    CDBIF_BEGIN_TRY {
        client_->set_keyspace(tablespace);
        tablespace_ = tablespace;
    } CDBIF_END_TRY_RETURN_FALSE(tablespace)

    KsDef retval;
    CDBIF_BEGIN_TRY {
        client_->describe_keyspace(retval, tablespace);
    } CDBIF_END_TRY_RETURN_FALSE(tablespace)

    std::vector<CfDef>::const_iterator iter;
    for (iter = retval.cf_defs.begin(); iter != retval.cf_defs.end(); iter++) {
        std::string name = (*iter).name;
        CfDef *cfdef = new CfDef;
        *cfdef = *iter;
        CdbIfCfList.insert(name, new CdbIfCfInfo(cfdef));
    }
    return true;
}

bool CdbIf::Db_AddSetTablespace(const std::string& tablespace,
    const std::string& replication_factor) {
    if (!Db_AddTablespace(tablespace, replication_factor)) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_WRITE_TABLESPACE);
        return false;
    }
    if (!Db_SetTablespace(tablespace)) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_READ_TABLESPACE);
        return false;
    }
    return true;
}

bool CdbIf::Db_FindTablespace(const std::string& tablespace) {
    CDBIF_BEGIN_TRY {
        KsDef retval;
        client_->describe_keyspace(retval, tablespace);
    } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(tablespace, false, true, false,
        CdbIfStats::CDBIF_STATS_ERR_NO_ERROR,
        CdbIfStats::CDBIF_STATS_CF_OP_NONE)
    return true;
}

bool CdbIf::Db_GetColumnfamily(CdbIfCfInfo **info, const std::string& cfname) {
    CdbIfCfListType::iterator it;
    if ((it = CdbIfCfList.find(cfname)) != CdbIfCfList.end()) {
        *info = it->second;
        return true;
    }
    return false;
}

bool CdbIf::Db_UseColumnfamily(const GenDb::NewCf& cf) {
    if (Db_FindColumnfamily(cf.cfname_)) {
        return true;
    }
    CdbIfCfInfo *cfinfo;
    if (!Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cf.cfname_);
        return false;
    }
    cfinfo->cf_.reset(new GenDb::NewCf(cf));
    return true;
}

bool CdbIf::Db_FindColumnfamily(const std::string& cfname) {
    CdbIfCfListType::iterator it;
    if ((it = CdbIfCfList.find(cfname)) != CdbIfCfList.end() &&
            (it->second->cf_.get())) {
        return true;
    }
    return false;
}

bool CdbIf::Db_Columnfamily_present(const std::string& cfname) {
    CdbIfCfListType::iterator it;
    if ((it = CdbIfCfList.find(cfname)) != CdbIfCfList.end()) {
        return true;
    }
    return false;
}

bool CdbIf::DbDataTypeVecToCompositeType(std::string& res,
    const GenDb::DbDataTypeVec& db_type) {
    if (db_type.size() == 0) {
        return false;
    } else if (db_type.size() == 1) {
        CdbIfTypeMapDef::iterator it;
        if ((it = CdbIfTypeMap.find(db_type.front())) == CdbIfTypeMap.end())
            return false;

        res = it->second.cassandra_type_;
        return true;
    } else {
        res = "CompositeType(";
        std::vector<GenDb::DbDataType::type>::const_iterator it = db_type.begin();
        CdbIfTypeMapDef::iterator jt;

        if ((jt = CdbIfTypeMap.find(*it)) == CdbIfTypeMap.end())
            return false;
        res.append(jt->second.cassandra_type_);

        it++;
        for (; it != db_type.end(); it++) {
            res.append(", ");
            if ((jt = CdbIfTypeMap.find(*it)) == CdbIfTypeMap.end())
                return false;
            res.append(jt->second.cassandra_type_);
        }
        res.append(")");
        return true;
    }
}

bool CdbIf::DbDataValueFromType(GenDb::DbDataValue& res,
    const GenDb::DbDataType::type& type, const std::string& input) {
    CdbIfTypeMapDef::iterator it = CdbIfTypeMap.find(type);

    if (it == CdbIfTypeMap.end()) {
        return false;
    }
    res = it->second.decode_non_composite_fn_(input);

    return true;
}

bool CdbIf::DbDataValueFromString(GenDb::DbDataValue& res,
    const std::string& cfname, const std::string& col_name,
    const std::string& input) {
    CdbIfCfInfo *info;
    GenDb::NewCf *cf;

    if (!Db_GetColumnfamily(&info, cfname) ||
            !((cf = info->cf_.get()))) {
        return false;
    }
    NewCf::SqlColumnMap::iterator it;
    if ((it = cf->cfcolumns_.find(col_name)) == cf->cfcolumns_.end()) {
        return false;
    }
    CdbIfTypeMapDef::iterator jt = CdbIfTypeMap.find(it->second);

    if (jt == CdbIfTypeMap.end()) {
        return false;
    }
    res = jt->second.decode_non_composite_fn_(input);

    return true;
}

bool CdbIf::DbDataValueToStringNonComposite(std::string& res,
        const GenDb::DbDataValue& value) {
    switch (value.which()) {
    case DB_VALUE_STRING:
        res = DbEncodeStringNonComposite(value);
        break;
    case DB_VALUE_UINT64:
        res = DbEncodeIntegerNonComposite<uint64_t>(value);
        break;
    case DB_VALUE_UINT32:
        res = DbEncodeIntegerNonComposite<uint32_t>(value);
        break;
    case DB_VALUE_UUID:
        res = DbEncodeUUIDNonComposite(value);
        break;
    case DB_VALUE_UINT8:
        res = DbEncodeIntegerNonComposite<uint8_t>(value);
        break;
    case DB_VALUE_UINT16:
        res = DbEncodeIntegerNonComposite<uint16_t>(value);
        break;
    case DB_VALUE_DOUBLE:
        res = DbEncodeDoubleNonComposite(value);
        break;
    case DB_VALUE_BLANK:
    default:
        assert(0);
        break;
    }
    return true;
};    

bool CdbIf::DbDataValueVecToString(std::string& res, bool composite,
        const GenDb::DbDataValueVec& input) {
    if (!composite) {
        if (input.size() == 1) {
            return DbDataValueToStringNonComposite(res, input[0]);
        }
    }
    // Composite encoding
    for (GenDb::DbDataValueVec::const_iterator it = input.begin();
         it != input.end(); it++) {
        const GenDb::DbDataValue &value(*it);
        switch (value.which()) {
        case DB_VALUE_STRING:
            res += DbEncodeStringComposite(value);
            break;
        case DB_VALUE_UINT64:
            res += DbEncodeIntegerComposite<uint64_t>(value);
            break;
        case DB_VALUE_UINT32:
            res += DbEncodeIntegerComposite<uint32_t>(value);
            break;
        case DB_VALUE_UUID:
            res += DbEncodeUUIDComposite(value);
            break;
        case DB_VALUE_UINT8:
            res += DbEncodeIntegerComposite<uint8_t>(value);
            break;
        case DB_VALUE_UINT16:
            res += DbEncodeIntegerComposite<uint16_t>(value);
            break;
        case DB_VALUE_DOUBLE:
            res += DbEncodeDoubleComposite(value);
            break;
        case DB_VALUE_BLANK:
        default:
            assert(0);
            break;
        }
    }    
    return true;
}

bool CdbIf::DbDataValueVecFromString(GenDb::DbDataValueVec& res,
        const GenDb::DbDataTypeVec& typevec,
        const std::string& input) {
    if (typevec.size() == 1) {
        GenDb::DbDataValue res1;
        if (!DbDataValueFromType(res1, typevec[0], input)) {
            CDBIF_LOG_ERR_RETURN_FALSE("Extract type " << typevec[0] 
                << " from " << input << " FAILED");
        }
        res.push_back(res1);
    } else {
        int used = 0;
        const int str_size = input.size();
        const char *data = input.c_str();
        for (GenDb::DbDataTypeVec::const_iterator it = typevec.begin();
                it != typevec.end(); it++) {
            const GenDb::DbDataType::type &type(*it);
            CdbIfTypeMapDef::iterator jt = CdbIfTypeMap.find(type);
            if (jt == CdbIfTypeMap.end()) {
                CDBIF_LOG_ERR("Unknown type " << type);
                continue;
            }
            int elem_use;
            CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE(used < str_size);
            GenDb::DbDataValue val(jt->second.decode_composite_fn_(data+used,
                elem_use));
            used += elem_use;
            res.push_back(val);
        }
    }
    return true;
}

bool CdbIf::ConstructDbDataValueKey(std::string& res, const GenDb::NewCf *cf,
    const GenDb::DbDataValueVec& rowkey) {
    bool composite = cf->key_validation_class.size() == 1 ? false : true;
    return DbDataValueVecToString(res, composite, rowkey);
}

bool CdbIf::ConstructDbDataValueColumnName(std::string& res,
    const GenDb::NewCf *cf, const GenDb::DbDataValueVec& name) {
    bool composite = cf->comparator_type.size() == 1 ? false : true;
    return DbDataValueVecToString(res, composite, name);
}

bool CdbIf::ConstructDbDataValueColumnValue(std::string& res,
    const GenDb::NewCf *cf, const GenDb::DbDataValueVec& value) {
    bool composite = cf->default_validation_class.size() == 1 ? false : true;
    return DbDataValueVecToString(res, composite, value);
}

bool CdbIf::Db_AddColumnfamily(const GenDb::NewCf& cf) {
    if (Db_FindColumnfamily(cf.cfname_)) {
        return true;
    }
    CdbIfStats::ErrorType err_type(
        CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN_FAMILY);
    CdbIfStats::CfOp op(
         CdbIfStats::CDBIF_STATS_CF_OP_WRITE_FAIL);
    if (cf.cftype_ == GenDb::NewCf::COLUMN_FAMILY_SQL) {
        cassandra::CfDef cf_def;
        cf_def.__set_keyspace(tablespace_);
        cf_def.__set_name(cf.cfname_);
        cf_def.__set_gc_grace_seconds(0);

        std::string key_valid_class;
        if (!DbDataTypeVecToCompositeType(key_valid_class,
            cf.key_validation_class)) {
            stats_.IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << 
                ": KeyValidate encode FAILED");
        }
        cf_def.__set_key_validation_class(key_valid_class);

        cassandra::ColumnDef col_def;
        std::vector<cassandra::ColumnDef> col_vec;
        GenDb::NewCf::SqlColumnMap::const_iterator it;
        CdbIfTypeMapDef::iterator jt;
        for (it = cf.cfcolumns_.begin(); it != cf.cfcolumns_.end(); it++) {
            col_def.__set_name(it->first);
            if ((jt = CdbIfTypeMap.find(it->second)) == CdbIfTypeMap.end()) {
                stats_.IncrementErrors(err_type);
                UpdateCfStats(op, cf.cfname_);
                CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << ": Unknown type " << 
                    it->second);
            }
            col_def.__set_validation_class(jt->second.cassandra_type_);
            col_vec.push_back(col_def);
        }
        cf_def.__set_column_metadata(col_vec);

        CdbIfCfInfo *cfinfo;
        if (Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
            if ((*cfinfo->cfdef_.get()).gc_grace_seconds != 0) {
                CDBIF_LOG(DEBUG, "CFName: " << cf.cfname_ << " ID: " << 
                    (*cfinfo->cfdef_.get()).id);
                cf_def.__set_id((*cfinfo->cfdef_.get()).id);
                CDBIF_BEGIN_TRY {
                    std::string ret;
                    client_->system_update_column_family(ret, cf_def);
                } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, false, false,
                    false, err_type, op)
            }
            cfinfo->cf_.reset(new GenDb::NewCf(cf));
        } else {
            CDBIF_BEGIN_TRY {
                std::string ret;
                client_->system_add_column_family(ret, cf_def);
            } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, true, false,
                  false, err_type, op)

            CfDef *cfdef_n = new CfDef;
            *cfdef_n = cf_def;
            std::string cfname_n = cf.cfname_;
            CdbIfCfList.insert(cfname_n, new CdbIfCfInfo(cfdef_n,
                new GenDb::NewCf(cf)));
        }
    } else if (cf.cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL) {
        cassandra::CfDef cf_def;
        cf_def.__set_keyspace(tablespace_);
        cf_def.__set_name(cf.cfname_);
        cf_def.__set_gc_grace_seconds(0);
        // Key Validation
        std::string key_valid_class;
        if (!DbDataTypeVecToCompositeType(key_valid_class,
            cf.key_validation_class)) {
            stats_.IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << 
                ": KeyValidate encode FAILED");
        }
        cf_def.__set_key_validation_class(key_valid_class);
        // Comparator
        std::string comparator_type;
        if (!DbDataTypeVecToCompositeType(comparator_type,
            cf.comparator_type)) {
            stats_.IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << 
               ": Comparator encode FAILED");
        }
        cf_def.__set_comparator_type(comparator_type);
        // Validation
        std::string default_validation_class;
        if (!DbDataTypeVecToCompositeType(default_validation_class,
            cf.default_validation_class)) {
            stats_.IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << 
                ": Validate encode FAILED");
        }
        cf_def.__set_default_validation_class(default_validation_class);

        CdbIfCfInfo *cfinfo;
        if (Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
            if ((*cfinfo->cfdef_.get()).gc_grace_seconds != 0) {
                CDBIF_LOG(DEBUG, "CFName: " << cf.cfname_ << " ID: " << 
                    (*cfinfo->cfdef_.get()).id);
                cf_def.__set_id((*cfinfo->cfdef_.get()).id);
                CDBIF_BEGIN_TRY {
                    std::string ret;
                    client_->system_update_column_family(ret, cf_def);
                } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, false, false,
                    false, err_type, op)
            }
            cfinfo->cf_.reset(new GenDb::NewCf(cf));
        } else {
            CDBIF_BEGIN_TRY {
                std::string ret;
                client_->system_add_column_family(ret, cf_def);
            } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, true, false,
                false, err_type, op)

            CfDef *cfdef_n = new CfDef;
            *cfdef_n = cf_def;
            std::string cfname_n = cf.cfname_;
            CdbIfCfList.insert(cfname_n, new CdbIfCfInfo(cfdef_n,
                new GenDb::NewCf(cf)));
        }
    } else {
        stats_.IncrementErrors(err_type);
        UpdateCfStats(op, cf.cfname_);
        CDBIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << ": Unknown type " <<
            cf.cftype_); 
    }
    return true;
}

bool CdbIf::Db_AsyncAddColumn(CdbIfColList &cl) {
    GenDb::ColList *new_colp(cl.gendb_cl);
    if (new_colp == NULL) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN);
        CDBIF_LOG_ERR("No Column Information");
        return true;
    }
    uint64_t ts(UTCTimestampUsec());
    std::string cfname(new_colp->cfname_);
    // Does the row key exist in the Cassandra mutation map ?
    std::string key_value;
    DbDataValueVecToString(key_value, new_colp->rowkey_.size() != 1,
                           new_colp->rowkey_);
    CassandraMutationMap::iterator cmm_it = mutation_map_.find(key_value);
    if (cmm_it == mutation_map_.end()) {
        cmm_it = mutation_map_.insert(
            std::pair<std::string, CFMutationMap>(key_value,
                CFMutationMap())).first;
    } 
    CFMutationMap &cf_mutation_map(cmm_it->second);
    // Does the column family exist in the column family mutation map ?
    CFMutationMap::iterator cfmm_it = cf_mutation_map.find(cfname);
    if (cfmm_it == cf_mutation_map.end()) {
        cfmm_it = cf_mutation_map.insert(
            std::pair<std::string, MutationList>(cfname, MutationList())).first;
    }
    MutationList &mutations(cfmm_it->second);
    mutations.reserve(mutations.size() + new_colp->columns_.size());

    GenDb::NewCf::ColumnFamilyType cftype = GenDb::NewCf::COLUMN_FAMILY_INVALID;
    for (GenDb::NewColVec::iterator it = new_colp->columns_.begin();
         it != new_colp->columns_.end(); it++) {
        cassandra::Mutation mutation;
        cassandra::ColumnOrSuperColumn c_or_sc;
        cassandra::Column c;

        if (it->cftype_ == GenDb::NewCf::COLUMN_FAMILY_SQL) {
            CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE((it->name->size() == 1) && 
                                           (it->value->size() == 1));
            CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE(
                cftype != GenDb::NewCf::COLUMN_FAMILY_NOSQL);
            cftype = GenDb::NewCf::COLUMN_FAMILY_SQL;
            // Column Name
            std::string col_name;
            try {
                col_name = boost::get<std::string>(it->name->at(0));
            } catch (boost::bad_get& ex) {
                CDBIF_LOG_ERR(cfname << "Column Name FAILED " << ex.what());
            }
            c.__set_name(col_name);
            // Column Value
            std::string col_value;
            DbDataValueToStringNonComposite(col_value, it->value->at(0));
            c.__set_value(col_value);
            // Timestamp and TTL
            c.__set_timestamp(ts);
            if (it->ttl == -1) {
                if (cassandra_ttl_) {
                    c.__set_ttl(cassandra_ttl_);
                }
            } else if (it->ttl) {
                c.__set_ttl(it->ttl);
            }
            c_or_sc.__set_column(c);
            mutation.__set_column_or_supercolumn(c_or_sc);
            mutations.push_back(mutation);
        } else if (it->cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL) {
            CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE(
                cftype != GenDb::NewCf::COLUMN_FAMILY_SQL);
            cftype = GenDb::NewCf::COLUMN_FAMILY_NOSQL;
            // Column Name
            std::string col_name;
            DbDataValueVecToString(col_name, it->name->size() != 1, *it->name);
            c.__set_name(col_name);
            // Column Value
            std::string col_value;
            DbDataValueVecToString(col_value, it->value->size() != 1,
                                   *it->value);
            c.__set_value(col_value);
            // Timestamp and TTL
            c.__set_timestamp(ts);
            if (it->ttl == -1) {
                if (cassandra_ttl_) {
                    c.__set_ttl(cassandra_ttl_);
                }
            } else if (it->ttl) {
                c.__set_ttl(it->ttl);
            }
            c_or_sc.__set_column(c);
            mutation.__set_column_or_supercolumn(c_or_sc);
            mutations.push_back(mutation);
        } else {
            stats_.IncrementErrors(
                CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN);
            UpdateCfWriteFailStats(cfname);
            CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": Invalid CFtype: " << 
                it->cftype_);
        }
    }
    // Update write stats
    UpdateCfWriteStats(cfname);
    // Allocated when enqueued, free it after processing
    delete new_colp;
    cl.gendb_cl = NULL;
    return true;
}

void CdbIf::Db_BatchAddColumn(bool done) {
    CDBIF_BEGIN_TRY {
        client_->batch_mutate(mutation_map_,
            org::apache::cassandra::ConsistencyLevel::ONE);
    } CDBIF_END_TRY_LOG_INTERNAL(integerToString(mutation_map_.size()),
          false, false, true, CdbIfStats::CDBIF_STATS_ERR_WRITE_BATCH_COLUMN,
          CdbIfStats::CDBIF_STATS_CF_OP_NONE)
    mutation_map_.clear();
}

bool CdbIf::Db_AddColumn(std::auto_ptr<GenDb::ColList> cl) {
    if (!Db_IsInitDone() || !cdbq_.get()) {
        UpdateCfWriteFailStats(cl->cfname_);
        return false;
    }
    CdbIfColList qentry;
    qentry.gendb_cl = cl.release();
    cdbq_->Enqueue(qentry);
    return true;
}

bool CdbIf::Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl) {
    CdbIfColList qentry;
    std::string cfname(cl->cfname_);
    qentry.gendb_cl = cl.release();
    bool success = Db_AsyncAddColumn(qentry);
    if (!success) {
        UpdateCfWriteFailStats(cfname);
        return success;
    }
    Db_BatchAddColumn(true);
    return true;
}

bool CdbIf::ColListFromColumnOrSuper(GenDb::ColList& ret,
        std::vector<cassandra::ColumnOrSuperColumn>& result,
        const std::string& cfname) {
    CdbIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND"); 
    }
    if (cf->cftype_ == NewCf::COLUMN_FAMILY_SQL) {
        NewColVec& columns = ret.columns_;
        std::vector<cassandra::ColumnOrSuperColumn>::iterator citer;
        for (citer = result.begin(); citer != result.end(); citer++) {
            GenDb::DbDataValue res;
            if (!DbDataValueFromString(res, cfname, citer->column.name,
                citer->column.value)) {
                CDBIF_LOG_ERR(cfname << ": " << citer->column.name << 
                    " Decode FAILED");
                continue;
            }
            GenDb::NewCol *col(new GenDb::NewCol(citer->column.name, res));
            columns.push_back(col);
        }
    } else if (cf->cftype_ == NewCf::COLUMN_FAMILY_NOSQL) {
        NewColVec& columns = ret.columns_;
        std::vector<cassandra::ColumnOrSuperColumn>::iterator citer;
        for (citer = result.begin(); citer != result.end(); citer++) {
            std::auto_ptr<GenDb::DbDataValueVec> name(new GenDb::DbDataValueVec);
            if (!DbDataValueVecFromString(*name, cf->comparator_type, 
                citer->column.name)) {
                CDBIF_LOG_ERR(cfname << ": Column Name Decode FAILED");
                continue;
            }
            std::auto_ptr<GenDb::DbDataValueVec> value(new GenDb::DbDataValueVec);
            if (!DbDataValueVecFromString(*value, cf->default_validation_class,
                    citer->column.value)) {
                CDBIF_LOG_ERR(cfname << ": Column Value Decode FAILED");
                continue;
            }
            GenDb::NewCol *col(new GenDb::NewCol(name.release(), value.release()));
            columns.push_back(col);
        }
    }
    return true;
}

bool CdbIf::Db_GetRow(GenDb::ColList& ret, const std::string& cfname,
        const DbDataValueVec& rowkey) {
    CdbIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND"); 
    }
    std::string key;
    if (!ConstructDbDataValueKey(key, cf, rowkey)) {
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": Key encode FAILED");
    }
    // Slicer has start_column and end_column as null string, which
    // means return all columns
    cassandra::SliceRange slicer;
    cassandra::SlicePredicate slicep;
    slicep.__set_slice_range(slicer);
    cassandra::ColumnParent cparent;
    cparent.column_family.assign(cfname);
    std::vector<cassandra::ColumnOrSuperColumn> result;
    CDBIF_BEGIN_TRY {
        client_->get_slice(result, key, cparent, slicep,
            ConsistencyLevel::ONE);
    } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cfname, false, false, false,
        CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN,
        CdbIfStats::CDBIF_STATS_CF_OP_READ_FAIL)
    bool success = ColListFromColumnOrSuper(ret, result, cfname);
    if (success) {
        UpdateCfReadStats(cfname);
    } else {
        UpdateCfReadFailStats(cfname);
    }
    return success;
}

bool CdbIf::Db_GetMultiRow(GenDb::ColListVec& ret, const std::string& cfname,
    const std::vector<DbDataValueVec>& rowkeys,
    GenDb::ColumnNameRange *crange_ptr) {
    CdbIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND"); 
    }
    // Populate column name range slice
    cassandra::SliceRange slicer;
    cassandra::SlicePredicate slicep;
    if (crange_ptr) {
        // Column range specified, set appropriate variables
        std::string start_string;
        if (!ConstructDbDataValueColumnName(start_string, cf,
            crange_ptr->start_)) {
            UpdateCfReadFailStats(cfname);
            CDBIF_LOG_ERR_RETURN_FALSE(cfname <<
                ": Column Name Range Start encode FAILED");
        }
        std::string finish_string;
        if (!ConstructDbDataValueColumnName(finish_string, cf,
            crange_ptr->finish_)) {
            UpdateCfReadFailStats(cfname);
            CDBIF_LOG_ERR_RETURN_FALSE(cfname <<
                ": Column Name Range Finish encode FAILED");
        }
        slicer.__set_start(start_string);
        slicer.__set_finish(finish_string);
        slicer.__set_count(crange_ptr->count);
    }
    // If column range is not specified, slicer has start_column and 
    // end_column as null string, which means return all columns
    slicep.__set_slice_range(slicer);
    cassandra::ColumnParent cparent;
    cparent.column_family.assign(cfname);
    // Do query for keys in batches
    std::vector<DbDataValueVec>::const_iterator it = rowkeys.begin();
    while (it != rowkeys.end())  {
        std::vector<std::string> keys;
        for (int i = 0; (it != rowkeys.end()) && (i <= kMaxQueryRows); 
                it++, i++) {
            std::string key;
            if (!ConstructDbDataValueKey(key, cf, *it)) {
                UpdateCfReadFailStats(cfname);
                CDBIF_LOG_ERR_RETURN_FALSE(cfname << "(" << i << 
                    "): Key encode FAILED");
            }
            keys.push_back(key);
        }
        std::map<std::string, std::vector<ColumnOrSuperColumn> > ret_c;
        CDBIF_BEGIN_TRY {
            client_->multiget_slice(ret_c, keys, cparent, slicep, ConsistencyLevel::ONE);
        } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cfname, false, false, false,
            CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN,
            CdbIfStats::CDBIF_STATS_CF_OP_READ_FAIL)
        // Update stats
        UpdateCfReadStats(cfname);
        // Convert result
        for (std::map<std::string, 
                 std::vector<ColumnOrSuperColumn> >::iterator it = ret_c.begin();
             it != ret_c.end(); it++) {
            std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
            if (!DbDataValueVecFromString(col_list->rowkey_,
                cf->key_validation_class, it->first)) {
                CDBIF_LOG_ERR(cfname << ": Key decode FAILED");
                continue;
            }
            if (!ColListFromColumnOrSuper(*col_list, it->second, cfname)) {
                CDBIF_LOG_ERR(cfname << ": Column decode FAILED");
            } 
            ret.push_back(col_list);
        }
    } // while loop
    return true;
}

bool CdbIf::Db_GetRangeSlices(GenDb::ColList& col_list,
    const std::string& cfname, const GenDb::ColumnNameRange& crange,
    const GenDb::DbDataValueVec& rowkey) {
    CdbIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        stats_.IncrementErrors(
            CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND"); 
    }
    bool result = 
        Db_GetRangeSlicesInternal(col_list, cf, crange, rowkey);
    bool col_limit_reached = (col_list.columns_.size() == crange.count);
    GenDb::ColumnNameRange crange_new = crange;
    if (col_limit_reached && (col_list.columns_.size()>0)) {
        // copy last entry of result returned as column start for next qry
        crange_new.start_ = *(col_list.columns_.back()).name;
    }
    // extract rest of the result
    while (col_limit_reached && result) {
        GenDb::ColList next_col_list;
        result = 
            Db_GetRangeSlicesInternal(next_col_list, cf, crange_new, rowkey);
        col_limit_reached = 
            (next_col_list.columns_.size() == crange.count);
        // copy last entry of result returned as column start for next qry
        if (col_limit_reached && (next_col_list.columns_.size()>0)) {
            crange_new.start_ = *(next_col_list.columns_.back()).name;
        }
        // copy result after the first entry
        NewColVec::iterator it = next_col_list.columns_.begin();
        if (it != next_col_list.columns_.end()) it++;
        col_list.columns_.transfer(col_list.columns_.end(), it,
            next_col_list.columns_.end(), next_col_list.columns_);
    }
    return result;
}

bool CdbIf::Db_GetRangeSlicesInternal(GenDb::ColList& col_list,
    const GenDb::NewCf *cf, const GenDb::ColumnNameRange& crange,
    const GenDb::DbDataValueVec& rowkey) {
    const std::string &cfname(cf->cfname_);

    cassandra::ColumnParent cparent;
    cparent.column_family.assign(cfname);
    cparent.super_column.assign("");
    // Key
    std::string key_string;
    if (!ConstructDbDataValueKey(key_string, cf, rowkey)) {
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname << ": Key encode FAILED");
    }
    cassandra::KeyRange krange;
    krange.__set_start_key(key_string);
    krange.__set_end_key(key_string);
    krange.__set_count(1);
    // Column Range
    std::string start_string;
    if (!ConstructDbDataValueColumnName(start_string, cf, crange.start_)) {
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname <<
            ": Column Name Range Start encode FAILED");
    }
    std::string finish_string;
    if (!ConstructDbDataValueColumnName(finish_string, cf, crange.finish_)) {
        UpdateCfReadFailStats(cfname);
        CDBIF_LOG_ERR_RETURN_FALSE(cfname <<
            ": Column Name Range Finish encode FAILED");
    }
    cassandra::SlicePredicate slicep;
    cassandra::SliceRange slicer;
    slicer.__set_start(start_string);
    slicer.__set_finish(finish_string);
    slicer.__set_count(crange.count);
    slicep.__set_slice_range(slicer);

    std::vector<cassandra::KeySlice> result;
    CDBIF_BEGIN_TRY {
        client_->get_range_slices(result, cparent, slicep, krange, ConsistencyLevel::ONE);
    } CDBIF_END_TRY_RETURN_FALSE_INTERNAL(cfname, false, false, false,
        CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN,
        CdbIfStats::CDBIF_STATS_CF_OP_READ_FAIL)
    // Convert result 
    CDBIF_EXPECT_TRUE_ELSE_RETURN_FALSE(result.size() <= 1);
    if (result.size() == 1) {
        cassandra::KeySlice& ks = result[0];
        if (!ColListFromColumnOrSuper(col_list, ks.columns, cfname)) {
            CDBIF_LOG_ERR(cfname << ": Column decode FAILED");
        }
    }
    return true;
}

bool CdbIf::Db_GetQueueStats(uint64_t &queue_count, uint64_t &enqueues) const {
    if (cdbq_.get() != NULL) {
        queue_count = cdbq_->Length();
        enqueues = cdbq_->NumEnqueues();
    } else {
        queue_count = 0;
        enqueues = 0;
    }
    return true;
}

bool CdbIf::Db_GetStats(std::vector<DbTableInfo> &vdbti, DbErrors &dbe) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.Get(vdbti, dbe);
    return true;
}
       
void CdbIf::UpdateCfWriteStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.UpdateCf(cf_name, true, false);
}

void CdbIf::UpdateCfWriteFailStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.UpdateCf(cf_name, true, true);
}

void CdbIf::UpdateCfReadStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.UpdateCf(cf_name, false, false);
}

void CdbIf::UpdateCfReadFailStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.UpdateCf(cf_name, false, true);
}

void CdbIf::UpdateCfStats(CdbIf::CdbIfStats::CfOp op,
    const std::string &cf_name) {
    switch (op) {
    case CdbIfStats::CDBIF_STATS_CF_OP_NONE:
        break;
    case CdbIfStats::CDBIF_STATS_CF_OP_WRITE:
        UpdateCfWriteStats(cf_name);
        break;
    case CdbIfStats::CDBIF_STATS_CF_OP_WRITE_FAIL:
        UpdateCfWriteFailStats(cf_name);
        break;
    case CdbIfStats::CDBIF_STATS_CF_OP_READ:
        UpdateCfReadStats(cf_name);
        break;
    case CdbIfStats::CDBIF_STATS_CF_OP_READ_FAIL:
        UpdateCfReadFailStats(cf_name);
        break;
    default:
        break;
    }
}

// CdbIfStats
void CdbIf::CdbIfStats::UpdateCf(const std::string &cfname, bool write,
    bool fail) {
    CfStatsMap::iterator it = cf_stats_map_.find(cfname);
    if (it == cf_stats_map_.end()) {
        it = (cf_stats_map_.insert(cfname, new CfStats)).first;
    }
    CfStats *cfstats = it->second;
    cfstats->Update(write, fail);
}

void CdbIf::CdbIfStats::IncrementErrors(CdbIf::CdbIfStats::ErrorType type) {
    switch (type) {
    case CdbIfStats::CDBIF_STATS_ERR_WRITE_TABLESPACE:
        db_errors_.write_tablespace_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_READ_TABLESPACE:
        db_errors_.read_tablespace_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN_FAMILY:
        db_errors_.write_column_family_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN_FAMILY:
        db_errors_.read_column_family_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_WRITE_COLUMN:
        db_errors_.write_column_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_WRITE_BATCH_COLUMN:
        db_errors_.write_batch_column_fails++;
        break;
    case CdbIfStats::CDBIF_STATS_ERR_READ_COLUMN:
        db_errors_.read_column_fails++;
        break;
    default:
        break;
    }
}

void CdbIf::CdbIfStats::Get(std::vector<DbTableInfo> &vdbti,
    DbErrors &dbe) {
    // Send diffs
    GetDiffStats<CdbIf::CdbIfStats::CfStatsMap, const std::string,
        CdbIf::CdbIfStats::CfStats, DbTableInfo>(
        cf_stats_map_, ocf_stats_map_, vdbti);
    // Subtract old from new
    CdbIf::CdbIfStats::Errors derrors(db_errors_ - odb_errors_);
    // Update old
    odb_errors_ = db_errors_;
    // Populate from diff 
    derrors.Get(dbe);
}

// CfStats
CdbIf::CdbIfStats::CfStats operator+(const CdbIf::CdbIfStats::CfStats &a,
    const CdbIf::CdbIfStats::CfStats &b) {
    CdbIf::CdbIfStats::CfStats sum;
    sum.num_reads = a.num_reads + b.num_reads;
    sum.num_read_fails = a.num_read_fails + b.num_read_fails;
    sum.num_writes = a.num_writes + b.num_writes;
    sum.num_write_fails = a.num_write_fails + b.num_write_fails;
    return sum;
}
 
CdbIf::CdbIfStats::CfStats operator-(const CdbIf::CdbIfStats::CfStats &a,
    const CdbIf::CdbIfStats::CfStats &b) {
    CdbIf::CdbIfStats::CfStats diff;
    diff.num_reads = a.num_reads - b.num_reads;
    diff.num_read_fails = a.num_read_fails - b.num_read_fails;
    diff.num_writes = a.num_writes - b.num_writes;
    diff.num_write_fails = a.num_write_fails - b.num_write_fails;
    return diff;
}

void CdbIf::CdbIfStats::CfStats::Update(bool write, bool fail) {
    if (write) {
        if (fail) {
            num_write_fails++;
        } else {
            num_writes++;
        }
    } else {
        if (fail) {
            num_read_fails++;
        } else {
            num_reads++;
        }
    }
}

void CdbIf::CdbIfStats::CfStats::Get(const std::string &cfname,
    DbTableInfo &info) const {
    info.set_table_name(cfname);
    info.set_reads(num_reads);
    info.set_read_fails(num_read_fails);
    info.set_writes(num_writes);
    info.set_write_fails(num_write_fails);
}

// Errors
CdbIf::CdbIfStats::Errors operator+(const CdbIf::CdbIfStats::Errors &a,
    const CdbIf::CdbIfStats::Errors &b) {
    CdbIf::CdbIfStats::Errors sum;
    sum.write_tablespace_fails = a.write_tablespace_fails + 
        b.write_tablespace_fails;
    sum.read_tablespace_fails = a.read_tablespace_fails +
        b.read_tablespace_fails;
    sum.write_column_family_fails = a.write_column_family_fails +
        b.write_column_family_fails;
    sum.read_column_family_fails = a.read_column_family_fails +
        b.read_column_family_fails;
    sum.write_column_fails = a.write_column_fails + b.write_column_fails;
    sum.write_batch_column_fails = a.write_batch_column_fails +
        b.write_batch_column_fails;
    sum.read_column_fails = a.read_column_fails + b.read_column_fails;
    return sum;
}

CdbIf::CdbIfStats::Errors operator-(const CdbIf::CdbIfStats::Errors &a,
    const CdbIf::CdbIfStats::Errors &b) {
    CdbIf::CdbIfStats::Errors diff;
    diff.write_tablespace_fails = a.write_tablespace_fails -
        b.write_tablespace_fails;
    diff.read_tablespace_fails = a.read_tablespace_fails -
        b.read_tablespace_fails;
    diff.write_column_family_fails = a.write_column_family_fails -
        b.write_column_family_fails;
    diff.read_column_family_fails = a.read_column_family_fails -
        b.read_column_family_fails;
    diff.write_column_fails = a.write_column_fails - b.write_column_fails;
    diff.write_batch_column_fails = a.write_batch_column_fails -
        b.write_batch_column_fails;
    diff.read_column_fails = a.read_column_fails - b.read_column_fails;
    return diff;
}

void CdbIf::CdbIfStats::Errors::Get(DbErrors &db_errors) const {
    db_errors.set_write_tablespace_fails(write_tablespace_fails);
    db_errors.set_read_tablespace_fails(read_tablespace_fails);
    db_errors.set_write_table_fails(write_column_family_fails);
    db_errors.set_read_table_fails(read_column_family_fails);
    db_errors.set_write_column_fails(write_column_fails);
    db_errors.set_write_batch_column_fails(write_batch_column_fails);
    db_errors.set_read_column_fails(read_column_fails);
}
