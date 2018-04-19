/* $Id: gmp_private.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmp_private.h - Private definitions for GMP Host/Router support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module defines the shared private data structures for GMP host
 * and router support.  This file should only be used internally by GMP.
 */

#ifndef __GMP_PRIVATE_H__
#define __GMP_PRIVATE_H__

#include "bitvector.h"
#include "ordinal.h"

/*
 * EMBEDDED_STRUCT_TO_STRUCT
 *
 * This macro creates an inline function to translate the address of
 * one structure embedded in another to an address of the enclosing
 * structure.  The inline returns NULL if the address passed to it was
 * NULL.
 */
#define EMBEDDED_STRUCT_TO_STRUCT(procname, outerstruct, innerstruct, field) \
    static inline outerstruct * procname (innerstruct *ptr) \
    { \
    if (ptr)\
        return ((outerstruct *) (((uint8_t *) ptr) - \
                     offsetof(outerstruct, field))); \
     return NULL; \
    }

/*
 * Generic GMP version.  This refers to the *capabilities* of a version,
 * rather than the version number itself, since MLD and IGMP have different
 * protocol version numbers that correspond to the same functionality.
 */
typedef enum {
    GMP_VERSION_INVALID = 0,        /* Invalid version */
    GMP_VERSION_BASIC,            /* Joins only */
    GMP_VERSION_LEAVES,            /* Joins and leaves */
    GMP_VERSION_SOURCES            /* Source information */
} gmp_version;

#define GMP_VERSION_DEFAULT GMP_VERSION_SOURCES    /* Default version */
#define GMP_ROBUSTNESS_DEFAULT 2    /* Retransmission count */
#define GMP_UNSOL_RPT_IVL_DEFAULT 1000    /* Unsolicited report delay (msec) */

#ifndef MSECS_PER_SEC
#define MSECS_PER_SEC 1000        /* Milliseconds per second */
#endif


/*
 * Address list alloc entry callback type
 *
 * This defines the callback to allocate an address list entry.
 *
 * Returns a pointer to an address list entry, or NULL if out of memory.
 */
typedef struct gmp_addr_list_entry_ * (*gmp_addr_list_alloc_func)(void *ctx);


/*
 * Address list free entry callback type
 *
 * This defines the callback to free an address list entry.
 */
typedef void
    (*gmp_addr_list_free_func)(struct gmp_addr_list_entry_ *addr_entry);


/*
 * Address catalog
 *
 * For efficiency, and to make the use of bit vectors possible, all of the
 * addresses referenced by a group are stored in a catalog, and all references
 * to them are done by their ordinal numbers.
 *
 * The catalog consists of a set of entries with two patricia trees
 * intertwined--one by address and one by ordinal.
 *
 * Each entry contains the address, its ordinal, and a reference count.  When
 * the reference count goes to zero, the entry is freed.
 *
 * Note that the address ordinal tree may not be in lexicographic order
 * for walking!
 */
typedef struct gmp_addr_catalog_
{
    gmpx_patroot *adcat_addr_root;    /* Root of address tree */
    gmpx_patroot *adcat_ord_root;    /* Root of ordinal tree */
    ordinal_handle adcat_ord_handle;    /* Ordinal space handle */
    uint32_t adcat_addrlen;        /* Address length */
} gmp_addr_catalog;

typedef struct gmp_addr_cat_entry_
{
    gmpx_patnode adcat_ent_addr_node;    /* Address tree node */
    gmpx_patnode adcat_ent_ord_node;    /* Ordinal tree node */
    gmp_addr_string adcat_ent_addr;    /* The address itself */
    ordinal_t adcat_ent_ord;        /* Address ordinal */
    int32_t adcat_ent_refcount;        /* Reference count */
} gmp_addr_cat_entry;

GMPX_PATNODE_TO_STRUCT(gmp_addr_patnode_to_addr_cat_entry, gmp_addr_cat_entry,
               adcat_ent_addr_node);
