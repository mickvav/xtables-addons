/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 * Copyright (C) 2003-2010 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module for IP set management */

#include "ip_set_kernel.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <net/netlink.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include "ip_set.h"
#include <net/genetlink.h>
#define GENLMSG_DEFAULT_SIZE (NLMSG_DEFAULT_SIZE - GENL_HDRLEN)

static struct list_head ip_set_type_list;	/* all registered set types */
static DEFINE_MUTEX(ip_set_type_mutex);		/* protects ip_set_type_list */

static struct ip_set **ip_set_list;		/* all individual sets */
static ip_set_id_t ip_set_max = 256; /* max number of sets */

#define STREQ(a, b)	(strncmp(a, b, IPSET_MAXNAMELEN) == 0)

static int max_sets;

module_param(max_sets, int, 0600);
MODULE_PARM_DESC(max_sets, "maximal number of sets");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("core IP set support");

/*
 * Prefixlen maps for fast conversions
 */

#define E(a, b, c, d) \
	{.ip6 = { \
		__constant_htonl(a), __constant_htonl(b), \
		__constant_htonl(c), __constant_htonl(d), \
	} }

/*
 * This table works for both IPv4 and IPv6;
 * just use prefixlen_netmask_map[prefixlength].ip.
 */
const union nf_inet_addr prefixlen_netmask_map[] = {
	E(0x00000000, 0x00000000, 0x00000000, 0x00000000),
	E(0x80000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xC0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xE0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xF0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xF8000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFC000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFE000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFF000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFF800000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFC00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFE00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFF00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFF80000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFC0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFE0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFF0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFF8000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFC000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFE000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFF000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFF800, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFC00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFE00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFF00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFF80, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFC0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFE0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFF0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFF8, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFC, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFE, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0x80000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xC0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xE0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xF0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xF8000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFC000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFE000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFF800000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFC00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFE00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFF00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFF80000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFC0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFE0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFF0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFF8000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFC000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFE000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFF000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFF800, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFC00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFE00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFF00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFF80, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFC0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFE0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFF0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFF8, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFC, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFE, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0x80000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xC0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xE0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xF0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xF8000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFC000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFE000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFF800000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFC00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFE00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFF00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFF80000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFC0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFE0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF8000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFC000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFE000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF800, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFC00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFE00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF80, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFC0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFE0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF8, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFC, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x80000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xE0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xF0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xF8000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFC000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFE000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF800000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFE00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF80000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFC0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFE0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF8000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFC000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFE000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF800),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFC00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFE00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF80),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFC0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFE0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF8),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFC),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF),
};
EXPORT_SYMBOL_GPL(prefixlen_netmask_map);

#undef  E
#define E(a, b, c, d) \
	{.ip6 = { a, b, c, d } }

/*
 * This table works for both IPv4 and IPv6;
 * just use prefixlen_hostmask_map[prefixlength].ip.
 */
const union nf_inet_addr prefixlen_hostmask_map[] = {
	E(0x00000000, 0x00000000, 0x00000000, 0x00000000),
	E(0x80000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xC0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xE0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xF0000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xF8000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFC000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFE000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFF000000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFF800000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFC00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFE00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFF00000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFF80000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFC0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFE0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFF0000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFF8000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFC000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFE000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFF000, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFF800, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFC00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFE00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFF00, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFF80, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFC0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFE0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFF0, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFF8, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFC, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFE, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0x80000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xC0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xE0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xF0000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xF8000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFC000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFE000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFF800000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFC00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFE00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFF00000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFF80000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFC0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFE0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFF0000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFF8000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFC000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFE000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFF000, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFF800, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFC00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFE00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFF00, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFF80, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFC0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFE0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFF0, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFF8, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFC, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFE, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0x80000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xC0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xE0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xF0000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xF8000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFC000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFE000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFF800000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFC00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFE00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFF00000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFF80000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFC0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFE0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF8000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFC000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFE000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF000, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF800, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFC00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFE00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF00, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF80, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFC0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFE0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF0, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF8, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFC, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x80000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xE0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xF0000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xF8000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFC000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFE000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF800000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFE00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF00000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF80000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFC0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFE0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF0000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF8000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFC000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFE000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF000),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFF800),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFC00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFE00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF00),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFF80),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFC0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFE0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF0),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFF8),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFC),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE),
	E(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF),
};
EXPORT_SYMBOL_GPL(prefixlen_hostmask_map);

static struct genl_family ip_set_netlink_subsys;

/*
 * The set types are implemented in modules and registered set types
 * can be found in ip_set_type_list. Adding/deleting types is
 * serialized by ip_set_type_mutex.
 */

static inline void
ip_set_type_lock(void)
{
	mutex_lock(&ip_set_type_mutex);
}

static inline void
ip_set_type_unlock(void)
{
	mutex_unlock(&ip_set_type_mutex);
}

/* Register and deregister settype */

static inline struct ip_set_type *
find_set_type(const char *name, u8 family, u8 revision)
{
	struct ip_set_type *type;

	list_for_each_entry_rcu(type, &ip_set_type_list, list)
		if (STREQ(type->name, name)
		    && (type->family == family || type->family == AF_UNSPEC)
		    && type->revision == revision)
			return type;
	return NULL;
}

/* Find a set type so that rcu_read_lock() is called by the function.
 * If we succeeded, the RCU lock is NOT released and the caller
 * must release it later.
 */
static struct ip_set_type *
find_set_type_rcu(const char *name, u8 family, u8 revision)
{
	struct ip_set_type *type;

	rcu_read_lock();
	type = find_set_type(name, family, revision);
	if (type == NULL)
		rcu_read_unlock();

	return type;
}

/* Find a given set type by name and family.
 * If we succeeded, the supported minimal and maximum revisions are
 * filled out.
 */
static bool
find_set_type_minmax(const char *name, u8 family,
		     u8 *min, u8 *max)
{
	struct ip_set_type *type;
	bool ret = false;

	*min = *max = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(type, &ip_set_type_list, list)
		if (STREQ(type->name, name)
		    && (type->family == family || type->family == AF_UNSPEC)) {
			ret = true;
			if (type->revision < *min)
				*min = type->revision;
			else if (type->revision > *max)
				*max = type->revision;
		}
	rcu_read_unlock();

	return ret;
}

