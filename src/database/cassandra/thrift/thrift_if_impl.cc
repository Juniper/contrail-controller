/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/foreach.hpp>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#include <base/parse_object.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include <protocol/TBinaryProtocol.h>
#include <transport/TSocketPool.h>
#include <transport/TTransportUtils.h>
#include <database/cassandra/thrift/gen-cpp/Cassandra.h>

#include <database/cassandra/thrift/thrift_if_impl.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::org::apache::cassandra;
namespace cassandra = ::org::apache::cassandra;
using namespace GenDb;

#define THRIFTIF_LOG_ERR_RETURN_FALSE(_Msg)                               \
    do {                                                                  \
        LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ << ":" << \
            __LINE__ << ": " << _Msg);                                    \
        return false;                                                     \
    } while (false)

#define THRIFTIF_LOG_ERR_RETURN_FALSE_STATIC(_Msg)                        \
    do {                                                                  \
        LOG(ERROR,  __func__ << ":" << __FILE__ << ":" << __LINE__ << ": "\
            << _Msg);                                                     \
        return false;                                                     \
    } while (false)

#define THRIFTIF_LOG(_Level, _Msg)                                        \
    do {                                                                  \
        if (LoggingDisabled()) break;                                     \
        log4cplus::Logger logger = log4cplus::Logger::getRoot();          \
        LOG4CPLUS_##_Level(logger, name_ << ": " << __func__ << ":" <<    \
            __FILE__ << ":" << __LINE__ << ": " << _Msg);                 \
    } while (false)

#define THRIFTIF_LOG_ERR(_Msg)                                            \
    do {                                                                  \
        LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ << ":" << \
            __LINE__ << ": " << _Msg);                                    \
    } while (false)

#define THRIFTIF_LOG_ERR_STATIC(_Msg)                                     \
    do {                                                                  \
        LOG(ERROR, __func__ << ":" << __FILE__ << ":" << __LINE__ << ": " \
            << _Msg);                                                     \
    } while (false)

#define THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE(cond)                      \
    do {                                                                  \
        if (!(cond)) {                                                    \
            LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ <<    \
                ":" << __LINE__ << ": (" << #cond << ") FALSE");          \
            return false;                                                 \
        }                                                                 \
    } while (false)

