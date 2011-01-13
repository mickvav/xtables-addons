/* Copyright 2007-2010 Jozsef Kadlecsik (kadlec@blackhole.kfki.hu)
 *
 * This program is free software; you can redistribute it and/or modify   
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation.
 */
#include <libipset/data.h>			/* IPSET_OPT_* */
#include <libipset/parse.h>			/* parser functions */
#include <libipset/print.h>			/* printing functions */
#include <libipset/ui.h>			/* ipset_port_usage */
#include <libipset/types.h>			/* prototypes */

/* Parse commandline arguments */
static const struct ipset_arg hash_netport_create_args[] = {
	{ .name = { "family", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_FAMILY,
	  .parse = ipset_parse_family,		.print = ipset_print_family,
	},
	/* Alias: family inet */
	{ .name = { "-4", NULL },
	  .has_arg = IPSET_NO_ARG,		.opt = IPSET_OPT_FAMILY,
	  .parse = ipset_parse_family,
	},
	/* Alias: family inet6 */
	{ .name = { "-6", NULL },
	  .has_arg = IPSET_NO_ARG,		.opt = IPSET_OPT_FAMILY,
	  .parse = ipset_parse_family,
	},
	{ .name = { "hashsize", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_HASHSIZE,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ .name = { "maxelem", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_MAXELEM,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
}; 

static const struct ipset_arg hash_netport_add_args[] = {
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
}; 

static const char hash_netport_usage[] =
"create SETNAME hash:net,port\n"
"		[family inet|inet6]\n"
"               [hashsize VALUE] [maxelem VALUE]\n"
"               [timeout VALUE]\n"
"add    SETNAME IP[/CIDR],PROTO:PORT [timeout VALUE]\n"
"del    SETNAME IP[/CIDR],PROTO:PORT\n"
"test   SETNAME IP[/CIDR],PROTO:PORT\n\n"
"where depending on the INET family\n"
"      IP is a valid IPv4 or IPv6 address (or hostname),\n"
"      CIDR is a valid IPv4 or IPv6 CIDR prefix.\n"
"      Adding/deleting multiple elements with TCP/UDP port range supported.\n";

struct ipset_type ipset_hash_netport0 = {
	.name = "hash:net,port",
	.alias = { "netporthash", NULL },
	.revision = 0,
	.family = AF_INET46,
	.dimension = IPSET_DIM_TWO,
	.elem = { 
		[IPSET_DIM_ONE] = { 
			.parse = ipset_parse_ipnet,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP
		},
		[IPSET_DIM_TWO] = { 
			.parse = ipset_parse_proto_port,
			.print = ipset_print_proto_port,
			.opt = IPSET_OPT_PORT
		},
	},
	.args = {
		[IPSET_CREATE] = hash_netport_create_args,
		[IPSET_ADD] = hash_netport_add_args,
	},
	.mandatory = {
		[IPSET_CREATE] = 0,
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_PORT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_PORT),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_PORT),
	},
	.full = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_HASHSIZE)
			| IPSET_FLAG(IPSET_OPT_MAXELEM)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT)
			| IPSET_FLAG(IPSET_OPT_CIDR),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_CIDR),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_CIDR),
	},

	.usage = hash_netport_usage,
	.usagefn = ipset_port_usage,
};