#define family_name(f)	((f) == AF_INET ? "inet" : \
			 (f) == AF_INET6 ? "inet6" : "any")

/* Register a set type structure. The type is identified by
 * the unique triple of name, family and revision.
 */
int
ip_set_type_register(struct ip_set_type *type)
{
	int ret = 0;

	if (type->protocol != IPSET_PROTOCOL) {
		pr_warning("ip_set type %s, family %s, revision %u uses "
			   "wrong protocol version %u (want %u)\n",
			   type->name, family_name(type->family),
			   type->revision, type->protocol, IPSET_PROTOCOL);
		return -EINVAL;
	}

	ip_set_type_lock();
	if (find_set_type(type->name, type->family, type->revision)) {
		/* Duplicate! */
		pr_warning("ip_set type %s, family %s, revision %u "
			   "already registered!\n", type->name,
			   family_name(type->family), type->revision);
		ret = -EINVAL;
		goto unlock;
	}
	list_add_rcu(&type->list, &ip_set_type_list);
	pr_debug("type %s, family %s, revision %u registered.",
		 type->name, family_name(type->family), type->revision);
unlock:
	ip_set_type_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_type_register);

/* Unregister a set type. There's a small race with ip_set_create */
void
ip_set_type_unregister(struct ip_set_type *type)
{
	ip_set_type_lock();
	if (!find_set_type(type->name, type->family, type->revision)) {
		pr_warning("ip_set type %s, family %s, revision %u "
			   "not registered\n", type->name,
			   family_name(type->family), type->revision);
		goto unlock;
	}
	list_del_rcu(&type->list);
	pr_debug("type %s, family %s, revision %u unregistered.",
		 type->name, family_name(type->family), type->revision);
unlock:
	ip_set_type_unlock();

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(ip_set_type_unregister);

/*
 * Creating/destroying/renaming/swapping affect the existence and
 * the properties of a set. All of these can be executed from userspace
 * only and serialized by the nfnl mutex indirectly from nfnetlink.
 *
 * Sets are identified by their index in ip_set_list and the index
 * is used by the external references (set/SET netfilter modules).
 *
 * The set behind an index may change by swapping only, from userspace.
 */

static inline void
__ip_set_get(ip_set_id_t index)
{
	atomic_inc(&ip_set_list[index]->ref);
}

static inline void
__ip_set_put(ip_set_id_t index)
{
	atomic_dec(&ip_set_list[index]->ref);
}

/*
 * Add, del and test set entries from kernel.
 *
 * The set behind the index must exist and must be referenced
 * so it can't be destroyed (or changed) under our foot.
 */

int
ip_set_test(ip_set_id_t index, const struct sk_buff *skb,
	    u8 family, u8 dim, u8 flags)
{
	struct ip_set *set = ip_set_list[index];
	int ret = 0;

	BUG_ON(set == NULL || atomic_read(&set->ref) == 0);
	pr_debug("set %s, index %u", set->name, index);

	if (dim < set->type->dimension
	    || !(family == set->family || set->family == AF_UNSPEC))
		return 0;

	read_lock_bh(&set->lock);
	ret = set->variant->kadt(set, skb, IPSET_TEST, family, dim, flags);
	read_unlock_bh(&set->lock);

	if (ret == -EAGAIN) {
		/* Type requests element to be completed */
		pr_debug("element must be competed, ADD is triggered");
		write_lock_bh(&set->lock);
		set->variant->kadt(set, skb, IPSET_ADD, family, dim, flags);
		write_unlock_bh(&set->lock);
		ret = 1;
	}

	/* Convert error codes to nomatch */
	return (ret < 0 ? 0 : ret);
}
EXPORT_SYMBOL_GPL(ip_set_test);

int
ip_set_add(ip_set_id_t index, const struct sk_buff *skb,
	   u8 family, u8 dim, u8 flags)
{
	struct ip_set *set = ip_set_list[index];
	int ret;

	BUG_ON(set == NULL || atomic_read(&set->ref) == 0);
	pr_debug("set %s, index %u", set->name, index);

	if (dim < set->type->dimension
	    || !(family == set->family || set->family == AF_UNSPEC))
		return 0;

	write_lock_bh(&set->lock);
	ret = set->variant->kadt(set, skb, IPSET_ADD, family, dim, flags);
	write_unlock_bh(&set->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_add);

int
ip_set_del(ip_set_id_t index, const struct sk_buff *skb,
	   u8 family, u8 dim, u8 flags)
{
	struct ip_set *set = ip_set_list[index];
	int ret = 0;

	BUG_ON(set == NULL || atomic_read(&set->ref) == 0);
	pr_debug("set %s, index %u", set->name, index);

	if (dim < set->type->dimension
	    || !(family == set->family || set->family == AF_UNSPEC))
		return 0;

	write_lock_bh(&set->lock);
	ret = set->variant->kadt(set, skb, IPSET_DEL, family, dim, flags);
	write_unlock_bh(&set->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(ip_set_del);

/*
 * Find set by name, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet.
 *
 * The nfnl mutex must already be activated.
 */
ip_set_id_t
ip_set_get_byname(const char *name, struct ip_set **set)
{
	ip_set_id_t i, index = IPSET_INVALID_ID;
	struct ip_set *s;

	for (i = 0; i < ip_set_max; i++) {
		s = ip_set_list[i];
		if (s != NULL && STREQ(s->name, name)) {
			__ip_set_get(i);
			index = i;
			*set = s;
		}
	}

	return index;
}
EXPORT_SYMBOL_GPL(ip_set_get_byname);

/*
 * If the given set pointer points to a valid set, decrement
 * reference count by 1. The caller shall not assume the index
 * to be valid, after calling this function.
 *
 * The nfnl mutex must already be activated.
 */
void
ip_set_put_byindex(ip_set_id_t index)
{
	if (ip_set_list[index] != NULL) {
		BUG_ON(atomic_read(&ip_set_list[index]->ref) == 0);
		__ip_set_put(index);
	}
}
EXPORT_SYMBOL_GPL(ip_set_put_byindex);

/*
 * Get the name of a set behind a set index.
 * We assume the set is referenced, so it does exist and
 * can't be destroyed. The set cannot be renamed due to
 * the referencing either.
 *
 * The nfnl mutex must already be activated.
 */
const char *
ip_set_name_byindex(ip_set_id_t index)
{
	const struct ip_set *set = ip_set_list[index];

	BUG_ON(set == NULL);
	BUG_ON(atomic_read(&set->ref) == 0);

	/* Referenced, so it's safe */
	return set->name;
}
EXPORT_SYMBOL_GPL(ip_set_name_byindex);

/*
 * Routines to call by external subsystems, which do not
 * call nfnl_lock for us.
 */

/*
 * Find set by name, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet.
 *
 * The nfnl mutex is used in the function.
 */
ip_set_id_t
ip_set_nfnl_get(const char *name)
{
	struct ip_set *s;
	ip_set_id_t index;

	nfnl_lock();
	index = ip_set_get_byname(name, &s);
	nfnl_unlock();

	return index;
}
EXPORT_SYMBOL_GPL(ip_set_nfnl_get);

/*
 * Find set by index, reference it once. The reference makes sure the
 * thing pointed to, does not go away under our feet.
 *
 * The nfnl mutex is used in the function.
 */
ip_set_id_t
ip_set_nfnl_get_byindex(ip_set_id_t index)
{
	if (index > ip_set_max)
		return IPSET_INVALID_ID;

	nfnl_lock();
	if (ip_set_list[index])
		__ip_set_get(index);
	else
		index = IPSET_INVALID_ID;
	nfnl_unlock();

	return index;
}
EXPORT_SYMBOL_GPL(ip_set_nfnl_get_byindex);

/*
 * If the given set pointer points to a valid set, decrement
 * reference count by 1. The caller shall not assume the index
 * to be valid, after calling this function.
 *
 * The nfnl mutex is used in the function.
 */
void
ip_set_nfnl_put(ip_set_id_t index)
{
	nfnl_lock();
	if (ip_set_list[index] != NULL) {
		BUG_ON(atomic_read(&ip_set_list[index]->ref) == 0);
		__ip_set_put(index);
	}
	nfnl_unlock();
}
EXPORT_SYMBOL_GPL(ip_set_nfnl_put);

/*
 * Communication protocol with userspace over netlink.
 *
 * We already locked by nfnl_lock.
 */

static inline bool
protocol_failed(struct nlattr *const *tb)
{
	return !tb[IPSET_ATTR_PROTOCOL]
	       || nla_get_u8(tb[IPSET_ATTR_PROTOCOL]) != IPSET_PROTOCOL;
}

static inline u32
flag_exist(const struct genlmsghdr *ghdr)
{
	return ghdr->reserved & NLM_F_EXCL ? 0 : IPSET_FLAG_EXIST;
}

static inline bool
flag_nested(const struct nlattr *nla)
{
	return nla->nla_type & NLA_F_NESTED;
}

static void *
start_msg(struct sk_buff *skb, u32 pid, u32 seq, unsigned int flags,
	  enum ipset_cmd cmd)
{
	void *nlh;

	nlh = genlmsg_put(skb, pid, seq, &ip_set_netlink_subsys, flags, cmd);
	if (nlh == NULL)
		return NULL;
	return nlh;
}

/* Create a set */

static const struct nla_policy ip_set_create_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_TYPENAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1},
	[IPSET_ATTR_REVISION]	= { .type = NLA_U8 },
	[IPSET_ATTR_FAMILY]	= { .type = NLA_U8 },
	[IPSET_ATTR_DATA]	= { .type = NLA_NESTED },
};

static ip_set_id_t
find_set_id(const char *name)
{
	ip_set_id_t i, index = IPSET_INVALID_ID;
	const struct ip_set *set;

	for (i = 0; index == IPSET_INVALID_ID && i < ip_set_max; i++) {
		set = ip_set_list[i];
		if (set != NULL && STREQ(set->name, name))
			index = i;
	}
	return index;
}

static inline struct ip_set *
find_set(const char *name)
{
	ip_set_id_t index = find_set_id(name);

	return index == IPSET_INVALID_ID ? NULL : ip_set_list[index];
}

static int
find_free_id(const char *name, ip_set_id_t *index, struct ip_set **set)
{
	ip_set_id_t i;

	*index = IPSET_INVALID_ID;
	for (i = 0;  i < ip_set_max; i++) {
		if (ip_set_list[i] == NULL) {
			if (*index == IPSET_INVALID_ID)
				*index = i;
		} else if (STREQ(name, ip_set_list[i]->name)) {
			/* Name clash */
			*set = ip_set_list[i];
			return -EEXIST;
		}
	}
	if (*index == IPSET_INVALID_ID)
		/* No free slot remained */
		return -IPSET_ERR_MAX_SETS;
	return 0;
}

static inline void
load_type_module(const char *typename)
{
	pr_debug("try to load ip_set_%s", typename);
	request_module("ip_set_%s", typename);
}

static int
ip_set_create(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;

	struct ip_set *set, *clash;
	ip_set_id_t index = IPSET_INVALID_ID;
	const char *name, *typename;
	u8 family, revision;
	u32 flags = flag_exist(info->genlhdr);
	int ret = 0, len;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || attr[IPSET_ATTR_TYPENAME] == NULL
		     || attr[IPSET_ATTR_REVISION] == NULL
		     || attr[IPSET_ATTR_FAMILY] == NULL
		     || (attr[IPSET_ATTR_DATA] != NULL
			 && !flag_nested(attr[IPSET_ATTR_DATA]))))
		return -IPSET_ERR_PROTOCOL;

	name = nla_data(attr[IPSET_ATTR_SETNAME]);
	typename = nla_data(attr[IPSET_ATTR_TYPENAME]);
	family = nla_get_u8(attr[IPSET_ATTR_FAMILY]);
	revision = nla_get_u8(attr[IPSET_ATTR_REVISION]);
	pr_debug("setname: %s, typename: %s, family: %s, revision: %u",
		 name, typename, family_name(family), revision);

	/*
	 * First, and without any locks, allocate and initialize
	 * a normal base set structure.
	 */
	set = kzalloc(sizeof(struct ip_set), GFP_KERNEL);
	if (!set)
		return -ENOMEM;
	rwlock_init(&set->lock);
	strncpy(set->name, name, IPSET_MAXNAMELEN);
	atomic_set(&set->ref, 0);
	set->family = family;

	/*
	 * Next, check that we know the type, and take
	 * a reference on the type, to make sure it stays available
	 * while constructing our new set.
	 *
	 * After referencing the type, we try to create the type
	 * specific part of the set without holding any locks.
	 */
	set->type = find_set_type_rcu(typename, family, revision);
	if (set->type == NULL) {
		/* Try loading the module */
		load_type_module(typename);
		set->type = find_set_type_rcu(typename, family, revision);
		if (set->type == NULL) {
			pr_warning("Can't find ip_set type %s, family %s, "
				   "revision %u: set '%s' not created",
				   typename, family_name(family), revision,
				   name);
			ret = -IPSET_ERR_FIND_TYPE;
			goto out;
		}
	}
	if (!try_module_get(set->type->me)) {
		rcu_read_unlock();
		ret = -EFAULT;
		goto out;
	}
	rcu_read_unlock();

	/*
	 * Without holding any locks, create private part.
	 */
	len = attr[IPSET_ATTR_DATA] ? nla_len(attr[IPSET_ATTR_DATA]) : 0;
	pr_debug("data len: %u", len);
	ret = set->type->create(set, attr[IPSET_ATTR_DATA] ?
				nla_data(attr[IPSET_ATTR_DATA]) : NULL, len,
				flags);
	if (ret != 0)
		goto put_out;

	/* BTW, ret==0 here. */

	/*
	 * Here, we have a valid, constructed set and we are protected
	 * by nfnl_lock. Find the first free index in ip_set_list and
	 * check clashing.
	 */
	if ((ret = find_free_id(set->name, &index, &clash)) != 0) {
		/* If this is the same set and requested, ignore error */
		if (ret == -EEXIST
		    && (flags & IPSET_FLAG_EXIST)
		    && STREQ(set->type->name, clash->type->name)
		    && set->type->family == clash->type->family
		    && set->type->revision == clash->type->revision
		    && set->variant->same_set(set, clash))
			ret = 0;
		goto cleanup;
	}

	/*
	 * Finally! Add our shiny new set to the list, and be done.
	 */
	pr_debug("create: '%s' created with index %u!", set->name, index);
	ip_set_list[index] = set;

	return ret;

cleanup:
	set->variant->destroy(set);
put_out:
	module_put(set->type->me);
out:
	kfree(set);
	return ret;
}

