/* $Id: ordinal_private.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * ordinal_private.h - Ordinal assignment private definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __ORDINAL_PRIVATE_H__
#define __ORDINAL_PRIVATE_H__

/*
 * Ordinal context.  It contains a bit vector that we use to keep
 * track of the ordinal space.
 */
typedef struct ord_context_ {
    bit_vector ord_vector;        /* Bit vector */
    ord_compact_option ord_compact;    /* Compact mode */
} ord_context;

#endif /* __ORDINAL_PRIVATE_H__ */
