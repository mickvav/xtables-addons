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
static const struct ipset_arg hash_ipportnet_create_args[] = {
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
	/* Backward compatibility */
	{ .name = { "probes", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_PROBES,
	  .parse = ipset_parse_ignored,		.print = ipset_print_number,
	},
	{ .name = { "resize", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_RESIZE,
	  .parse = ipset_parse_ignored,		.print = ipset_print_number,
	},
	{ .name = { "from", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_ignored,
	},
	{ .name = { "to", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP_TO,
	  .parse = ipset_parse_ignored,
	},
	{ .name = { "network", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_IP,
	  .parse = ipset_parse_ignored,
	},
	{ },
};

static const struct ipset_arg hash_ipportnet_add_args[] = {
	{ .name = { "timeout", NULL },
	  .has_arg = IPSET_MANDATORY_ARG,	.opt = IPSET_OPT_TIMEOUT,
	  .parse = ipset_parse_uint32,		.print = ipset_print_number,
	},
	{ },
};

static const char hash_ipportnet1_usage[] =
"create SETNAME hash:ip,port,net\n"
"		[family inet|inet6]\n"
"               [hashsize VALUE] [maxelem VALUE]\n"
"               [timeout VALUE]\n"
"add    SETNAME IP,PROTO:PORT,IP[/CIDR] [timeout VALUE]\n"
"del    SETNAME IP,PROTO:PORT,IP[/CIDR]\n"
"test   SETNAME IP,PROTO:PORT,IP[/CIDR]\n\n"
"where depending on the INET family\n"
"      IP are valid IPv4 or IPv6 addresses (or hostnames),\n"
"      CIDR is a valid IPv4 or IPv6 CIDR prefix.\n"
"      Adding/deleting multiple elements in IP/CIDR or FROM-TO form\n"
"      in the first IP component is supported for IPv4.\n"
"      Adding/deleting multiple elements with TCP/SCTP/UDP/UDPLITE\n"
"      port range is supported both for IPv4 and IPv6.\n";

struct ipset_type ipset_hash_ipportnet1 = {
	.name = "hash:ip,port,net",
	.alias = { "ipportnethash", NULL },
	.revision = 1,
	.family = NFPROTO_IPSET_IPV46,
	.dimension = IPSET_DIM_THREE,
	.elem = {
		[IPSET_DIM_ONE - 1] = {
			.parse = ipset_parse_ip4_single6,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP
		},
		[IPSET_DIM_TWO - 1] = {
			.parse = ipset_parse_proto_port,
			.print = ipset_print_proto_port,
			.opt = IPSET_OPT_PORT
		},
		[IPSET_DIM_THREE - 1] = {
			.parse = ipset_parse_ipnet,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP2
		},
	},
	.args = {
		[IPSET_CREATE] = hash_ipportnet_create_args,
		[IPSET_ADD] = hash_ipportnet_add_args,
	},
	.mandatory = {
		[IPSET_CREATE] = 0,
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
	},
	.full = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_HASHSIZE)
			| IPSET_FLAG(IPSET_OPT_MAXELEM)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_CIDR)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_CIDR)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2),
	},

	.usage = hash_ipportnet1_usage,
	.usagefn = ipset_port_usage,
};

static const char hash_ipportnet2_usage[] =
"create SETNAME hash:ip,port,net\n"
"		[family inet|inet6]\n"
"               [hashsize VALUE] [maxelem VALUE]\n"
"               [timeout VALUE]\n"
"add    SETNAME IP,PROTO:PORT,IP[/CIDR] [timeout VALUE]\n"
"del    SETNAME IP,PROTO:PORT,IP[/CIDR]\n"
"test   SETNAME IP,PROTO:PORT,IP[/CIDR]\n\n"
"where depending on the INET family\n"
"      IP are valid IPv4 or IPv6 addresses (or hostnames),\n"
"      CIDR is a valid IPv4 or IPv6 CIDR prefix.\n"
"      Adding/deleting multiple elements in IP/CIDR or FROM-TO form\n"
"      in both IP components are supported for IPv4.\n"
"      Adding/deleting multiple elements with TCP/SCTP/UDP/UDPLITE\n"
"      port range is supported both for IPv4 and IPv6.\n";

struct ipset_type ipset_hash_ipportnet2 = {
	.name = "hash:ip,port,net",
	.alias = { "ipportnethash", NULL },
	.revision = 2,
	.family = NFPROTO_IPSET_IPV46,
	.dimension = IPSET_DIM_THREE,
	.elem = {
		[IPSET_DIM_ONE - 1] = {
			.parse = ipset_parse_ip4_single6,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP
		},
		[IPSET_DIM_TWO - 1] = {
			.parse = ipset_parse_proto_port,
			.print = ipset_print_proto_port,
			.opt = IPSET_OPT_PORT
		},
		[IPSET_DIM_THREE - 1] = {
			.parse = ipset_parse_ip4_net6,
			.print = ipset_print_ip,
			.opt = IPSET_OPT_IP2
		},
	},
	.args = {
		[IPSET_CREATE] = hash_ipportnet_create_args,
		[IPSET_ADD] = hash_ipportnet_add_args,
	},
	.mandatory = {
		[IPSET_CREATE] = 0,
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2),
	},
	.full = {
		[IPSET_CREATE] = IPSET_FLAG(IPSET_OPT_HASHSIZE)
			| IPSET_FLAG(IPSET_OPT_MAXELEM)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_ADD] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_CIDR)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2)
			| IPSET_FLAG(IPSET_OPT_IP2_TO)
			| IPSET_FLAG(IPSET_OPT_TIMEOUT),
		[IPSET_DEL] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_CIDR)
			| IPSET_FLAG(IPSET_OPT_IP_TO)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PORT_TO)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2)
			| IPSET_FLAG(IPSET_OPT_IP2_TO),
		[IPSET_TEST] = IPSET_FLAG(IPSET_OPT_IP)
			| IPSET_FLAG(IPSET_OPT_PORT)
			| IPSET_FLAG(IPSET_OPT_PROTO)
			| IPSET_FLAG(IPSET_OPT_IP2)
			| IPSET_FLAG(IPSET_OPT_CIDR2),
	},

	.usage = hash_ipportnet2_usage,
	.usagefn = ipset_port_usage,
};