/* Destroy sets */

static const struct nla_policy
ip_set_setname_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
};

static inline void
ip_set_destroy_set(ip_set_id_t index)
{
	struct ip_set *set = ip_set_list[index];

	pr_debug("set: %s",  set->name);
	ip_set_list[index] = NULL;

	/* Must call it without holding any lock */
	set->variant->destroy(set);
	module_put(set->type->me);
	kfree(set);
}

static int
ip_set_destroy(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	ip_set_id_t i;

	if (unlikely(protocol_failed(attr)))
		return -IPSET_ERR_PROTOCOL;

	/* References are protected by the nfnl mutex */
	if (!attr[IPSET_ATTR_SETNAME]) {
		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] != NULL
			    && (atomic_read(&ip_set_list[i]->ref)))
				return -IPSET_ERR_BUSY;
		}
		for (i = 0; i < ip_set_max; i++) {
			if (ip_set_list[i] != NULL)
				ip_set_destroy_set(i);
		}
	} else {
		i = find_set_id(nla_data(attr[IPSET_ATTR_SETNAME]));
		if (i == IPSET_INVALID_ID)
			return -EEXIST;
		else if (atomic_read(&ip_set_list[i]->ref))
			return -IPSET_ERR_BUSY;

		ip_set_destroy_set(i);
	}
	return 0;
}