#define THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE_STATIC(cond)               \
    do {                                                                  \
        if (!(cond)) {                                                    \
            LOG(ERROR, __func__ << ":" << __FILE__ <<                     \
                ":" << __LINE__ << ": (" << #cond << ") FALSE");          \
            return false;                                                 \
        }                                                                 \
    } while (false)

#define THRIFTIF_EXPECT_TRUE(cond)                                        \
    do {                                                                  \
        if (!(cond)) {                                                    \
            LOG(ERROR, name_ << ": " << __func__ << ":" << __FILE__ <<    \
                ":" << __LINE__ << ": (" << #cond << ") FALSE");          \
        }                                                                 \
    } while (false)

#define THRIFTIF_BEGIN_TRY try
#define THRIFTIF_END_TRY_LOG_INTERNAL(msg, ignore_eexist, no_log_not_found,\
    invoke_hdlr, err_type, cf_op)                                          \
    catch (NotFoundException &tx) {                                        \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": NotFoundException: " << tx.what();               \
        if (!(no_log_not_found)) {                                         \
            THRIFTIF_LOG_ERR(ostr.str());                                  \
        }                                                                  \
    } catch (SchemaDisagreementException &tx) {                            \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": SchemaDisagreementException: " << tx.what();     \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    } catch (InvalidRequestException &tx) {                                \
        if (ignore_eexist) {                                               \
            size_t eexist = tx.why.find(                                   \
                "Cannot add already existing column family");              \
            if (eexist == std::string::npos) {                             \
                IncrementErrors(err_type);                                 \
                UpdateCfStats(cf_op, msg);                                 \
                std::ostringstream ostr;                                   \
                ostr << msg << ": InvalidRequestException: " << tx.why;    \
                THRIFTIF_LOG_ERR(ostr.str());                              \
            }                                                              \
        } else {                                                           \
            IncrementErrors(err_type);                                     \
            UpdateCfStats(cf_op, msg);                                     \
            std::ostringstream ostr;                                       \
            ostr << msg << ": InvalidRequestException: " << tx.why;        \
            THRIFTIF_LOG_ERR(ostr.str());                                  \
        }                                                                  \
    } catch (UnavailableException& ue) {                                   \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": UnavailableException: " << ue.what();            \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    } catch (TimedOutException& te) {                                      \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TimedOutException: " << te.what();               \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    } catch (TApplicationException &tx) {                                  \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TApplicationException: " << tx.what();           \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    } catch (TTransportException &tx) {                                    \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        if ((invoke_hdlr)) {                                               \
            errhandler_();                                                 \
        }                                                                  \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TTransportException: " << tx.what();             \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    } catch (TException &tx) {                                             \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TException: " << tx.what();                      \
        THRIFTIF_LOG_ERR(ostr.str());                                      \
    }

#define THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(msg, ignore_eexist,         \
    no_log_not_found, invoke_hdlr, err_type, cf_op)                        \
    catch (NotFoundException &tx) {                                        \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": NotFoundException: " << tx.what();               \
        if (!(no_log_not_found)) {                                         \
            THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                     \
        } else {                                                           \
            return false;                                                  \
        }                                                                  \
    } catch (SchemaDisagreementException &tx) {                            \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": SchemaDisagreementException: " << tx.what();     \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    } catch (InvalidRequestException &tx) {                                \
        if (ignore_eexist) {                                               \
            size_t eexist = tx.why.find(                                   \
                "Cannot add already existing column family");              \
            if (eexist == std::string::npos) {                             \
                IncrementErrors(err_type);                                 \
                UpdateCfStats(cf_op, msg);                                 \
                std::ostringstream ostr;                                   \
                ostr << msg << ": InvalidRequestException: " << tx.why;    \
                THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                 \
            }                                                              \
        } else {                                                           \
            IncrementErrors(err_type);                                     \
            UpdateCfStats(cf_op, msg);                                     \
            std::ostringstream ostr;                                       \
            ostr << msg << ": InvalidRequestException: " << tx.why;        \
            THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                     \
        }                                                                  \
    } catch (UnavailableException& ue) {                                   \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": UnavailableException: " << ue.what();            \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    } catch (TimedOutException& te) {                                      \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TimedOutException: " << te.what();               \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    } catch (TApplicationException &tx) {                                  \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TApplicationException: " << tx.what();           \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    } catch (AuthenticationException &tx) {                                \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": Authentication Exception: " << tx.what();        \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    }  catch (TTransportException &tx) {                                   \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        if ((invoke_hdlr)) {                                               \
            errhandler_();                                                 \
        }                                                                  \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TTransportException: " << tx.what();             \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    } catch (TException &tx) {                                             \
        IncrementErrors(err_type);                                         \
        UpdateCfStats(cf_op, msg);                                         \
        std::ostringstream ostr;                                           \
        ostr << msg << ": TException: " << tx.what();                      \
        THRIFTIF_LOG_ERR_RETURN_FALSE(ostr.str());                         \
    }

#define THRIFTIF_END_TRY_RETURN_FALSE(msg)                                    \
    THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(msg, false, false, false,          \
        GenDb::IfErrors::ERR_NO_ERROR,                                        \
        GenDb::GenDbIfStats::TABLE_OP_NONE)

#define THRIFTIF_END_TRY_LOG(msg)                                             \
    THRIFTIF_END_TRY_LOG_INTERNAL(msg, false, false, false,                   \
        GenDb::IfErrors::ERR_NO_ERROR,                                        \
        GenDb::GenDbIfStats::TABLE_OP_NONE)

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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    int input_size = input.size();
    uint8_t *data = (uint8_t *)malloc(input_size+3);
    if (data == NULL) {
        THRIFTIF_LOG_ERR_STATIC("Allocation (size=" << (input_size+3) <<
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
    boost::uuids::uuid u(boost::uuids::nil_uuid());
    try {
        u = boost::get<boost::uuids::uuid>(value);
    } catch (boost::bad_get &ex) {
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
            ex.what());
    }
    return output;
}

GenDb::DbDataValue DbDecodeStringNonComposite(const std::string &input) {
    return input;
}

// UUID
std::string DbEncodeUUIDNonComposite(const GenDb::DbDataValue &value) {
    boost::uuids::uuid u(boost::uuids::nil_uuid());
    try {
        u = boost::get<boost::uuids::uuid>(value);
    } catch (boost::bad_get &ex) {
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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
        THRIFTIF_LOG_ERR_STATIC("Extract type " << value.which() << " FAILED: " <<
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

//
// Encode / Decode Type Map
//
typedef boost::function<std::string(const GenDb::DbDataValue&)>
    DbEncodeCompositeFunc;
typedef boost::function<GenDb::DbDataValue(const char *input, int &used)>
    DbDecodeCompositeFunc;
typedef boost::function<std::string(const GenDb::DbDataValue&)>
    DbEncodeNonCompositeFunc;
typedef boost::function<GenDb::DbDataValue(const std::string&)>
    DbDecodeNonCompositeFunc;

struct ThriftIfTypeInfo {
    ThriftIfTypeInfo(std::string cassandra_type,
                     DbEncodeCompositeFunc encode_composite_fn,
                     DbDecodeCompositeFunc decode_composite_fn,
                     DbEncodeNonCompositeFunc encode_non_composite_fn,
                     DbDecodeNonCompositeFunc decode_non_composite_fn) :
        cassandra_type_("org.apache.cassandra.db.marshal." + cassandra_type),
        encode_composite_fn_(encode_composite_fn),
        decode_composite_fn_(decode_composite_fn),
        encode_non_composite_fn_(encode_non_composite_fn),
        decode_non_composite_fn_(decode_non_composite_fn) {
    }

    std::string cassandra_type_;
    DbEncodeCompositeFunc encode_composite_fn_;
    DbDecodeCompositeFunc decode_composite_fn_;
    DbEncodeNonCompositeFunc encode_non_composite_fn_;
    DbDecodeNonCompositeFunc decode_non_composite_fn_;
};

typedef boost::unordered_map<GenDb::DbDataType::type, ThriftIfTypeInfo>
    ThriftIfTypeMapDef;

static ThriftIfTypeMapDef ThriftIfTypeMap =
    boost::assign::map_list_of
        (GenDb::DbDataType::AsciiType,
             ThriftIfTypeInfo("AsciiType",
                 DbEncodeStringComposite,
                 DbDecodeStringComposite,
                 DbEncodeStringNonComposite,
                 DbDecodeStringNonComposite))
        (GenDb::DbDataType::LexicalUUIDType,
            ThriftIfTypeInfo("LexicalUUIDType",
                DbEncodeUUIDComposite,
                DbDecodeUUIDComposite,
                DbEncodeUUIDNonComposite,
                DbDecodeUUIDNonComposite))
        (GenDb::DbDataType::TimeUUIDType,
            ThriftIfTypeInfo("TimeUUIDType",
                DbEncodeUUIDComposite,
                DbDecodeUUIDComposite,
                DbEncodeUUIDNonComposite,
                DbDecodeUUIDNonComposite))
        (GenDb::DbDataType::Unsigned8Type,
            ThriftIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint8_t>,
                DbDecodeIntegerComposite<uint8_t>,
                DbEncodeIntegerNonComposite<uint8_t>,
                DbDecodeIntegerNonComposite<uint8_t>))
        (GenDb::DbDataType::Unsigned16Type,
            ThriftIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint16_t>,
                DbDecodeIntegerComposite<uint16_t>,
                DbEncodeIntegerNonComposite<uint16_t>,
                DbDecodeIntegerNonComposite<uint16_t>))
        (GenDb::DbDataType::Unsigned32Type,
            ThriftIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint32_t>,
                DbDecodeIntegerComposite<uint32_t>,
                DbEncodeIntegerNonComposite<uint32_t>,
                DbDecodeIntegerNonComposite<uint32_t>))
        (GenDb::DbDataType::Unsigned64Type,
            ThriftIfTypeInfo("IntegerType",
                DbEncodeIntegerComposite<uint64_t>,
                DbDecodeIntegerComposite<uint64_t>,
                DbEncodeIntegerNonComposite<uint64_t>,
                DbDecodeIntegerNonComposite<uint64_t>))
        (GenDb::DbDataType::DoubleType,
            ThriftIfTypeInfo("DoubleType",
                DbEncodeDoubleComposite,
                DbDecodeDoubleComposite,
                DbEncodeDoubleNonComposite,
                DbDecodeDoubleNonComposite))
        (GenDb::DbDataType::UTF8Type,
            ThriftIfTypeInfo("UTF8Type",
                DbEncodeStringComposite,
                DbDecodeStringComposite,
                DbEncodeStringNonComposite,
                DbDecodeStringNonComposite));


//
// Encode / Decode Functions
//
static bool DbDataTypeVecToCompositeType(std::string& res,
    const GenDb::DbDataTypeVec& db_type) {
    if (db_type.size() == 0) {
        return false;
    } else if (db_type.size() == 1) {
        ThriftIfTypeMapDef::iterator it;
        if ((it = ThriftIfTypeMap.find(db_type.front())) == ThriftIfTypeMap.end())
            return false;

        res = it->second.cassandra_type_;
        return true;
    } else {
        res = "org.apache.cassandra.db.marshal.CompositeType(";
        std::vector<GenDb::DbDataType::type>::const_iterator it = db_type.begin();
        ThriftIfTypeMapDef::iterator jt;
        if ((jt = ThriftIfTypeMap.find(*it)) == ThriftIfTypeMap.end())
            return false;
        res.append(jt->second.cassandra_type_);

        it++;
        for (; it != db_type.end(); it++) {
            res.append(",");
            if ((jt = ThriftIfTypeMap.find(*it)) == ThriftIfTypeMap.end())
                return false;
            res.append(jt->second.cassandra_type_);
        }
        res.append(")");
        return true;
    }
}

static bool DbDataValueFromType(GenDb::DbDataValue& res,
    const GenDb::DbDataType::type& type, const std::string& input) {
    ThriftIfTypeMapDef::iterator it = ThriftIfTypeMap.find(type);
    if (it == ThriftIfTypeMap.end()) {
	return false;
    }
    res = it->second.decode_non_composite_fn_(input);
    return true;
}

static bool DbDataValueToStringNonComposite(std::string& res,
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

bool DbDataValueVecToString(std::string& res, bool composite,
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

bool DbDataValueVecFromString(GenDb::DbDataValueVec& res,
    const GenDb::DbDataTypeVec& typevec, const std::string& input) {
    if (typevec.size() == 1) {
        GenDb::DbDataValue res1;
        if (!DbDataValueFromType(res1, typevec[0], input)) {
            THRIFTIF_LOG_ERR_RETURN_FALSE_STATIC("Extract type " << typevec[0]
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
            ThriftIfTypeMapDef::iterator jt = ThriftIfTypeMap.find(type);
            if (jt == ThriftIfTypeMap.end()) {
                THRIFTIF_LOG_ERR_STATIC("Unknown type " << type);
                continue;
            }
            int elem_use;
            THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE_STATIC(used < str_size);
            GenDb::DbDataValue val(jt->second.decode_composite_fn_(data+used,
                elem_use));
            used += elem_use;
            res.push_back(val);
        }
    }
    return true;
}

static bool ConstructDbDataValueKey(std::string& res, const GenDb::NewCf *cf,
    const GenDb::DbDataValueVec& rowkey) {
    bool composite = cf->key_validation_class.size() == 1 ? false : true;
    return DbDataValueVecToString(res, composite, rowkey);
}

static bool ConstructDbDataValueColumnName(std::string& res,
    const GenDb::NewCf *cf, const GenDb::DbDataValueVec& name) {
    bool composite = cf->comparator_type.size() == 1 ? false : true;
    return DbDataValueVecToString(res, composite, name);
}

//
// ThriftIfImpl
//
class ThriftIfImpl::CleanupTask : public Task {
public:
    CleanupTask(std::string task_id, int task_instance, ThriftIfImpl *if_impl) :
        Task(TaskScheduler::GetInstance()->GetTaskId(task_id),
             task_instance),
        if_impl_(if_impl) {
    }

    virtual bool Run() {
        tbb::mutex::scoped_lock lock(if_impl_->q_mutex_);
        // Return if cleanup was cancelled
        if (if_impl_->cleanup_task_ == NULL) {
            return true;
        }
        if (if_impl_->q_.get() != NULL) {
            if_impl_->q_->Shutdown();
            if_impl_->q_.reset();
        }
        if_impl_->cleanup_task_ = NULL;
        return true;
    }
    std::string Description() const { return "ThriftImpls::CleanupTask"; }

private:
    ThriftIfImpl *if_impl_;
};

class ThriftIfImpl::InitTask : public Task {
public:
    InitTask(std::string task_id, int task_instance, ThriftIfImpl *if_impl) :
        Task(TaskScheduler::GetInstance()->GetTaskId(task_id),
             task_instance),
        task_id_(task_id),
        task_instance_(task_instance),
        if_impl_(if_impl) {
    }

    virtual bool Run() {
        tbb::mutex::scoped_lock lock(if_impl_->q_mutex_);
        if (if_impl_->q_.get() != NULL) {
            if_impl_->q_->Shutdown();
        }
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        if_impl_->q_.reset(new ThriftIfQueue(
            scheduler->GetTaskId(task_id_), if_impl_->task_instance_,
            boost::bind(&ThriftIfImpl::Db_AsyncAddColumn, if_impl_, _1),
            ThriftIfImpl::kQueueSize));
        if_impl_->q_->SetStartRunnerFunc(
            boost::bind(&ThriftIfImpl::Db_IsInitDone, if_impl_));
        if_impl_->q_->SetExitCallback(boost::bind(
            &ThriftIfImpl::Db_BatchAddColumn, if_impl_, _1));
        if_impl_->Db_SetQueueWaterMarkInternal(if_impl_->q_.get(),
            if_impl_->q_wm_info_);
        if (if_impl_->cleanup_task_) {
            scheduler->Cancel(if_impl_->cleanup_task_);
            if_impl_->cleanup_task_ = NULL;
        }
        if_impl_->init_task_ = NULL;
        return true;
    }
    std::string Description() const { return "ThriftImpl::InitTask"; }

private:
    std::string task_id_;
    int task_instance_;
    ThriftIfImpl *if_impl_;
};

ThriftIfImpl::ThriftIfImpl(GenDb::GenDbIf::DbErrorHandler errhandler,
        const std::vector<std::string> &cassandra_ips,
        const std::vector<int> &cassandra_ports,
        std::string name, bool only_sync, const std::string& cassandra_user,
        const std::string& cassandra_password) :
    socket_(new TSocketPool(cassandra_ips, cassandra_ports)),
    transport_(new TFramedTransport(socket_)),
    protocol_(new TBinaryProtocol(transport_)),
    client_(new CassandraClient(protocol_)),
    errhandler_(errhandler),
    name_(name),
    init_task_(NULL),
    cleanup_task_(NULL),
    only_sync_(only_sync),
    task_instance_(-1),
    prev_task_instance_(-1),
    task_instance_initialized_(false),
    cassandra_user_(cassandra_user),
    cassandra_password_(cassandra_password) {
    // reduce connection timeout
    boost::shared_ptr<TSocket> tsocket =
        boost::dynamic_pointer_cast<TSocket>(socket_);
    tsocket->setConnTimeout(connectionTimeout);

    db_init_done_ = false;
}

ThriftIfImpl::ThriftIfImpl() :
    init_task_(NULL),
    cleanup_task_(NULL),
    only_sync_(false),
    task_instance_(-1),
    prev_task_instance_(-1),
    task_instance_initialized_(false) {
    db_init_done_ = false;
}

ThriftIfImpl::~ThriftIfImpl() {
    tbb::mutex::scoped_lock lock(q_mutex_);
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

bool ThriftIfImpl::Db_IsInitDone() const {
    return db_init_done_;
}

void ThriftIfImpl::Db_SetInitDone(bool init_done) {
    db_init_done_ = init_done;
}

bool ThriftIfImpl::Db_Init(const std::string& task_id, int task_instance) {
    std::ostringstream ostr;
    ostr << task_id << ":" << task_instance;
    std::string errstr(ostr.str());

    THRIFTIF_BEGIN_TRY {
        transport_->open();
    } THRIFTIF_END_TRY_RETURN_FALSE(errstr)

    if (!set_keepalive()) {
       return false;
    }

    //Connect with passwd
    if (!cassandra_user_.empty() && !cassandra_password_.empty()) {
        std::map<std::string, std::string> creds;
        creds.insert(std::pair<std::string,
                         std::string>("username",cassandra_user_));
        creds.insert(std::pair<std::string,
                         std::string>("password",cassandra_password_));
        AuthenticationRequest authRequest;
        authRequest.__set_credentials(creds);
        ostr << "Authentication failed";
        std::string errstr1(ostr.str());
        THRIFTIF_BEGIN_TRY {
            client_->login(authRequest);
        } THRIFTIF_END_TRY_RETURN_FALSE(errstr1)
    }
    if (only_sync_) {
        return true;
    }
    tbb::mutex::scoped_lock lock(q_mutex_);
    // Initialize task instance
    if (!task_instance_initialized_) {
        task_instance_ = task_instance;
        task_instance_initialized_ = true;
    }
    if (!init_task_) {
        // Start init task with previous task instance to ensure
        // task exclusion with dequeue and exit callback task
        // when calling shutdown
        prev_task_instance_ = task_instance_;
        task_instance_ = task_instance;
        init_task_ = new InitTask(task_id, prev_task_instance_, this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(init_task_);
        THRIFTIF_LOG(INFO, "Initialization Task Created: " <<
            prev_task_instance_ << " -> " << task_instance_);
    } else {
        prev_task_instance_ = task_instance_;
        task_instance_ = task_instance;
        THRIFTIF_LOG(INFO, "Initialization Task Present: " <<
            prev_task_instance_ << " -> " << task_instance_);
    }
    return true;
}

void ThriftIfImpl::Db_UninitUnlocked(const std::string& task_id, int task_instance) {
    std::ostringstream ostr;
    ostr << task_id << ":" << task_instance;
    std::string errstr(ostr.str());
    THRIFTIF_BEGIN_TRY {
        transport_->close();
    } THRIFTIF_END_TRY_LOG(errstr)
    if (only_sync_) {
        return;
    }
    if (!cleanup_task_) {
        cleanup_task_ = new CleanupTask(task_id, task_instance, this);
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(cleanup_task_);
    }
}

void ThriftIfImpl::Db_Uninit(const std::string& task_id, int task_instance) {
    tbb::mutex::scoped_lock lock(q_mutex_);
    Db_UninitUnlocked(task_id, task_instance);
}

std::vector<GenDb::Endpoint> ThriftIfImpl::Db_GetEndpoints() const {
    boost::shared_ptr<TSocket> tsocket =
        boost::dynamic_pointer_cast<TSocket>(socket_);
    boost::system::error_code ec;
    boost::asio::ip::address addr(boost::asio::ip::address::from_string(
        tsocket->getHost(), ec));
    GenDb::Endpoint endpoint(addr, tsocket->getPort());
    return boost::assign::list_of(endpoint);
}

void ThriftIfImpl::Db_SetQueueWaterMarkInternal(ThriftIfQueue *queue,
    const DbQueueWaterMarkInfo &wmi) {
    WaterMarkInfo wm(wmi.get<1>(), wmi.get<2>());
    if (wmi.get<0>()) {
        queue->SetHighWaterMark(wm);
    } else {
        queue->SetLowWaterMark(wm);
    }
}

void ThriftIfImpl::Db_SetQueueWaterMarkInternal(ThriftIfQueue *queue,
    const std::vector<DbQueueWaterMarkInfo> &vwmi) {
    BOOST_FOREACH(const DbQueueWaterMarkInfo &wmi, vwmi) {
        Db_SetQueueWaterMarkInternal(queue, wmi);
    }
}

void ThriftIfImpl::Db_SetQueueWaterMark(bool high, size_t queue_count,
                                 GenDb::GenDbIf::DbQueueWaterMarkCb cb) {
    tbb::mutex::scoped_lock lock(q_mutex_);
    DbQueueWaterMarkInfo wm(high, queue_count, cb);
    q_wm_info_.push_back(wm);
    if (q_.get() == NULL) {
        return;
    }
    Db_SetQueueWaterMarkInternal(q_.get(), wm);
}

void ThriftIfImpl::Db_ResetQueueWaterMarks() {
    tbb::mutex::scoped_lock lock(q_mutex_);
    q_wm_info_.clear();
    if (q_.get() != NULL) {
        q_->ResetHighWaterMark();
        q_->ResetLowWaterMark();
    }
}

bool ThriftIfImpl::Db_AddTablespace(const std::string& tablespace,
    const std::string& replication_factor) {
    if (!Db_FindTablespace(tablespace)) {
        KsDef ks_def;
        ks_def.__set_name(tablespace);
        ks_def.__set_strategy_class("SimpleStrategy");
        std::map<std::string, std::string> strat_options;
        strat_options.insert(std::pair<std::string,
            std::string>("replication_factor", replication_factor));
        ks_def.__set_strategy_options(strat_options);

        THRIFTIF_BEGIN_TRY {
            std::string retval;
            client_->system_add_keyspace(retval, ks_def);
        } THRIFTIF_END_TRY_RETURN_FALSE(tablespace)

        return true;
    }
    return true;
}

bool ThriftIfImpl::Db_SetTablespace(const std::string& tablespace) {
    if (!Db_FindTablespace(tablespace)) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Tablespace: " << tablespace <<
            "NOT FOUND");
    }
    THRIFTIF_BEGIN_TRY {
        client_->set_keyspace(tablespace);
        tablespace_ = tablespace;
    } THRIFTIF_END_TRY_RETURN_FALSE(tablespace)

    KsDef retval;
    THRIFTIF_BEGIN_TRY {
        client_->describe_keyspace(retval, tablespace);
    } THRIFTIF_END_TRY_RETURN_FALSE(tablespace)

    std::vector<CfDef>::const_iterator iter;
    for (iter = retval.cf_defs.begin(); iter != retval.cf_defs.end(); iter++) {
        std::string name = (*iter).name;
        CfDef *cfdef = new CfDef;
        *cfdef = *iter;
        ThriftIfCfList.insert(name, new ThriftIfCfInfo(cfdef));
    }
    return true;
}

bool ThriftIfImpl::Db_AddSetTablespace(const std::string& tablespace,
    const std::string& replication_factor) {
    if (!Db_AddTablespace(tablespace, replication_factor)) {
        IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_TABLESPACE);
        return false;
    }
    if (!Db_SetTablespace(tablespace)) {
        IncrementErrors(
            GenDb::IfErrors::ERR_READ_TABLESPACE);
        return false;
    }
    return true;
}

bool ThriftIfImpl::Db_FindTablespace(const std::string& tablespace) {
    THRIFTIF_BEGIN_TRY {
        KsDef retval;
        client_->describe_keyspace(retval, tablespace);
    } THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(tablespace, false, true, false,
        GenDb::IfErrors::ERR_NO_ERROR,
        GenDb::GenDbIfStats::TABLE_OP_NONE)
    return true;
}

bool ThriftIfImpl::Db_GetColumnfamily(ThriftIfCfInfo **info, const std::string& cfname) {
    ThriftIfCfListType::iterator it;
    if ((it = ThriftIfCfList.find(cfname)) != ThriftIfCfList.end()) {
        *info = it->second;
        return true;
    }
    return false;
}

bool ThriftIfImpl::Db_UseColumnfamily(const GenDb::NewCf& cf) {
    if (Db_FindColumnfamily(cf.cfname_)) {
        return true;
    }
    ThriftIfCfInfo *cfinfo;
    if (!Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
        IncrementErrors(
            GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cf.cfname_);
        return false;
    }
    cfinfo->cf_.reset(new GenDb::NewCf(cf));
    return true;
}

bool ThriftIfImpl::Db_FindColumnfamily(const std::string& cfname) {
    ThriftIfCfListType::iterator it;
    if ((it = ThriftIfCfList.find(cfname)) != ThriftIfCfList.end() &&
            (it->second->cf_.get())) {
        return true;
    }
    return false;
}

bool ThriftIfImpl::Db_Columnfamily_present(const std::string& cfname) {
    ThriftIfCfListType::iterator it;
    if ((it = ThriftIfCfList.find(cfname)) != ThriftIfCfList.end()) {
        return true;
    }
    return false;
}

bool ThriftIfImpl::DbDataValueFromString(GenDb::DbDataValue& res,
    const std::string& cfname, const std::string& col_name,
    const std::string& input) {
    ThriftIfCfInfo *info;
    GenDb::NewCf *cf;

    if (!Db_GetColumnfamily(&info, cfname) ||
            !((cf = info->cf_.get()))) {
        return false;
    }
    NewCf::SqlColumnMap::iterator it;
    if ((it = cf->cfcolumns_.find(col_name)) == cf->cfcolumns_.end()) {
        return false;
    }
    ThriftIfTypeMapDef::iterator jt = ThriftIfTypeMap.find(it->second);

    if (jt == ThriftIfTypeMap.end()) {
        return false;
    }
    res = jt->second.decode_non_composite_fn_(input);

    return true;
}


bool ThriftIfImpl::Db_AddColumnfamily(const GenDb::NewCf& cf) {
    if (Db_FindColumnfamily(cf.cfname_)) {
        return true;
    }
    GenDb::IfErrors::Type err_type(
        GenDb::IfErrors::ERR_WRITE_COLUMN_FAMILY);
    GenDb::GenDbIfStats::TableOp op(
         GenDb::GenDbIfStats::TABLE_OP_WRITE_FAIL);
    if (cf.cftype_ == GenDb::NewCf::COLUMN_FAMILY_SQL) {
        cassandra::CfDef cf_def;
        cf_def.__set_keyspace(tablespace_);
        cf_def.__set_name(cf.cfname_);
        cf_def.__set_gc_grace_seconds(0);
        cf_def.__set_compaction_strategy("LeveledCompactionStrategy");

        std::string key_valid_class;
        if (!DbDataTypeVecToCompositeType(key_valid_class,
            cf.key_validation_class)) {
            IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ <<
                ": KeyValidate encode FAILED");
        }
        cf_def.__set_key_validation_class(key_valid_class);

        cassandra::ColumnDef col_def;
        std::vector<cassandra::ColumnDef> col_vec;
        GenDb::NewCf::SqlColumnMap::const_iterator it;
        ThriftIfTypeMapDef::iterator jt;
        for (it = cf.cfcolumns_.begin(); it != cf.cfcolumns_.end(); it++) {
            col_def.__set_name(it->first);
            if ((jt = ThriftIfTypeMap.find(it->second)) == ThriftIfTypeMap.end()) {
                IncrementErrors(err_type);
                UpdateCfStats(op, cf.cfname_);
                THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << ": Unknown type " <<
                    it->second);
            }
            col_def.__set_validation_class(jt->second.cassandra_type_);
            col_vec.push_back(col_def);
        }
        cf_def.__set_column_metadata(col_vec);

        ThriftIfCfInfo *cfinfo;
        if (Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
            // for SQL-like schema change is fine
            if (DB_IsCfSchemaChanged(cfinfo->cfdef_.get(), &cf_def)) {
                THRIFTIF_LOG(DEBUG, "CFName: " << cf.cfname_ << " ID: " <<
                    (*cfinfo->cfdef_.get()).id << " schema changed...");
            }
            cfinfo->cf_.reset(new GenDb::NewCf(cf));
        } else {
            THRIFTIF_BEGIN_TRY {
                std::string ret;
                client_->system_add_column_family(ret, cf_def);
            } THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, true, false,
                  false, err_type, op)

            CfDef *cfdef_n = new CfDef;
            *cfdef_n = cf_def;
            std::string cfname_n = cf.cfname_;
            ThriftIfCfList.insert(cfname_n, new ThriftIfCfInfo(cfdef_n,
                new GenDb::NewCf(cf)));
        }
    } else if (cf.cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL) {
        cassandra::CfDef cf_def;
        cf_def.__set_keyspace(tablespace_);
        cf_def.__set_name(cf.cfname_);
        cf_def.__set_gc_grace_seconds(0);
        cf_def.__set_compaction_strategy("LeveledCompactionStrategy");

        // Key Validation
        std::string key_valid_class;
        if (!DbDataTypeVecToCompositeType(key_valid_class,
            cf.key_validation_class)) {
            IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ <<
                ": KeyValidate encode FAILED");
        }
        cf_def.__set_key_validation_class(key_valid_class);
        // Comparator
        std::string comparator_type;
        if (!DbDataTypeVecToCompositeType(comparator_type,
            cf.comparator_type)) {
            IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ <<
               ": Comparator encode FAILED");
        }
        cf_def.__set_comparator_type(comparator_type);
        // Validation
        std::string default_validation_class;
        if (!DbDataTypeVecToCompositeType(default_validation_class,
            cf.default_validation_class)) {
            IncrementErrors(err_type);
            UpdateCfStats(op, cf.cfname_);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ <<
                ": Validate encode FAILED");
        }
        cf_def.__set_default_validation_class(default_validation_class);

        ThriftIfCfInfo *cfinfo;
        if (Db_GetColumnfamily(&cfinfo, cf.cfname_)) {
            // for NoSQL schema change cannot be supported
            if (DB_IsCfSchemaChanged(cfinfo->cfdef_.get(), &cf_def)) {
                THRIFTIF_LOG_ERR_RETURN_FALSE("CFName: " << cf.cfname_ << " ID: " <<
                    (*cfinfo->cfdef_.get()).id << " schema changed...");
            }
            cfinfo->cf_.reset(new GenDb::NewCf(cf));
        } else {
            THRIFTIF_BEGIN_TRY {
                std::string ret;
                client_->system_add_column_family(ret, cf_def);
            } THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(cf.cfname_, true, false,
                false, err_type, op)

            CfDef *cfdef_n = new CfDef;
            *cfdef_n = cf_def;
            std::string cfname_n = cf.cfname_;
            ThriftIfCfList.insert(cfname_n, new ThriftIfCfInfo(cfdef_n,
                new GenDb::NewCf(cf)));
        }
    } else {
        IncrementErrors(err_type);
        UpdateCfStats(op, cf.cfname_);
        THRIFTIF_LOG_ERR_RETURN_FALSE(cf.cfname_ << ": Unknown type " <<
            cf.cftype_);
    }
    return true;
}

