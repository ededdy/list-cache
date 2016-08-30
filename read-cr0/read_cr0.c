/**
 * read_cr0.c	- Rudimentary module to dump the value of per-cpu cr0
 * 		registor to kernel log ring buffer.
 *
 * Author: Sougata Santra (sougata.santra@gmail.com)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>

#ifdef CONFIG_SMP
static DEFINE_SPINLOCK(display_lock);

static void __read_cr0(void *nop __attribute__((__unused__)))
{
	unsigned long flags;
	unsigned long cr0, tmp;
	bool nw, cd;

	/*
	 *  Not exported to modules.
	 *
	 *  if (idle_cpu(smp_processor_id()))
	 *  	return
	 */
	spin_lock_irqsave(&display_lock, flags);
	tmp = cr0 =  read_cr0();
	/*
	 * Bit 29 - Not-write through,  globally enables/disable write-through
	 * caching.
	 */
	nw = ((tmp >>= 29) & 0x1);
	/*
	 * Cache disable, globally enables/disable the memory cache.
	 */
	cd = ((tmp >>= 1) & 0x1);
	pr_info("Set CPU%d: (cr0 0x%lx, NW: %s, CD: %s)\n",
			smp_processor_id(),
			cr0, nw ?  "true" : "false",
			cd ?  "true" : "false");
	spin_unlock_irqrestore(&display_lock, flags);
}
#else
static inline int __read_cr0,(void *) { return 0; }
#endif

static int __init readcr0_init(void)
{
	smp_call_function(__read_cr0, NULL, 0);
	return 0;
}

static void __exit readcr0_exit(void)
{
}

MODULE_LICENSE("GPL");
module_init(readcr0_init);
module_exit(readcr0_exit);