/* Flush sets */

static inline void
ip_set_flush_set(struct ip_set *set)
{
	pr_debug("set: %s",  set->name);

	write_lock_bh(&set->lock);
	set->variant->flush(set);
	write_unlock_bh(&set->lock);
}

static int
ip_set_flush(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	ip_set_id_t i;

	if (unlikely(protocol_failed(attr)))
		return -EPROTO;

	if (!attr[IPSET_ATTR_SETNAME]) {
		for (i = 0; i < ip_set_max; i++)
			if (ip_set_list[i] != NULL)
				ip_set_flush_set(ip_set_list[i]);
	} else {
		i = find_set_id(nla_data(attr[IPSET_ATTR_SETNAME]));
		if (i == IPSET_INVALID_ID)
			return -EEXIST;

		ip_set_flush_set(ip_set_list[i]);
	}

	return 0;
}

/* Rename a set */

static const struct nla_policy
ip_set_setname2_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_SETNAME2]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
};

static int
ip_set_rename(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	struct ip_set *set;
	const char *name2;
	ip_set_id_t i;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || attr[IPSET_ATTR_SETNAME2] == NULL))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (set == NULL)
		return -EEXIST;
	if (atomic_read(&set->ref) != 0)
		return -IPSET_ERR_REFERENCED;

	name2 = nla_data(attr[IPSET_ATTR_SETNAME2]);
	for (i = 0; i < ip_set_max; i++) {
		if (ip_set_list[i] != NULL
		    && STREQ(ip_set_list[i]->name, name2))
			return -IPSET_ERR_EXIST_SETNAME2;
	}
	strncpy(set->name, name2, IPSET_MAXNAMELEN);

	return 0;
}