bool ThriftIfImpl::DB_IsCfSchemaChanged(org::apache::cassandra::CfDef *cfdef,
                                 org::apache::cassandra::CfDef *newcfdef) {
    if (cfdef->key_validation_class != newcfdef->key_validation_class) {
        return true;
    }

    if ((newcfdef->default_validation_class.size()) &&
        (cfdef->default_validation_class != newcfdef->default_validation_class)) {
        return true;
    }

    if (cfdef->comparator_type != newcfdef->comparator_type) {
        return true;
    }

    if (cfdef->column_metadata.size() != newcfdef->column_metadata.size()) {
        return true;
    }

    std::vector<ColumnDef>::iterator cfdef_it = cfdef->column_metadata.begin();
    std::vector<ColumnDef>::iterator newcfdef_it;

    bool schema_changed;
    for(; cfdef_it != cfdef->column_metadata.end(); cfdef_it++) {
        schema_changed = true;
        for(newcfdef_it = newcfdef->column_metadata.begin();
            newcfdef_it != newcfdef->column_metadata.end(); newcfdef_it++) {
            if(cfdef_it->name == newcfdef_it->name &&
                cfdef_it->validation_class == newcfdef_it->validation_class) {
                schema_changed = false;
                break;
             }
        }
        if(schema_changed) {
            return true;
        }
    }

    return false;
}