GMPX_PATNODE_TO_STRUCT(gmp_ord_patnode_to_addr_cat_entry, gmp_addr_cat_entry,
               adcat_ent_ord_node);


/*
 * Address vector
 *
 * An address vector is a bit vector representing a set of addresses.
 * Each address within a group is assigned an ordinal, and the bit
 * corresponding to that ordinal is set in the bit vector if that address
 * is part of the set.
 *
 * This vector alone is sufficient to represent a list of addresses in its
 * simplest form.
 *
 * If the catalog pointer is nonzero, vector operations will lock and unlock
 * catalog entries as necessary.  If the catalog pointer is NULL, this vector
 * is a scratch vector, and no locking is done.
 */
typedef struct gmp_addr_vect_
{
    bit_vector av_vector;        /* Bit vector of addresses */
    gmp_addr_catalog *av_catalog;    /* Address catalog */
} gmp_addr_vect;


/*
 * gmp_addr_vector
 *
 * Returns a pointer to the bit vector in an address vector, or NULL if the
 * address vector pointer is NULL.
 */
static inline bit_vector *
gmp_addr_vector (gmp_addr_vect *addr_vect)
{
    if (addr_vect)
    return &addr_vect->av_vector;
    return NULL;
}


/*
 * Address list
 *
 * An address list represents a set of addresses, along with some
 * application-specific other information.  It is structured in two
 * ways--as a patricia tree (so that we can quickly find single
 * entries) and as a simple thread (so as to be able to traverse
 * unordered entries quickly.)
 *
 * The address list also has an embedded address vector;  the vector and
 * the addresses on the list represent the same information (and thus must
 * remain consistent!)
 *
 * In addition, there is a second thread to act as a transmit list.
 * Callers to this code are responsible for manipulating this thread;
 * they are free to add and delete entries from the thread as they
 * wish.  The only thing this module does is to delink an entry before
 * freeing it.
 *
 * The list contains pointers to entry allocation/free routines in order
 * to create specific entry types for eacy list.
 *
 * Each list entry contains the source address ordinal, a patricia
 * node, and two thread links and is typically embedded in a separate
 * data structure.  Extreme care must be taken to ensure that the
 * varying types of entry are not confused.
 *
 * Address lists may be embedded in another structure, or may be allocated
 * independently.
 *
 * Note that any address list head MUST be initialized via
 * gmp_addr_list_init() before use!
 *
 * Note also that the patricia tree may not be in lexicographic order
 * for walking (due to endianness.)
 */

/* Address list head */

typedef struct gmp_addr_list_ {
    gmp_addr_list_alloc_func addr_alloc; /* Allocation routine */
    gmp_addr_list_free_func addr_free;    /* Free function */
    void *addr_context;            /* Context for alloc/free */
    gmp_addr_vect addr_vect;        /* Address vector */
    gmpx_patroot *addr_list_root;    /* Root of patricia tree */
    thread addr_list_head;        /* Head of address thread */
    int addr_count;            /* Number of addresses in list */
    thread addr_list_xmit_head;        /* Head of transmit list */
    int xmit_addr_count;        /* Number of addresses in xmit list */
} gmp_addr_list;

/* Address list entry */

typedef struct gmp_addr_list_entry_ {
    gmp_addr_list *addr_ent_list;    /* Pointer to owning address list */
    gmpx_patnode addr_ent_patnode;    /* Patricia tree node */
    thread addr_ent_thread;        /* Entry on list thread */
    thread addr_ent_xmit_thread;    /* Entry on transmit thread */
    ordinal_t addr_ent_ord;        /* Address ordinal */
} gmp_addr_list_entry;

GMPX_PATNODE_TO_STRUCT(gmp_patnode_to_addr_list_entry, gmp_addr_list_entry,
               addr_ent_patnode);
THREAD_TO_STRUCT(gmp_thread_to_addr_list_entry, gmp_addr_list_entry,
         addr_ent_thread);