/* Swap two sets so that name/index points to the other.
 * References and set names are also swapped.
 *
 * We are protected by the nfnl mutex and references are
 * manipulated only by holding the mutex. The kernel interfaces
 * do not hold the mutex but the pointer settings are atomic
 * so the ip_set_list always contains valid pointers to the sets.
 */

static int
ip_set_swap(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	struct ip_set *from, *to;
	ip_set_id_t from_id, to_id;
	char from_name[IPSET_MAXNAMELEN];
	u32 from_ref;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || attr[IPSET_ATTR_SETNAME2] == NULL))
		return -IPSET_ERR_PROTOCOL;

	from_id = find_set_id(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (from_id == IPSET_INVALID_ID)
		return -EEXIST;

	to_id = find_set_id(nla_data(attr[IPSET_ATTR_SETNAME2]));
	if (to_id == IPSET_INVALID_ID)
		return -IPSET_ERR_EXIST_SETNAME2;

	from = ip_set_list[from_id];
	to = ip_set_list[to_id];

	/* Features must not change.
	 * Not an artifical restriction anymore, as we must prevent
	 * possible loops created by swapping in setlist type of sets. */
	if (!(from->type->features == to->type->features
	      && from->type->family == to->type->family))
		return -IPSET_ERR_TYPE_MISMATCH;

	/* No magic here: ref munging protected by the nfnl_lock */
	strncpy(from_name, from->name, IPSET_MAXNAMELEN);
	from_ref = atomic_read(&from->ref);

	strncpy(from->name, to->name, IPSET_MAXNAMELEN);
	atomic_set(&from->ref, atomic_read(&to->ref));
	strncpy(to->name, from_name, IPSET_MAXNAMELEN);
	atomic_set(&to->ref, from_ref);

	ip_set_list[from_id] = to;
	ip_set_list[to_id] = from;

	/* Avoid possible race between ongoing slow add/del in kernel space
	 * and next destroy command. */
	synchronize_net();

	return 0;
}

/* List/save set data */

#define DUMP_INIT	0L
#define DUMP_ALL	1L
#define DUMP_ONE	2L
#define DUMP_LAST	3L

static int
ip_set_dump_done(struct netlink_callback *cb)
{
	if (cb->args[2]) {
		pr_debug("release set %s", ip_set_list[cb->args[1]]->name);
		__ip_set_put((ip_set_id_t) cb->args[1]);
	}
	return 0;
}

static inline void
dump_attrs(void *phdr)
{
	const struct nlattr *attr;
	const struct nlmsghdr *nlh = phdr - GENL_HDRLEN - NLMSG_HDRLEN;
	int rem;

	pr_debug("nlmsg_len: %u", nlh->nlmsg_len);
	pr_debug("dump nlmsg");
	nlmsg_for_each_attr(attr, nlh, sizeof(struct genlmsghdr), rem) {
		pr_debug("type: %u, len %u", nla_type(attr), attr->nla_len);
	}
}

static inline int
dump_init(struct netlink_callback *cb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(cb->skb);
	int min_len = NLMSG_SPACE(sizeof(struct genlmsghdr));
	struct nlattr *cda[IPSET_ATTR_CMD_MAX+1];
	struct nlattr *attr = (void *)nlh + min_len;
	ip_set_id_t index;

	/* Second pass, so parser can't fail */
	nla_parse(cda, IPSET_ATTR_CMD_MAX,
		  attr, nlh->nlmsg_len - min_len, ip_set_setname_policy);

	/* cb->args[0] : dump single set/all sets
	 *         [1] : set index
	 *         [..]: type specific
	 */

	if (!cda[IPSET_ATTR_SETNAME]) {
		cb->args[0] = DUMP_ALL;
		return 0;
	}

	index = find_set_id(nla_data(cda[IPSET_ATTR_SETNAME]));
	if (index == IPSET_INVALID_ID)
		return -EEXIST;

	cb->args[0] = DUMP_ONE;
	cb->args[1] = index;
	return 0;
}

static int
ip_set_dump_start(struct sk_buff *skb, struct netlink_callback *cb)
{
	ip_set_id_t index = IPSET_INVALID_ID, max;
	struct ip_set *set = NULL;
	void *nlh = NULL;
	unsigned int flags = NETLINK_CB(cb->skb).pid ? NLM_F_MULTI : 0;
	int ret = 0;

	if (cb->args[0] == DUMP_INIT) {
		ret = dump_init(cb);
		if (ret < 0) {
			/* We have to create and send the error message
			 * manually :-( */
			netlink_ack(cb->skb, nlmsg_hdr(cb->skb), ret);
			return ret;
		}
	}

	if (cb->args[1] >= ip_set_max)
		goto out;

	pr_debug("args[0]: %ld args[1]: %ld\n", cb->args[0], cb->args[1]);
	max = cb->args[0] == DUMP_ONE ? cb->args[1] + 1 : ip_set_max;
	for (; cb->args[1] < max; cb->args[1]++) {
		index = (ip_set_id_t) cb->args[1];
		set = ip_set_list[index];
		if (set == NULL) {
			if (cb->args[0] == DUMP_ONE) {
				ret = -EEXIST;
				goto out;
			}
			continue;
		}
		/* When dumping all sets, we must dump "sorted"
		 * so that lists (unions of sets) are dumped last.
		 */
		if (cb->args[0] != DUMP_ONE
		    && !((cb->args[0] == DUMP_ALL)
			 ^ (set->type->features & IPSET_DUMP_LAST)))
			continue;
		pr_debug("List set: %s", set->name);
		if (!cb->args[2]) {
			/* Start listing: make sure set won't be destroyed */
			pr_debug("reference set");
			__ip_set_get(index);
		}
		nlh = start_msg(skb, NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, flags,
				IPSET_CMD_LIST);
		if (!nlh) {
			ret = -EFAULT;
			goto release_refcount;
		}
		NLA_PUT_U8(skb, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL);
		NLA_PUT_STRING(skb, IPSET_ATTR_SETNAME, set->name);
		switch (cb->args[2]) {
		case 0:
			/* Core header data */
			NLA_PUT_STRING(skb, IPSET_ATTR_TYPENAME,
				       set->type->name);
			NLA_PUT_U8(skb, IPSET_ATTR_FAMILY,
				   set->family);
			NLA_PUT_U8(skb, IPSET_ATTR_REVISION,
				   set->type->revision);
			ret = set->variant->head(set, skb);
			if (ret < 0)
				goto release_refcount;
			/* Fall through and add elements */
		default:
			read_lock_bh(&set->lock);
			ret = set->variant->list(set, skb, cb);
			read_unlock_bh(&set->lock);
			if (!cb->args[2]) {
				/* Set is done, proceed with next one */
				if (cb->args[0] == DUMP_ONE)
					cb->args[1] = IPSET_INVALID_ID;
				else
					cb->args[1]++;
			}
			goto release_refcount;
		}
	}
	goto out;

nla_put_failure:
	ret = -EFAULT;
release_refcount:
	/* If there was an error or set is done, release set */
	if (ret || !cb->args[2]) {
		pr_debug("release set %s", ip_set_list[index]->name);
		__ip_set_put(index);
	}

	/* If we dump all sets, continue with dumping last ones */
	if (cb->args[0] == DUMP_ALL && cb->args[1] >= max && !cb->args[2])
		cb->args[0] = DUMP_LAST;

out:
	if (nlh) {
		genlmsg_end(skb, nlh);
		dump_attrs(nlh);
	}

	return ret < 0 ? ret : skb->len;
}