bool ThriftIfImpl::Db_AsyncAddColumn(ThriftIfColList &cl) {
    GenDb::ColList *new_colp(cl.gendb_cl);
    if (new_colp == NULL) {
        IncrementErrors(
            GenDb::IfErrors::ERR_WRITE_COLUMN);
        THRIFTIF_LOG_ERR("No Column Information");
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
            THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE((it->name->size() == 1) &&
                                           (it->value->size() == 1));
            THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE(
                cftype != GenDb::NewCf::COLUMN_FAMILY_NOSQL);
            cftype = GenDb::NewCf::COLUMN_FAMILY_SQL;
            // Column Name
            std::string col_name;
            try {
                col_name = boost::get<std::string>(it->name->at(0));
            } catch (boost::bad_get& ex) {
                THRIFTIF_LOG_ERR(cfname << "Column Name FAILED " << ex.what());
            }
            c.__set_name(col_name);
            // Column Value
            std::string col_value;
            DbDataValueToStringNonComposite(col_value, it->value->at(0));
            c.__set_value(col_value);
            // Timestamp and TTL
            c.__set_timestamp(ts);
            if (it->ttl > 0) {
                c.__set_ttl(it->ttl);
            }
            c_or_sc.__set_column(c);
            mutation.__set_column_or_supercolumn(c_or_sc);
            mutations.push_back(mutation);
        } else if (it->cftype_ == GenDb::NewCf::COLUMN_FAMILY_NOSQL) {
            THRIFTIF_EXPECT_TRUE_ELSE_RETURN_FALSE(
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
            if (it->ttl > 0) {
                c.__set_ttl(it->ttl);
            }
            c_or_sc.__set_column(c);
            mutation.__set_column_or_supercolumn(c_or_sc);
            mutations.push_back(mutation);
        } else {
            IncrementErrors(
                GenDb::IfErrors::ERR_WRITE_COLUMN);
            UpdateCfWriteFailStats(cfname);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << ": Invalid CFtype: " <<
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

void ThriftIfImpl::Db_BatchAddColumn(bool done) {
    THRIFTIF_BEGIN_TRY {
        client_->batch_mutate(mutation_map_,
            org::apache::cassandra::ConsistencyLevel::ONE);
    } THRIFTIF_END_TRY_LOG_INTERNAL(integerToString(mutation_map_.size()),
          false, false, true, GenDb::IfErrors::ERR_WRITE_BATCH_COLUMN,
          GenDb::GenDbIfStats::TABLE_OP_NONE)
    mutation_map_.clear();
}

bool ThriftIfImpl::Db_AddColumn(std::auto_ptr<GenDb::ColList> cl) {
    tbb::mutex::scoped_lock lock(q_mutex_);
    if (!Db_IsInitDone() || !q_.get()) {
        UpdateCfWriteFailStats(cl->cfname_);
        return false;
    }
    ThriftIfColList qentry;
    qentry.gendb_cl = cl.release();
    q_->Enqueue(qentry);
    return true;
}

bool ThriftIfImpl::Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl) {
    ThriftIfColList qentry;
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

bool ThriftIfImpl::ColListFromColumnOrSuper(GenDb::ColList *ret,
        const std::vector<cassandra::ColumnOrSuperColumn>& result,
        const std::string& cfname) {
    ThriftIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND");
    }
    if (cf->cftype_ == NewCf::COLUMN_FAMILY_SQL) {
        NewColVec& columns = ret->columns_;
        std::vector<cassandra::ColumnOrSuperColumn>::const_iterator citer;
        for (citer = result.begin(); citer != result.end(); citer++) {
            GenDb::DbDataValue res;
            if (!DbDataValueFromString(res, cfname, citer->column.name,
                citer->column.value)) {
                THRIFTIF_LOG_ERR(cfname << ": " << citer->column.name <<
                    " Decode FAILED");
                continue;
            }
            GenDb::NewCol *col(new GenDb::NewCol(citer->column.name, res, 0));
            columns.push_back(col);
        }
    } else if (cf->cftype_ == NewCf::COLUMN_FAMILY_NOSQL) {
        NewColVec& columns = ret->columns_;
        std::vector<cassandra::ColumnOrSuperColumn>::const_iterator citer;
        for (citer = result.begin(); citer != result.end(); citer++) {
            std::auto_ptr<GenDb::DbDataValueVec> name(new GenDb::DbDataValueVec);
            if (!DbDataValueVecFromString(*name, cf->comparator_type,
                citer->column.name)) {
                THRIFTIF_LOG_ERR(cfname << ": Column Name Decode FAILED");
                continue;
            }
            std::auto_ptr<GenDb::DbDataValueVec> value(new GenDb::DbDataValueVec);
            if (!DbDataValueVecFromString(*value, cf->default_validation_class,
                    citer->column.value)) {
                THRIFTIF_LOG_ERR(cfname << ": Column Value Decode FAILED");
                continue;
            }
            GenDb::NewCol *col(new GenDb::NewCol(name.release(), value.release(), 0));
            columns.push_back(col);
        }
    }
    return true;
}

bool ThriftIfImpl::Db_GetRow(GenDb::ColList *ret, const std::string& cfname,
        const DbDataValueVec& rowkey) {
    ThriftIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        IncrementErrors(
            GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cfname);
        THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND");
    }
    std::string key;
    if (!ConstructDbDataValueKey(key, cf, rowkey)) {
        UpdateCfReadFailStats(cfname);
        THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << ": Key encode FAILED");
    }
    // Slicer has start_column and end_column as null string, which
    // means return all columns
    cassandra::SliceRange slicer;
    cassandra::SlicePredicate slicep;
    slicep.__set_slice_range(slicer);
    cassandra::ColumnParent cparent;
    cparent.column_family.assign(cfname);
    std::vector<cassandra::ColumnOrSuperColumn> result;
    THRIFTIF_BEGIN_TRY {
        client_->get_slice(result, key, cparent, slicep,
            ConsistencyLevel::ONE);
    } THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(cfname, false, false, false,
        GenDb::IfErrors::ERR_READ_COLUMN,
        GenDb::GenDbIfStats::TABLE_OP_READ_FAIL)
    bool success = ColListFromColumnOrSuper(ret, result, cfname);
    if (success) {
        UpdateCfReadStats(cfname);
    } else {
        UpdateCfReadFailStats(cfname);
    }
    return success;
}

