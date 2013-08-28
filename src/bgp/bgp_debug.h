/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_DEBUG_H__

#define __BGP_DEBUG_H__

#if !defined(__BGP_DEBUG__)

#define BGP_DEBUG(...)

#else

class DBEntry;
class DBTable;
class IPeer;

class BgpDebug {
public:
    static void Debug(DBTable *t, DBEntry *r, IPeer *p,
                      const char *file, const char *function, int line,
                      char *msg);
    static bool Enabled() { return enable_; }
private:
    static bool enable_;
    static bool enable_stack_trace_;
};

//
// Generic debug macro to print a peer, instance, table, and route information
// if provided. (Use NULL to omit a partiuclar field). It also prints caller
// file name, function name, class name and line number
//
// TODO: Have a simple way to filter messages such as filter based on server,
// instance, table, peer, route, etc.
//
//
#ifdef __APPLE__
#define __FUNC__ __PRETTY_FUNCTION__
#else
#define __FUNC__ __FUNCTION__
#endif

#define BGP_DEBUG(dtable, dpeer, droute, format, args...)                      \
do {                                                                           \
    if (!BgpDebug::Enabled()) break;                                           \
                                                                               \
    char msg[1024];                                                            \
    snprintf(msg, sizeof(msg), format, ##args);                                \
                                                                               \
    BgpDebug::Debug(static_cast<DBTable *>(dtable),                            \
                    static_cast<DBEntry *>(droute),                            \
                    static_cast<IPeer *>(dpeer),                               \
                    __FILE__, __FUNC__, __LINE__, msg);                        \
} while (0)

#endif // __BGP_DEBUG__

#endif // __BGP_DEBUG_H__