THREAD_TO_STRUCT(gmp_xmit_thread_to_addr_list_entry, gmp_addr_list_entry,
         addr_ent_xmit_thread);


/*
 * Generic packet format
 *
 * This set of data structures represent the generic form of a GMP
 * packet, independent of protocol (IGMP/MLD) and version.
 */


/*
 * Query packet
 *
 * The query packet refers to at most one group, and includes an
 * address list of sources (if any.)
 *
 * We use different methods for storing the source address list,
 * depending on whether the packet is being transmitted or being
 * received.  On transmit we use a pointer to an address list;  this
 * is both handy (because the address list already exists) and is
 * necessary (to keep track of which sources are transmitted and which
 * are not, since they may not all fit in a packet.)
 *
 * On receive we use a simple address thread, because we can't create
 * an address list until we are within the realm of the receiving client,
 * as the associated address catalog necessarily belongs to the client.
 */
typedef struct gmp_query_packet_ {
    uint32_t gmp_query_max_resp;    /* Max resp value, in msec */
    gmp_addr_string gmp_query_group;    /* Group address */
    void *gmp_query_group_id;        /* Opaque group ID */
    boolean gmp_query_group_query;    /* TRUE if a group query */
    boolean gmp_query_suppress;        /* Suppress router-side processing */
    uint32_t gmp_query_qrv;        /* Querier's robustness variable */
    uint32_t gmp_query_qqi;        /* Querier's query interval (msec) */
    gmp_addr_list *gmp_query_xmit_srcs;    /* List of sources (xmit) */
    gmp_addr_thread *gmp_query_rcv_srcs; /* Sources (receive) */
} gmp_query_packet;


/*
 * Report packet
 *
 * The report packet consists of a linked list of group records.
 *
 * Each group record includes an address list of sources (if any.)
 */
typedef struct gmp_report_packet_ {
    thread gmp_report_group_head;    /* Head of thread of group records */
    uint32_t gmp_report_group_count;    /* Number of group records */
} gmp_report_packet;


/*
 * Report record types
 *
 * We assume that these report type codes are identical in IGMP and MLD, and
 * use those protocol values as our internal form as well.
 */
typedef enum {
    GMP_RPT_INVALID = 0,        /* Invalid type */
    GMP_RPT_IS_IN = 1,            /* MODE_IS_INCLUDE */
    GMP_RPT_IS_EX = 2,            /* MODE_IS_EXCLUDE */
    GMP_RPT_TO_IN = 3,            /* CHANGE_TO_INCLUDE_MODE */
    GMP_RPT_TO_EX = 4,            /* CHANGE_TO_EXCLUDE_MODE */
    GMP_RPT_ALLOW = 5,            /* ALLOW_NEW_SOURCES */
    GMP_RPT_BLOCK = 6,            /* BLOCK_OLD_SOURCES */
    GMP_RPT_MIN = GMP_RPT_IS_IN,    /* Minimum value */
    GMP_RPT_MAX = GMP_RPT_BLOCK,    /* Maximum value */
} gmp_report_rectype;


/*
 * Report group record
 *
 * Like the query packet above, we use an address list to represent the
 * set of sources on transmit, and an address thread on receive.
 */
typedef struct gmp_report_group_record_ {
    void *gmp_rpt_group_id;        /* Opaque group ID */
    thread gmp_rpt_thread;        /* Entry on thread */
    gmp_report_rectype gmp_rpt_type;    /* Record type */
    gmp_addr_string gmp_rpt_group;    /* Group address */
    gmp_addr_list *gmp_rpt_xmit_srcs;    /* List of source addresses (xmit) */
    gmp_addr_thread *gmp_rpt_rcv_srcs;    /* List of source addresses (rcv) */
} gmp_report_group_record;

THREAD_TO_STRUCT(gmp_thread_to_report_group_record, gmp_report_group_record,
         gmp_rpt_thread);


typedef enum {
    GMP_QUERY_PACKET,            /* Query */
    GMP_REPORT_PACKET,            /* Report */
    GMP_NUM_PACKET_TYPES
} gmp_message_type;