static int
ip_set_dump(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	struct nlmsghdr *nlh = info->nlhdr;
	struct sock *ctnl = genl_info_net(info)->genl_sock;
	int ret;

	if (unlikely(protocol_failed(attr)))
		return -IPSET_ERR_PROTOCOL;

	genl_unlock();
	ret = netlink_dump_start(ctnl, skb, nlh,
				  ip_set_dump_start,
				  ip_set_dump_done);
	genl_lock();
	return ret;
}

/* Add, del and test */

static const struct nla_policy ip_set_adt_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_SETNAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
	[IPSET_ATTR_DATA]	= { .type = NLA_NESTED },
	[IPSET_ATTR_ADT]	= { .type = NLA_NESTED },
};

static int
call_ad(struct sk_buff *skb, struct nlattr *const attr[],
	struct ip_set *set, const struct nlattr *nla,
	enum ipset_adt adt, u32 flags)
{
	struct nlattr *head = nla_data(nla);
	int ret, len = nla_len(nla), retried = 0;
	u32 lineno = 0;
	bool eexist = flags & IPSET_FLAG_EXIST;

	do {
		write_lock_bh(&set->lock);
		ret = set->variant->uadt(set, head, len, adt,
					 &lineno, flags);
		write_unlock_bh(&set->lock);
	} while (ret == -EAGAIN
		 && set->variant->resize
		 && (ret = set->variant->resize(set, retried++)) == 0);

	if (!ret || (ret == -IPSET_ERR_EXIST && eexist))
		return 0;
	if (lineno && attr[IPSET_ATTR_LINENO]) {
		/* Error in restore/batch mode: send back lineno */
		u32 *errline = nla_data(attr[IPSET_ATTR_LINENO]);

		*errline = lineno;
	}

	return ret;
}

static int
ip_set_uadd(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;

	struct ip_set *set;
	const struct nlattr *nla;
	u32 flags = flag_exist(info->genlhdr);
	int ret = 0;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || !((attr[IPSET_ATTR_DATA] != NULL)
			  ^ (attr[IPSET_ATTR_ADT] != NULL))
		     || (attr[IPSET_ATTR_DATA] != NULL
			 && !flag_nested(attr[IPSET_ATTR_DATA]))
		     || (attr[IPSET_ATTR_ADT] != NULL
			 && (!flag_nested(attr[IPSET_ATTR_ADT])
			     || attr[IPSET_ATTR_LINENO] == NULL))))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (set == NULL)
		return -EEXIST;

	if (attr[IPSET_ATTR_DATA]) {
		ret = call_ad(skb, attr,
			      set, attr[IPSET_ATTR_DATA], IPSET_ADD, flags);
	} else {
		int nla_rem;

		nla_for_each_nested(nla, attr[IPSET_ATTR_ADT], nla_rem) {
			if (nla_type(nla) != IPSET_ATTR_DATA
			    || !flag_nested(nla))
				return -IPSET_ERR_PROTOCOL;
			ret = call_ad(skb, attr,
				       set, nla, IPSET_ADD, flags);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

static int
ip_set_udel(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;

	struct ip_set *set;
	const struct nlattr *nla;
	u32 flags = flag_exist(info->genlhdr);
	int ret = 0;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || !((attr[IPSET_ATTR_DATA] != NULL)
			  ^ (attr[IPSET_ATTR_ADT] != NULL))
		     || (attr[IPSET_ATTR_DATA] != NULL
			 && !flag_nested(attr[IPSET_ATTR_DATA]))
		     || (attr[IPSET_ATTR_ADT] != NULL
			 && (!flag_nested(attr[IPSET_ATTR_ADT])
			    || attr[IPSET_ATTR_LINENO] == NULL))))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (set == NULL)
		return -EEXIST;

	if (attr[IPSET_ATTR_DATA]) {
		ret = call_ad(skb, attr,
			      set, attr[IPSET_ATTR_DATA], IPSET_DEL, flags);
	} else {
		int nla_rem;

		nla_for_each_nested(nla, attr[IPSET_ATTR_ADT], nla_rem) {
			if (nla_type(nla) != IPSET_ATTR_DATA
			    || !flag_nested(nla))
				return -IPSET_ERR_PROTOCOL;
			ret = call_ad(skb, attr,
				       set, nla, IPSET_DEL, flags);
			if (ret < 0)
				return ret;
		}
	}
	return ret;
}

