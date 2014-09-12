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

#endif /* ndef _agent_os_h_ */