/*
 * The generic packet type consists of a generic version, the packet type,
 * and a union of the other two structures.  The correct structure can
 * be determined by the message type.
 */
struct gmp_packet_ {
    gmp_version gmp_packet_version;    /* Packet version (generic) */
    gmp_message_type gmp_packet_type;    /* Packet type */
    gmp_addr_string gmp_packet_src_addr; /* Source address */
    gmp_addr_string gmp_packet_dest_addr; /* Destination address */
    gmp_proto gmp_packet_proto;        /* Protocol (IGMP/MLD) */
    gmpx_packet_attr gmp_packet_attr;    /* Opaque attribute */
    union {
    gmp_query_packet gmp_packet_query; /* Query packet */
    gmp_report_packet gmp_packet_report; /* Report packet */
    } gmp_packet_contents;
};


/*
 * Address thread
 *
 * This is a simple thread of addresses.  It is primarily used by external
 * clients to pass a set of source addresses in and out.  The packet parsing
 * routines use it as well.
 */
struct gmp_addr_thread_ {
    thread gmp_addr_thread_head;    /* Head of address thread */
    uint32_t gmp_addr_thread_count;    /* Count of entries */
};


/*
 * Address thread entry
 *
 * An entry in an address thread.
 */
struct gmp_addr_thread_entry_ {
    thread gmp_adth_thread;        /* Entry on address thread */
    gmp_addr_string gmp_adth_addr;    /* Address */
};
THREAD_TO_STRUCT(gmp_adth_thread_to_thread_entry, gmp_addr_thread_entry,
         gmp_adth_thread);


/*
 * Packet interface transmit callback type
 *
 * This defines the callback made from the generic packet I/O routines
 * when the I/O routines are ready to transmit a packet.
 *
 * Returns a pointer to a packet to send (which must be returned with the
 * packet_free callback) or NULL if there's nothing to send.
 *
 * The size of the transmit buffer is passed down so that the generic
 * packet build routines can make an estimate of how much to put into
 * the generic packet (only an estimate, since the exact coding is
 * unknown at that point.)
 */
typedef gmp_packet * (*gmp_xmit_callback_func)(gmpx_intf_id intf_id,
                           gmp_proto proto,
                           uint32_t buffer_len);


/*
 * Packet interface release callback type
 *
 * This defines the callback made from the generic packet I/O routines
 * when they are done with a packet structure.
 */
typedef void (*gmp_packet_free_callback_func)(gmp_packet *packet);


/*
 * Packet interface receive callback type
 *
 * This defines the callback made from the generic packet I/O routines
 * when a packet has been received.
 */
typedef void (*gmp_rcv_callback_func)(gmpx_intf_id intf_id, gmp_packet *pkt);


/*
 * Packet interface group-done callback type
 *
 * This defines the callback made from the generic packet I/O routines when
 * transmit processing is done for a group.
 */
typedef void (*gmp_group_done_callback_func)(void *group_id);


/*
 * Inlines
 */

/*
 * gmp_translate_version
 *
 * Translates from protocol ID and protocol version number to generic
 * version.
 */
static inline gmp_version
gmp_translate_version (gmp_proto proto, uint32_t version)
{
    /* Bloody dull. */

    switch (proto) {
      case GMP_PROTO_IGMP:
    switch (version) {
      case GMP_IGMP_VERSION_1:
        return GMP_VERSION_BASIC;
      case GMP_IGMP_VERSION_2:
        return GMP_VERSION_LEAVES;
      case GMP_IGMP_VERSION_3:
        return GMP_VERSION_SOURCES;
      default:
        return GMP_VERSION_INVALID;
    }

      case GMP_PROTO_MLD:
    switch (version) {
      case GMP_MLD_VERSION_1:
        return GMP_VERSION_LEAVES;
      case GMP_MLD_VERSION_2:
        return GMP_VERSION_SOURCES;
      default:
        return GMP_VERSION_INVALID;
    }

      default:
    return GMP_VERSION_INVALID;
    }
}

