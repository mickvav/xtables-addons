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
static const struct ipset_arg bitmap_ipmac_create_args[] = {
	{ .name = { "range", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_netrange,	.print = ipset_print_ip,
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

static const struct ipset_arg bitmap_ipmac_add_args[] = {
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
};

static const char bitmap_ipmac_usage[] =
"create SETNAME bitmap:ip,mac range IP/CIDR|FROM-TO\n"
"               [matchunset] [timeout VALUE]\n"
"add    SETNAME IP[,MAC] [timeout VALUE]\n"
"del    SETNAME IP[,MAC]\n"
"test   SETNAME IP[,MAC]\n\n"
"where IP, FROM and TO are IPv4 addresses (or hostnames),\n"
"      CIDR is a valid IPv4 CIDR prefix,\n"
"      MAC is a valid MAC address.\n";

struct ipset_type ipset_bitmap_ipmac0 = {
	.name = "bitmap:ip,mac",
	.alias = { "macipmap", NULL },
	.revision = 0,
	.family = NFPROTO_IPV4,
	.dimension = IPSET_DIM_TWO,
	.last_elem_optional = true,
	.elem = {
		[IPSET_DIM_ONE - 1] = {
			.parse = ipset_parse_single_ip,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP
		},
		[IPSET_DIM_TWO - 1] = {
			.parse = ipset_parse_ether,
			.print = ipset_print_ether,
			.opt = IPSET_OPT_ETHER
		},
	},
	.args = {
		[IPSET_CREATE] = bitmap_ipmac_create_args,
		[IPSET_ADD] = bitmap_ipmac_add_args,
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
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_ETHER)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_ETHER),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_ETHER),
	},

	.usage = bitmap_ipmac_usage,
};