bool ThriftIfImpl::Db_GetMultiRow(GenDb::ColListVec *ret,
    const std::string& cfname,
    const std::vector<DbDataValueVec>& rowkeys) {
    return Db_GetMultiRow(ret, cfname, rowkeys, GenDb::ColumnNameRange());
}

bool ThriftIfImpl::Db_GetMultiRow(GenDb::ColListVec *ret,
    const std::string& cfname,
    const std::vector<DbDataValueVec>& rowkeys,
    const GenDb::ColumnNameRange& crange) {
    ThriftIfCfInfo *info;
    GenDb::NewCf *cf;
    if (!Db_GetColumnfamily(&info, cfname) || !(cf = info->cf_.get())) {
        IncrementErrors(
            GenDb::IfErrors::ERR_READ_COLUMN_FAMILY);
        UpdateCfReadFailStats(cfname);
        THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << ": NOT FOUND");
    }
    // Populate column name range slice
    cassandra::SliceRange slicer;
    cassandra::SlicePredicate slicep;
    if (!crange.IsEmpty()) {
        // Column range specified, set appropriate variables
        std::string start_string;
        if (!ConstructDbDataValueColumnName(start_string, cf,
            crange.start_)) {
            UpdateCfReadFailStats(cfname);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cfname <<
                ": Column Name Range Start encode FAILED");
        }
        std::string finish_string;
        if (!ConstructDbDataValueColumnName(finish_string, cf,
            crange.finish_)) {
            UpdateCfReadFailStats(cfname);
            THRIFTIF_LOG_ERR_RETURN_FALSE(cfname <<
                ": Column Name Range Finish encode FAILED");
        }
        slicer.__set_start(start_string);
        slicer.__set_finish(finish_string);
        slicer.__set_count(crange.count_);
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
                THRIFTIF_LOG_ERR_RETURN_FALSE(cfname << "(" << i <<
                    "): Key encode FAILED");
            }
            keys.push_back(key);
        }
        std::map<std::string, std::vector<ColumnOrSuperColumn> > ret_c;
        THRIFTIF_BEGIN_TRY {
            client_->multiget_slice(ret_c, keys, cparent, slicep, ConsistencyLevel::ONE);
        } THRIFTIF_END_TRY_RETURN_FALSE_INTERNAL(cfname, false, false, false,
            GenDb::IfErrors::ERR_READ_COLUMN,
            GenDb::GenDbIfStats::TABLE_OP_READ_FAIL)
        // Update stats
        UpdateCfReadStats(cfname);
        // Convert result
        for (std::map<std::string,
                 std::vector<ColumnOrSuperColumn> >::iterator it = ret_c.begin();
             it != ret_c.end(); it++) {
            std::auto_ptr<GenDb::ColList> col_list(new GenDb::ColList);
            if (!DbDataValueVecFromString(col_list->rowkey_,
                cf->key_validation_class, it->first)) {
                THRIFTIF_LOG_ERR(cfname << ": Key decode FAILED");
                continue;
            }
            if (!ColListFromColumnOrSuper(col_list.get(), it->second, cfname)) {
                THRIFTIF_LOG_ERR(cfname << ": Column decode FAILED");
            }
            ret->push_back(col_list);
        }
    } // while loop
    return true;
}

