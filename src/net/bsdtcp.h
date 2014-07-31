#ifndef _agent_bsdtcp_h_
#define _agent_bsdtcp_h_

#if defined(__linux__)
# if !defined(_BSD_SOURCE)
#  define BSDTCP_UNDEF__BSD_SOURCE
#  define _BSD_SOURCE 1
# endif
# if defined(_POSIX_SOURCE)
#  define BSDTCP_DEF__POSIX_SOURCE
#  undef _POSIX_SOURCE
# endif
# if defined(_POSIX_C_SOURCE)
#  define BSDTCP_DEF__POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
# endif
# if defined(_XOPEN_SOURCE)
#  define BSDTCP_DEF__XOPEN_SOURCE
#  undef _XOPEN_SOURCE
# endif
# if defined(_GNU_SOURCE)
#  define BSDTCP_DEF__GNU_SOURCE
#  undef _GNU_SOURCE
# endif
# if defined(_SVID_SOURCE)
#  define BSDTCP__SVID_SOURCE
#  undef _SVID_SOURCE
# endif
# if !defined(__FAVOR_BSD)
#  define BSDTCP_UNDEF___FAVOR_BSD
#  define __FAVOR_BSD 1
# endif
#endif

#include <netinet/tcp.h>

#if defined(BSDTCP_UNDEF__BSD_SOURCE)
# undef _BSD_SOURCE
# undef BSDTCP_UNDEF__BSD_SOURCE
#endif
#if defined(BSDTCP_DEF__POSIX_SOURCE) && !defined(_POSIX_SOURCE)
# define _POSIX_SOURCE
# undef BSDTCP_DEF__POSIX_SOURCE
#endif
#if defined(BSDTCP_DEF__POSIX_C_SOURCE) && !defined(_POSIX_C_SOURCE)
# define _POSIX_SOURCE
# undef BSDTCP_DEF__POSIX_SOURCE
#endif
#if defined(BSDTCP_DEF__POSIX_C_SOURCE) && !defined(_POSIX_C_SOURCE)
# define _POSIX_C_SOURCE
# undef BSDTCP_DEF__POSIX_C_SOURCE
#endif
#if defined(BSDTCP_DEF__XOPEN_SOURCE) && !defined(_XOPEN_SOURCE)
# define _XOPEN_SOURCE
# undef BSDTCP_DEF__XOPEN_SOURCE
#endif
#if defined(BSDTCP_DEF__GNU_SOURCE) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE
# undef BSDTCP_DEF__GNU_SOURCE
#endif
#if defined(BSDTCP_DEF__SVID_SOURCE) && !defined(_SVID_SOURCE)
# define _SVID_SOURCE
# undef BSDTCP_DEF__SVID_SOURCE
#endif
#if defined(BSDTCP_UNDEF___FAVOR_BSD)
# undef __FAVOR_BSD
# undef BSDTCP_DEF___FAVOR_BSD
#endif

#endif /* ndef _agent_bsdtcp+h+ */