static int
ip_set_utest(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;

	struct ip_set *set;
	int ret = 0;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL
		     || attr[IPSET_ATTR_DATA] == NULL
		     || !flag_nested(attr[IPSET_ATTR_DATA])))
		return -IPSET_ERR_PROTOCOL;

	set = find_set(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (set == NULL)
		return -EEXIST;

	read_lock_bh(&set->lock);
	ret = set->variant->uadt(set,
				 nla_data(attr[IPSET_ATTR_DATA]),
				 nla_len(attr[IPSET_ATTR_DATA]),
				 IPSET_TEST, NULL, 0);
	read_unlock_bh(&set->lock);
	/* Userspace can't trigger element to be re-added */
	if (ret == -EAGAIN)
		ret = 1;

	return ret < 0 ? ret : ret > 0 ? 0 : -IPSET_ERR_EXIST;
}

/* Get headed data of a set */

static int
ip_set_header(struct sk_buff *skb, struct genl_info *info)
{
	const struct ip_set *set;
	struct nlattr *const *attr = info->attrs;
	const struct nlmsghdr *nlh = info->nlhdr;
	struct sk_buff *skb2;
	struct nlmsghdr *nlh2;
	ip_set_id_t index;
	int ret = 0;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_SETNAME] == NULL))
		return -IPSET_ERR_PROTOCOL;

	index = find_set_id(nla_data(attr[IPSET_ATTR_SETNAME]));
	if (index == IPSET_INVALID_ID)
		return -EEXIST;
	set = ip_set_list[index];

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).pid, nlh->nlmsg_seq, 0,
			 IPSET_CMD_HEADER);
	if (!nlh2)
		goto nlmsg_failure;
	NLA_PUT_U8(skb2, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL);
	NLA_PUT_STRING(skb2, IPSET_ATTR_SETNAME, set->name);
	NLA_PUT_STRING(skb2, IPSET_ATTR_TYPENAME, set->type->name);
	NLA_PUT_U8(skb2, IPSET_ATTR_FAMILY, set->family);
	NLA_PUT_U8(skb2, IPSET_ATTR_REVISION, set->type->revision);
	genlmsg_end(skb2, nlh2);

	ret = genlmsg_unicast(genl_info_net(info), skb2, NETLINK_CB(skb).pid);
	if (ret < 0)
		return -EFAULT;

	return 0;

nla_put_failure:
	nlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EFAULT;
}

/* Get type data */

static const struct nla_policy ip_set_type_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
	[IPSET_ATTR_TYPENAME]	= { .type = NLA_NUL_STRING,
				    .len = IPSET_MAXNAMELEN - 1 },
	[IPSET_ATTR_FAMILY]	= { .type = NLA_U8 },
};

static int
ip_set_type(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	const struct nlmsghdr *nlh = info->nlhdr;

	struct sk_buff *skb2;
	void *nlh2;
	u8 family, min, max;
	const char *typename;
	int ret = 0;

	if (unlikely(protocol_failed(attr)
		     || attr[IPSET_ATTR_TYPENAME] == NULL
		     || attr[IPSET_ATTR_FAMILY] == NULL))
		return -IPSET_ERR_PROTOCOL;

	family = nla_get_u8(attr[IPSET_ATTR_FAMILY]);
	typename = nla_data(attr[IPSET_ATTR_TYPENAME]);
	if (!find_set_type_minmax(typename, family, &min, &max)) {
		/* Try to load in the type module */
		load_type_module(typename);
		if (!find_set_type_minmax(typename, family, &min, &max)) {
			pr_debug("can't find: %s, family: %u",
				 typename, family);
			return -EEXIST;
		}
	}

	skb2 = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).pid, nlh->nlmsg_seq, 0,
			 IPSET_CMD_TYPE);
	if (!nlh2)
		goto nlmsg_failure;
	NLA_PUT_U8(skb2, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL);
	NLA_PUT_STRING(skb2, IPSET_ATTR_TYPENAME, typename);
	NLA_PUT_U8(skb2, IPSET_ATTR_FAMILY, family);
	NLA_PUT_U8(skb2, IPSET_ATTR_REVISION, max);
	NLA_PUT_U8(skb2, IPSET_ATTR_REVISION_MIN, min);
	genlmsg_end(skb2, nlh2);

	ret = genlmsg_unicast(genl_info_net(info), skb2, NETLINK_CB(skb).pid);
	if (ret < 0)
		return -EFAULT;

	return 0;

nla_put_failure:
	genlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EFAULT;
}

/* Get protocol version */

static const struct nla_policy
ip_set_protocol_policy[IPSET_ATTR_CMD_MAX + 1] = {
	[IPSET_ATTR_PROTOCOL]	= { .type = NLA_U8 },
};

static int
ip_set_protocol(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *const *attr = info->attrs;
	const struct nlmsghdr *nlh = info->nlhdr;

	struct sk_buff *skb2;
	void *nlh2;
	int ret = 0;

	if (unlikely(attr[IPSET_ATTR_PROTOCOL] == NULL))
		return -IPSET_ERR_PROTOCOL;

	skb2 = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	nlh2 = start_msg(skb2, NETLINK_CB(skb).pid, nlh->nlmsg_seq, 0,
			 IPSET_CMD_PROTOCOL);
	if (!nlh2)
		goto nlmsg_failure;
	NLA_PUT_U8(skb2, IPSET_ATTR_PROTOCOL, IPSET_PROTOCOL);
	genlmsg_end(skb2, nlh2);

	ret = genlmsg_unicast(genl_info_net(info), skb2, NETLINK_CB(skb).pid);
	if (ret < 0)
		return -EFAULT;

	return 0;

nla_put_failure:
	genlmsg_cancel(skb2, nlh2);
nlmsg_failure:
	kfree_skb(skb2);
	return -EFAULT;
}