bool ThriftIfImpl::Db_GetQueueStats(uint64_t *queue_count, uint64_t *enqueues) const {
    if (q_.get() != NULL) {
        *queue_count = q_->Length();
        *enqueues = q_->NumEnqueues();
    } else {
        *queue_count = 0;
        *enqueues = 0;
    }
    return true;
}

bool ThriftIfImpl::Db_GetStats(std::vector<DbTableInfo> *vdbti, DbErrors *dbe) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.GetDiffs(vdbti, dbe);
    return true;
}

void ThriftIfImpl::UpdateCfWriteStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementTableWrite(cf_name);
}

void ThriftIfImpl::UpdateCfWriteFailStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementTableWriteFail(cf_name);
}

void ThriftIfImpl::UpdateCfReadStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementTableRead(cf_name);
}

void ThriftIfImpl::UpdateCfReadFailStats(const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementTableReadFail(cf_name);
}

void ThriftIfImpl::UpdateCfStats(GenDb::GenDbIfStats::TableOp op,
    const std::string &cf_name) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementTableStats(op, cf_name);
}

void ThriftIfImpl::IncrementErrors(GenDb::IfErrors::Type etype) {
    tbb::mutex::scoped_lock lock(smutex_);
    stats_.IncrementErrors(etype);
}

