/*
 * netfilter module to enforce network quotas
 *
 * Sam Johnston <samj@samj.net>
 */
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#include <linux/netfilter/x_tables.h>
#include "xt_quota2.h"
#include "compat_xtables.h"

struct quota_counter {
	u_int64_t quota;
	spinlock_t lock;
	struct list_head list;
	atomic_t ref;
	char name[XT_QUOTA_COUNTER_NAME_LENGTH];
	struct proc_dir_entry *procfs_entry;
};

static LIST_HEAD(counter_list);
static DEFINE_SPINLOCK(counter_list_lock);

static struct proc_dir_entry *proc_xt_quota;
static unsigned int quota_list_perms = S_IRUGO | S_IWUSR;
static unsigned int quota_list_uid   = 0;
static unsigned int quota_list_gid   = 0;
module_param_named(perms, quota_list_perms, uint, S_IRUGO | S_IWUSR);
module_param_named(uid, quota_list_uid, uint, S_IRUGO | S_IWUSR);
module_param_named(gid, quota_list_gid, uint, S_IRUGO | S_IWUSR);

static int quota_proc_read(char *page, char **start, off_t offset,
                           int count, int *eof, void *data)
{
	struct quota_counter *e = data;
	int ret;

	spin_lock_bh(&e->lock);
	ret = snprintf(page, PAGE_SIZE, "%llu\n", e->quota);
	spin_unlock_bh(&e->lock);
	return ret;
}

static int quota_proc_write(struct file *file, const char __user *input,
                            unsigned long size, void *data)
{
	struct quota_counter *e = data;
	char buf[sizeof("18446744073709551616")];

	if (size > sizeof(buf))
		size = sizeof(buf);
	if (copy_from_user(buf, input, size) != 0)
		return -EFAULT;
	buf[sizeof(buf)-1] = '\0';

	spin_lock_bh(&e->lock);
	e->quota = simple_strtoul(buf, NULL, 0);
	spin_unlock_bh(&e->lock);
	return size;
}

/**
 * q2_get_counter - get ref to counter or create new
 * @name:	name of counter
 */
static struct quota_counter *q2_get_counter(const struct xt_quota_mtinfo2 *q)
{
	struct proc_dir_entry *p;
	struct quota_counter *e;

	spin_lock_bh(&counter_list_lock);
	list_for_each_entry(e, &counter_list, list) {
		if (strcmp(e->name, q->name) == 0) {
			atomic_inc(&e->ref);
			spin_unlock_bh(&counter_list_lock);
			return e;
		}
	}

	e = kmalloc(sizeof(struct quota_counter), GFP_KERNEL);
	if (e == NULL)
		goto out;

	e->quota = q->quota;
	spin_lock_init(&e->lock);
	INIT_LIST_HEAD(&e->list);
	atomic_set(&e->ref, 1);
	strncpy(e->name, q->name, sizeof(e->name));

	p = e->procfs_entry = create_proc_entry(e->name, quota_list_perms,
	                      proc_xt_quota);
	if (p == NULL || IS_ERR(p))
		goto out;
		
	p->owner        = THIS_MODULE;
	p->data         = e;
	p->read_proc    = quota_proc_read;
	p->write_proc   = quota_proc_write;
	p->uid          = quota_list_uid;
	p->gid          = quota_list_gid;
	list_add_tail(&e->list, &counter_list);
	spin_unlock_bh(&counter_list_lock);
	return e;

 out:
	spin_unlock_bh(&counter_list_lock);
	kfree(e);
	return NULL;
}

static bool
quota_mt2_check(const char *tablename, const void *entry,
                const struct xt_match *match, void *matchinfo,
                unsigned int hook_mask)
{
	struct xt_quota_mtinfo2 *q = matchinfo;

	if (q->flags & ~XT_QUOTA_MASK)
		return false;

	q->name[sizeof(q->name)-1] = '\0';
	if (*q->name == '\0' || *q->name == '.' ||
	    strchr(q->name, '/') != NULL) {
		printk(KERN_ERR "xt_quota.2: illegal name\n");
		return false;
	}

	q->master = q2_get_counter(q);
	if (q->master == NULL) {
		printk(KERN_ERR "xt_quota.2: memory alloc failure\n");
		return false;
	}

	return true;
}

static void quota_mt2_destroy(const struct xt_match *match, void *matchinfo)
{
	struct xt_quota_mtinfo2 *q = matchinfo;
	struct quota_counter *e = q->master;

	spin_lock_bh(&counter_list_lock);
	if (!atomic_dec_and_test(&e->ref)) {
		spin_unlock_bh(&counter_list_lock);
		return;
	}

	list_del(&e->list);
	spin_unlock_bh(&counter_list_lock);
	remove_proc_entry(e->name, proc_xt_quota);
	kfree(e);
}

static bool
quota_mt2(const struct sk_buff *skb, const struct net_device *in,
          const struct net_device *out, const struct xt_match *match,
          const void *matchinfo, int offset, unsigned int protoff,
          bool *hotdrop)
{
	struct xt_quota_mtinfo2 *q = (void *)matchinfo;
	struct quota_counter *e = q->master;
	bool ret = q->flags & XT_QUOTA_INVERT;

	if (q->flags & XT_QUOTA_GROW) {
		spin_lock_bh(&e->lock);
		e->quota += skb->len;
		q->quota = e->quota;
		spin_unlock_bh(&e->lock);
		ret = true;
	} else {
		spin_lock_bh(&e->lock);
		if (e->quota >= skb->len) {
			e->quota -= skb->len;
			ret = !ret;
		} else {
			/* we do not allow even small packets from now on */
			e->quota = 0;
		}
		q->quota = e->quota;
		spin_unlock_bh(&e->lock);
	}

	return ret;
}

static struct xt_match quota_mt2_reg[] __read_mostly = {
	{
		.name       = "quota2",
		.revision   = 2,
		.family     = AF_INET,
		.checkentry = quota_mt2_check,
		.match      = quota_mt2,
		.destroy    = quota_mt2_destroy,  
		.matchsize  = sizeof(struct xt_quota_mtinfo2),
		.me         = THIS_MODULE,
	},
	{
		.name       = "quota2",
		.revision   = 2,
		.family     = AF_INET6,
		.checkentry = quota_mt2_check,
		.match      = quota_mt2,
		.destroy    = quota_mt2_destroy,  
		.matchsize  = sizeof(struct xt_quota_mtinfo2),
		.me         = THIS_MODULE,
	},
};

static int __init quota_mt2_init(void)
{
	int ret;

	proc_xt_quota = proc_mkdir("xt_quota", init_net__proc_net);
	if (proc_xt_quota == NULL)
		return -EACCES;

	ret = xt_register_matches(quota_mt2_reg, ARRAY_SIZE(quota_mt2_reg));
	if (ret < 0)
		remove_proc_entry("xt_quota", init_net__proc_net);
	return ret;
}

static void __exit quota_mt2_exit(void)
{
	xt_unregister_matches(quota_mt2_reg, ARRAY_SIZE(quota_mt2_reg));
	remove_proc_entry("xt_quota", init_net__proc_net);
}

module_init(quota_mt2_init);
module_exit(quota_mt2_exit);
MODULE_DESCRIPTION("Xtables: countdown quota match; up counter");
MODULE_AUTHOR("Sam Johnston <samj@samj.net>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_quota2");
MODULE_ALIAS("ip6t_quota2");