static struct genl_ops ip_set_netlink_subsys_cb[] __read_mostly = {
	{
		.cmd    = IPSET_CMD_CREATE,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_create,
		.policy = ip_set_create_policy,
	},
	{
		.cmd    = IPSET_CMD_DESTROY,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_destroy,
		.policy = ip_set_setname_policy,
	},
	{
		.cmd    = IPSET_CMD_FLUSH,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_flush,
		.policy = ip_set_setname_policy,
	},
	{
		.cmd    = IPSET_CMD_RENAME,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_rename,
		.policy = ip_set_setname2_policy,
	},
	{
		.cmd    = IPSET_CMD_SWAP,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_swap,
		.policy = ip_set_setname2_policy,
	},
	{
		.cmd    = IPSET_CMD_LIST,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_dump,
		.policy = ip_set_setname_policy,
	},
	{
		.cmd    = IPSET_CMD_SAVE,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_dump,
		.policy = ip_set_setname_policy,
	},
	{
		.cmd    = IPSET_CMD_ADD,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_uadd,
		.policy = ip_set_adt_policy,
	},
	{
		.cmd    = IPSET_CMD_DEL,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_udel,
		.policy = ip_set_adt_policy,
	},
	{
		.cmd    = IPSET_CMD_TEST,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_utest,
		.policy = ip_set_adt_policy,
	},
	{
		.cmd    = IPSET_CMD_HEADER,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_header,
		.policy = ip_set_setname_policy,
	},
	{
		.cmd    = IPSET_CMD_TYPE,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_type,
		.policy = ip_set_type_policy,
	},
	{
		.cmd    = IPSET_CMD_PROTOCOL,
		.flags  = GENL_ADMIN_PERM,
		.doit   = ip_set_protocol,
		.policy = ip_set_protocol_policy,
	},
};

static struct genl_family ip_set_netlink_subsys __read_mostly = {
	.name    = "ip_set",
	.id      = GENL_ID_GENERATE,
	.hdrsize = 0,
	.version = 5,
	.maxattr = IPSET_ATTR_CMD_MAX,
};

/* Interface to iptables/ip6tables */

static int
ip_set_sockfn_get(struct sock *sk, int optval, void *user, int *len)
{
	unsigned *op;
	void *data;
	int copylen = *len, ret = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	if (optval != SO_IP_SET)
		return -EBADF;
	if (*len < sizeof(unsigned))
		return -EINVAL;

	data = vmalloc(*len);
	if (!data)
		return -ENOMEM;
	if (copy_from_user(data, user, *len) != 0) {
		ret = -EFAULT;
		goto done;
	}
	op = (unsigned *) data;

	if (*op < IP_SET_OP_VERSION) {
		/* Check the version at the beginning of operations */
		struct ip_set_req_version *req_version = data;
		if (req_version->version != IPSET_PROTOCOL) {
			ret = -EPROTO;
			goto done;
		}
	}

	switch (*op) {
	case IP_SET_OP_VERSION: {
		struct ip_set_req_version *req_version = data;

		if (*len != sizeof(struct ip_set_req_version)) {
			ret = -EINVAL;
			goto done;
		}

		req_version->version = IPSET_PROTOCOL;
		ret = copy_to_user(user, req_version,
				   sizeof(struct ip_set_req_version));
		goto done;
	}
	case IP_SET_OP_GET_BYNAME: {
		struct ip_set_req_get_set *req_get = data;

		if (*len != sizeof(struct ip_set_req_get_set)) {
			ret = -EINVAL;
			goto done;
		}
		req_get->set.name[IPSET_MAXNAMELEN - 1] = '\0';
		nfnl_lock();
		req_get->set.index = find_set_id(req_get->set.name);
		nfnl_unlock();
		goto copy;
	}
	case IP_SET_OP_GET_BYINDEX: {
		struct ip_set_req_get_set *req_get = data;

		if (*len != sizeof(struct ip_set_req_get_set)
		    || req_get->set.index >= ip_set_max) {
			ret = -EINVAL;
			goto done;
		}
		nfnl_lock();
		strncpy(req_get->set.name,
			ip_set_list[req_get->set.index]
				? ip_set_list[req_get->set.index]->name : "",
			IPSET_MAXNAMELEN);
		nfnl_unlock();
		goto copy;
	}
	default:
		ret = -EBADMSG;
		goto done;
	}	/* end of switch(op) */

copy:
	ret = copy_to_user(user, data, copylen);

done:
	vfree(data);
	if (ret > 0)
		ret = 0;
	return ret;
}

static struct nf_sockopt_ops so_set __read_mostly = {
	.pf		= PF_INET,
	.get_optmin	= SO_IP_SET,
	.get_optmax	= SO_IP_SET + 1,
	.get		= &ip_set_sockfn_get,
	.owner		= THIS_MODULE,
};

static int __init
ip_set_init(void)
{
	int ret;

	if (max_sets)
		ip_set_max = max_sets;
	if (ip_set_max >= IPSET_INVALID_ID)
		ip_set_max = IPSET_INVALID_ID - 1;

	ip_set_list = kzalloc(sizeof(struct ip_set *) * ip_set_max,
			      GFP_KERNEL);
	if (!ip_set_list) {
		pr_err("ip_set: Unable to create ip_set_list");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ip_set_type_list);

	ret = genl_register_family_with_ops(&ip_set_netlink_subsys,
	      ip_set_netlink_subsys_cb, ARRAY_SIZE(ip_set_netlink_subsys_cb));
	if (ret != 0) {
		pr_err("ip_set: cannot register with genetlink.");
		kfree(ip_set_list);
		return ret;
	}
	ret = nf_register_sockopt(&so_set);
	if (ret != 0) {
		pr_err("SO_SET registry failed: %d", ret);
		genl_unregister_family(&ip_set_netlink_subsys);
		kfree(ip_set_list);
		return ret;
	}

	pr_notice("ip_set: protocol %u", IPSET_PROTOCOL);
	return 0;
}

static void __exit
ip_set_fini(void)
{
	/* There can't be any existing set */
	nf_unregister_sockopt(&so_set);
	genl_unregister_family(&ip_set_netlink_subsys);
	kfree(ip_set_list);
	pr_debug("these are the famous last words");
}

module_init(ip_set_init);
module_exit(ip_set_fini);