template<>
size_t ThriftIfImpl::ThriftIfQueue::AtomicIncrementQueueCount(
    ThriftIfImpl::ThriftIfColList *colList) {
    GenDb::ColList *gcolList(colList->gendb_cl);
    size_t size(gcolList->GetSize());
    return count_.fetch_and_add(size) + size;
}

template<>
size_t ThriftIfImpl::ThriftIfQueue::AtomicDecrementQueueCount(
    ThriftIfImpl::ThriftIfColList *colList) {
    GenDb::ColList *gcolList(colList->gendb_cl);
    size_t size(gcolList->GetSize());
    return count_.fetch_and_add(0-size) - size;
}

bool ThriftIfImpl::set_keepalive() {

    int keepalive_idle_sec = keepaliveIdleSec;
    int keepalive_intvl_sec = keepaliveIntvlSec;
    int keepalive_probe_count = keepaliveProbeCount;
    int tcp_user_timeout_ms = tcpUserTimeoutMs;//30 seconds

    boost::shared_ptr<TSocket> tsocket =
        boost::dynamic_pointer_cast<TSocket>(socket_);

    //Set KeepAlive for the client connections to cassandra
    int optval = 1;
    int socket_fd = tsocket->getSocketFD();
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &optval,
                   sizeof(int)) < 0) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Cannot set SO_KEEPALIVE option on"
                                   "listen socket (" << strerror(errno) << ")");
    }