/*
 * gmp_untranslate_version
 *
 * Untranslates from generic version to protocol version number.
 */
static inline uint32_t
gmp_untranslate_version (gmp_proto proto, gmp_version version)
{
    switch (proto) {
    case GMP_PROTO_IGMP:
    switch (version) {
    case GMP_VERSION_BASIC:
        return GMP_IGMP_VERSION_1;
    case GMP_VERSION_LEAVES:
        return GMP_IGMP_VERSION_2;
    case GMP_VERSION_SOURCES:
        return GMP_IGMP_VERSION_3;
    default:
        return GMP_IGMP_VERSION_UNSPEC;
    }
    break;
    case GMP_PROTO_MLD:
    switch (version) {
    case GMP_VERSION_LEAVES:
        return GMP_MLD_VERSION_1;
    case GMP_VERSION_SOURCES:
        return GMP_MLD_VERSION_2;
    default:
        return GMP_MLD_VERSION_UNSPEC;
    }
    break;
    default:
    break;
    }

    /* Shouldn't reach here */
    return 0;
}

/*
 * gmp_addr_in_list
 *
 * Returns TRUE if the address ordinal is part of the specified address
 * list, or FALSE if not.
 */
static inline boolean
gmp_addr_in_list (gmp_addr_list *addr_list, ordinal_t ordinal)
{
    return (bv_bit_is_set(&addr_list->addr_vect.av_vector, ordinal));
}


/*
 * Externals
 */

/* gmp_addrlist.c */

extern void gmp_common_init(void);
extern int gmp_addr_vect_fill(gmp_addr_vect *addr_vect,
                  gmp_addr_thread *addr_thread);
extern void gmp_addr_list_clean(gmp_addr_list *addr_list);
extern void
    gmp_free_generic_addr_list_entry(gmp_addr_list_entry *addr_entry);
extern void gmp_flush_addr_list(gmp_addr_list *addr_list);
extern int gmp_add_addr_list_entry(gmp_addr_list *addr_list,
                   gmp_addr_list_entry *addr_entry);
extern gmp_addr_list_entry *gmp_lookup_addr_entry(gmp_addr_list *addr_list,
                          ordinal_t ordinal);
extern gmp_addr_list_entry
    *gmp_addr_list_next_entry(gmp_addr_list *list, gmp_addr_list_entry *prev);
extern void gmp_addr_list_init(gmp_addr_list *list, gmp_addr_catalog *catalog,
                   gmp_addr_list_alloc_func alloc_func,
                   gmp_addr_list_free_func free_func,
                   void *context);
extern void gmp_enqueue_xmit_addr_entry(gmp_addr_list_entry *addr_entry);
extern void gmp_dequeue_xmit_addr_entry(gmp_addr_list_entry *addr_entry);
extern gmp_addr_list_entry *
    gmp_first_xmit_addr_entry(gmp_addr_list *addr_list);
extern void gmp_enqueue_xmit_addr_list(gmp_addr_list *addr_list);
extern void gmp_flush_xmit_list(gmp_addr_list *addr_list);
extern void gmp_delete_addr_list_entry(gmp_addr_list_entry *addr_entry);
extern boolean gmp_addr_list_empty(gmp_addr_list *list);
extern boolean gmp_xmit_addr_list_empty(gmp_addr_list *list);
extern int gmp_addr_vect_set(gmp_addr_vect *vector, gmp_addr_string *addr);
extern void gmp_addr_vect_clean(gmp_addr_vect *vector);
extern void gmp_init_addr_vector(gmp_addr_vect *vector,
                 gmp_addr_catalog *catalog);
extern void gmp_destroy_addr_catalog(gmp_addr_catalog *catalog);
extern int gmp_init_addr_catalog(gmp_addr_catalog *catalog,
                 uint32_t addr_len);
