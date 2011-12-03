/* Copyright 2007-2010 Jozsef Kadlecsik (kadlec@blackhole.kfki.hu)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <libipset/data.h>			/* IPSET_OPT_* */
#include <libipset/parse.h>			/* parser functions */
#include <libipset/print.h>			/* printing functions */
#include <libipset/types.h>			/* prototypes */

/* Parse commandline arguments */
static const struct ipset_arg bitmap_ip_create_args[] = {
	{ .name = { "range", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_netrange,	.print = ipset_print_ip,
	},
	{ .name = { "netmask", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_NETMASK,
	  .parse = ipset_parse_netmask,		.print = ipset_print_number,
	},
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	/* Backward compatibility */
	{ .name = { "from", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_single_ip,
	},
	{ .name = { "to", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP_TO,
	  .parse = ipset_parse_single_ip,
	},
	{ .name = { "network", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_net,
	},
	{ },
};

static const struct ipset_arg bitmap_ip_add_args[] = {
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
};

static const char bitmap_ip_usage[] =
"create SETNAME bitmap:ip range IP/CIDR|FROM-TO\n"
"               [netmask CIDR] [timeout VALUE]\n"
"add    SETNAME IP|IP/CIDR|FROM-TO [timeout VALUE]\n"
"del    SETNAME IP|IP/CIDR|FROM-TO\n"
"test   SETNAME IP\n\n"
"where IP, FROM and TO are IPv4 addresses (or hostnames),\n"
"      CIDR is a valid IPv4 CIDR prefix.\n";

struct ipset_type ipset_bitmap_ip0 = {
	.name = "bitmap:ip",
	.alias = { "ipmap", NULL },
	.revision = 0,
	.family = NFPROTO_IPV4,
	.dimension = IPSET_DIM_ONE,
	.elem = {
		[IPSET_DIM_ONE - 1] = {
			.parse = ipset_parse_ip,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP
		},
	},
	.args = {
		[IPSET_CREATE] = bitmap_ip_create_args,
		[IPSET_ADD] = bitmap_ip_add_args,
	},
	.mandatory = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_IP_TO),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP),
	},
	.full = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_NETMASK)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_IP_TO),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP),
	},

	.usage = bitmap_ip_usage,
};
