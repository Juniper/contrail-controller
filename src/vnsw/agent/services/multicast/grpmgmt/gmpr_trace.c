/* $Id: gmpr_trace.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmpr_trace.c - GMP router-side trace support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmp_trace.h"
#include "gmpr_trace.h"


/*
 * gmpr_client_notif_string
 *
 * Returns a string given the client notification type.
 */
const char *
gmpr_client_notif_string (gmpr_client_notification_type type)
{
    switch (type) {
      case GMPR_NOTIF_GROUP_DELETE:
	return "Group Del";
      case GMPR_NOTIF_GROUP_ADD_EXCL:
	return "Add Group Excl";
      case GMPR_NOTIF_GROUP_ADD_INCL:
	return "Add Group Incl";
      case GMPR_NOTIF_ALLOW_SOURCE:
	return "Allow Source";
      case GMPR_NOTIF_BLOCK_SOURCE:
	return "Block Source";
      case GMPR_NOTIF_GROUP_STATE:
	return "Group State";
      case GMPR_NOTIF_REFRESH_END:
	return "Refresh End";
      default:
	return "Invalid";
    }
}


/*
 * gmpr_host_notif_string
 *
 * Returns a string given the host notification type.
 */
const char *
gmpr_host_notif_string (gmpr_client_host_notification_type type)
{
    switch (type) {
      case GMPR_NOTIF_HOST_UNKNOWN:
	return "Unknown";
      case GMPR_NOTIF_HOST_JOIN:
	return "Join";
      case GMPR_NOTIF_HOST_LEAVE:
	return "Leave";
      case GMPR_NOTIF_HOST_TIMEOUT:
	return "Timeout";
      case GMPR_NOTIF_HOST_IFDOWN:
	return "Intf Down";
      default:
	return "Invalid";
    }
}