extern gmp_addr_cat_entry *
gmp_get_addr_cat_by_ordinal(gmp_addr_catalog *catalog, ordinal_t ordinal);
extern ordinal_t gmp_lookup_create_addr_cat_entry(gmp_addr_catalog *catalog,
                          uint8_t *addr);
extern gmp_addr_cat_entry *gmp_lookup_addr_cat_entry(gmp_addr_catalog *catalog,
                             const uint8_t *addr);
extern void gmp_lock_adcat_entry(gmp_addr_catalog *catalog, ordinal_t ordinal);
extern void gmp_unlock_adcat_entry(gmp_addr_catalog *catalog,
                   ordinal_t ordinal);
extern int gmp_addr_vect_inter(gmp_addr_vect *src1, gmp_addr_vect *src2,
                   gmp_addr_vect *dest, bv_callback callback,
                   void *context, bv_callback_option cb_opt);
extern int gmp_addr_vect_union(gmp_addr_vect *src1, gmp_addr_vect *src2,
                   gmp_addr_vect *dest, bv_callback callback,
                   void *context, bv_callback_option cb_opt);
extern int gmp_addr_vect_minus(gmp_addr_vect *src1, gmp_addr_vect *src2,
                   gmp_addr_vect *dest, bv_callback callback,
                   void *context, bv_callback_option cb_opt);
extern int gmp_addr_vect_compare (gmp_addr_vect *src1, gmp_addr_vect *src2,
                  bv_callback callback, void *context);
extern int gmp_addr_vect_walk(gmp_addr_vect *vect, bv_callback callback,
                  void *context);
extern int gmp_build_addr_list(gmp_addr_list *addr_list,
                   gmp_addr_vect *vector);
extern boolean gmp_addr_list_copy_cb(void *context, bv_bitnum_t bitnum,
                     boolean new_val, boolean old_val);
extern gmp_addr_list_entry *gmp_alloc_generic_addr_list_entry(void *context);
extern gmp_addr_list_entry *
    gmp_create_addr_list_entry(gmp_addr_list *addr_list, ordinal_t ordinal);
extern void gmp_move_addr_list_entry(gmp_addr_list *to_list,
                     gmp_addr_list_entry *addr_entry);
extern boolean gmp_addr_vect_empty(gmp_addr_vect *vector);
extern boolean gmp_addr_is_zero(gmp_addr_string *addr, uint32_t addr_len);

/* gmpp_proto.c */

extern void gmpp_start_xmit(gmp_role role, gmp_proto proto,
                gmpx_intf_id intf_id);
extern void gmpp_register(gmp_role role, gmp_xmit_callback_func xmit_callback,
              gmp_rcv_callback_func rcv_callback,
              gmp_group_done_callback_func group_done_callback,
              gmp_packet_free_callback_func packet_free_callback);
extern void gmpp_deregister(gmp_role role);
extern void gmpp_enab_disab_proto(gmp_role role, gmp_proto proto,
                  boolean enabled);
extern gmp_packet *gmpp_create_packet_header(gmp_version version,
                         gmp_message_type message_type,
                         gmp_proto proto);
extern void gmpp_destroy_packet(gmp_packet *packet);
extern gmp_report_group_record *
    gmpp_create_group_record(gmp_report_packet *report_packet, void *group_id,
                 const uint8_t *group_addr, uint32_t addr_len);
extern void gmpp_init(void);
extern gmp_packet *gmpp_next_xmit_packet(gmp_role role, gmp_proto proto,
                     gmpx_intf_id intf_id,
                     uint32_t buffer_len);
extern void gmpp_group_done(gmp_role role, gmp_proto proto, void *group_id);
extern void gmpp_packet_done(gmp_role role, gmp_proto proto,
                 gmp_packet *packet);
extern void gmpp_process_rcv_packet(gmp_packet *packet, gmpx_intf_id intf_id);
extern uint32_t gmpp_max_group_count(gmp_proto proto, gmp_version version,
                  gmp_message_type msg_type, uint32_t buffer_len);

#endif /* __GMP_PRIVATE_H__ */
