/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp_debug.h"

#if defined(__BGP_DEBUG__)

#include <cxxabi.h>
#include "base/logging.h"
#include "base/util.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_table.h"
#include "routing-instance/routing_instance.h"

using namespace std;

bool BgpDebug::enable_ = (getenv("BGP_DEBUG") != NULL);
bool BgpDebug::enable_stack_trace_ = (getenv("BGP_DEBUG_STACK_TRACE") != NULL);

#define COLOR_RESET       "\e[m"
#define COLOR_BLACK       "\e[0;30m"
#define COLOR_RED         "\e[0;31m"
#define COLOR_GREEN       "\e[0;32m"
#define COLOR_BROWN       "\e[0;33m"
#define COLOR_BLUE        "\e[0;34m"
#define COLOR_MAGENTA     "\e[0;35m"
#define COLOR_CYAN        "\e[0;36m"
#define COLOR_GRAY        "\e[0;37m"
#define COLOR_DARKGRAY    "\e[1;30m"
#define COLOR_LIGHTBLUE   "\e[1;34m"
#define COLOR_LIGHTGREEN  "\e[1;32m"
#define COLOR_LIGHTCYAN   "\e[1;36m"
#define COLOR_LIGHTRED    "\e[1;31m"
#define COLOR_LIGHTPURPLE "\e[1;35m"
#define COLOR_YELLOW      "\e[1;33m"
#define COLOR_WHITE       "\e[1;37m"

void BgpDebug::Debug(DBTable *t, DBEntry *r, IPeer *peer, const char *file,
                     const char *function, int line, char *msg) {

    if (!enable_) return;
    if (enable_stack_trace_) BackTrace::Log();

    char *buf;
    char *str;

    BgpTable *table = dynamic_cast<BgpTable *>(t);
    BgpRoute *rt = dynamic_cast<BgpRoute *>(r);

    size_t buf_size = 1024 * 1024;
    buf = static_cast<char *>(malloc(buf_size));
    str = buf;

    const char *server_name = (peer && peer->server()) ?
        peer->server()->ToString().c_str() : "Unknown";
    const char *instance_name =
        (table && table->routing_instance()) ?
            table->routing_instance()->name().c_str() : "Unknown";
    const char *table_name = table ?
        Address::FamilyToString(table->family()).c_str() : "Unknown";
    const char *peer_name = peer ? peer->ToString().c_str() : "Unknown";
    const char *route_name = rt ? rt->ToString().c_str() : "Unknown";

    str += snprintf(str, buf_size - (str - buf), "\n%s:%s:%d: ",
                    file, function, line); // class: typeid(*this).name()
    if (strcmp(server_name, "Unknown")) {
        str += snprintf(str, buf_size    - (str - buf),
                        COLOR_BROWN "Serv %s(%p); ",
                        server_name, peer->server());
    }
    if (strcmp(instance_name, "Unknown")) {
        str += snprintf(str, buf_size    - (str - buf),
                        COLOR_RED "Inst %s(%p); ", instance_name,
                        table->routing_instance());
    }
    if (strcmp(table_name, "Unknown")) {
        if (strcmp(table_name, "inet"))
            str += snprintf(str, buf_size    - (str - buf),
                            COLOR_LIGHTBLUE "Tabl %s(%p); ",
                            table_name, table);
        else
            str += snprintf(str, buf_size    - (str - buf),
                            COLOR_BLUE "Tabl %s(%p); ",
                            table_name, table);
    }
    if (strcmp(peer_name, "Unknown")) {
        str += snprintf(str, buf_size    - (str - buf),
                        COLOR_GREEN "Peer %s(%s)(%p), ", peer_name,
                        peer->GetStateName().c_str(), peer);
    }
    if (strcmp(route_name, "Unknown")) {
        str += snprintf(str, buf_size    - (str - buf),
                        COLOR_MAGENTA "Route %s(%p)", route_name, rt);
    }
    str += snprintf(str, buf_size    - (str - buf), COLOR_CYAN "\n    %s", msg);
    str += snprintf(str, buf_size    - (str - buf), "\n"
                    "------------------------------------------"
                    "------------------------------------------");
    str += snprintf(str, buf_size    - (str - buf), COLOR_RESET);

    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL, buf);
    free(buf);
}

#endif // __BGP_DEBUG__
