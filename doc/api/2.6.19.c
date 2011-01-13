match:

	/* true/false */
	int
	(*match)(
		const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const struct xt_match *match,
		const void *matchinfo,
		int offset,
		unsigned int protoff,
		int *hotdrop,
	);

	/* error code */
	int
	(*checkentry)(
		const char *tablename,
		const void *ip,
		const struct xt_match *match,
		void *matchinfo,
		unsigned int hook_mask,
	);

	void
	(*destroy)(
		const struct xt_match *match,
		void *matchinfo,
	);

target:

	/* verdict */
	unsigned int
	(*target)(
		struct sk_buff **pskb,
		const struct net_device *in,
		const struct net_device *out,
		unsigned int hooknum,
		const struct xt_target *target,
		const void *targinfo,
	);

	/* error code */
	int
	(*checkentry)(
		const char *tablename,
		const void *entry,
		const struct xt_target *target,
		void *targinfo,
		unsigned int hook_mask,
	);

	void
	(*destroy)(
		const struct xt_target *target,
		void *targinfo,
	);
