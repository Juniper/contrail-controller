/* $Id: gmp_trace.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmp_trace.c - GMP role-independent trace support
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


/*
 * gmp_proto_string
 *
 * Returns a string, given the protocol (GMP_PROTO_xxxx)
 */
const char *
gmp_proto_string (gmp_proto proto)
{
    switch (proto) {
      case GMP_PROTO_IGMP:
	return "IGMP";
      case GMP_PROTO_MLD:
	return "MLD";
      default:
	return "Invalid";
    }
}


/*
 * gmp_filter_mode_string
 *
 * Returns a string given the filter mode (GMP_FILTER_MODE_xxx)
 */
const char *
gmp_filter_mode_string (gmp_filter_mode mode)
{
    switch (mode) {
      case GMP_FILTER_MODE_EXCLUDE:
	return "Exclude";
      case GMP_FILTER_MODE_INCLUDE:
	return "Include";
      default:
	return "Invalid";
    }
}


/*
 * gmp_generic_version_string
 *
 * Returns a string given the generic packet version.
 */
const char *
gmp_generic_version_string (gmp_version ver)
{
    switch (ver) {
      case GMP_VERSION_BASIC:
	return "Basic";
      case GMP_VERSION_LEAVES:
	return "Leaves";
      case GMP_VERSION_SOURCES:
	return "Sources";
      default:
	return "Invalid";
    }
}


/*
 * gmp_report_type_string
 *
 * Returns a string given the report type
 */
const char *
gmp_report_type_string (gmp_report_rectype type)
{
    switch (type) {
      case GMP_RPT_IS_IN:
	return "IS_IN";
      case GMP_RPT_IS_EX:
	return "IS_EX";
      case GMP_RPT_TO_IN:
	return "TO_IN";
      case GMP_RPT_TO_EX:
	return "TO_EX";
      case GMP_RPT_ALLOW:
	return "ALLOW";
      case GMP_RPT_BLOCK:
	return "BLOCK";
      default:
	return "Invalid";
    }
}