#ifdef TCP_KEEPIDLE
    //Set the KEEPALIVE IDLE time
    if (setsockopt(socket_fd, SOL_TCP, TCP_KEEPIDLE, &keepalive_idle_sec,
                   sizeof(keepalive_idle_sec)) < 0) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Cannot set TCP_KEEPIDLE option on"
                                   "listen socket (" << strerror(errno) << ")");
    }
#endif
#ifdef TCP_KEEPINTVL
    //Set the KEEPALIVE INTERVAL time between probes
    if (setsockopt(socket_fd, SOL_TCP, TCP_KEEPINTVL, &keepalive_intvl_sec,
                   sizeof(keepalive_intvl_sec)) < 0) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Cannot set TCP_KEEPINTVL option on"
                                   "listen socket (" << strerror(errno) << ")");
    }
#endif
#ifdef TCP_KEEPCNT
    //Set the KEEPALIVE PROBE COUNT
    if (setsockopt(socket_fd, SOL_TCP, TCP_KEEPCNT, &keepalive_probe_count,
                   sizeof(keepalive_probe_count)) < 0) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Cannot set TCP_KEEPINTVL option on"
                                   "listen socket (" << strerror(errno) << ")");
    }
#endif
#ifdef TCP_USER_TIMEOUT
    //Set TCP_USER_TIMEOUT so pending data on sockets
    if (setsockopt(socket_fd, SOL_TCP, TCP_USER_TIMEOUT, &tcp_user_timeout_ms,
                   sizeof(tcp_user_timeout_ms)) < 0) {
        THRIFTIF_LOG_ERR_RETURN_FALSE("Cannot set TCP_USER_TIMEOUT_MS option on"
                                   "listen socket (" << strerror(errno) << ")");
    }
#endif
    return true;
}
