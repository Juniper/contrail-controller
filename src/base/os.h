/* System dependent (re)definitions */
#ifndef _agent_os_h_
#define _agent_os_h_

/* Requires to be preceeded by inclusion of:
   sys/types.h
   net/ethernet.h
*/

#if defined(__FreeBSD__)
# define SO_RCVBUFFORCE SO_RCVBUF
# define SIOCGIFHWADDR SIOCGIFADDR
#endif

/* This is universal union that is giving octet level access to MAC
   address */
union ether_addr_octets
{
    struct ether_addr addr;
    u_int8_t octets[ETHER_ADDR_LEN];
};

#endif /* ndef _agent_os_h_ */

