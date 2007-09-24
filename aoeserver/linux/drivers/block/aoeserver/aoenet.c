/*                                            
 *  linux/drivers/block/aoeserver/aoenet.c
 * 
 * Implementation of an in kernel Ata Over Ethernet storage target for Linux.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *  Copyright (C) 2005  wowie@hack.se
 *
 * The functions in this file takes care of network related stuff, such as 
 * registering with the networking subsystem and recieving packets and 
 * queueing them for processing. 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <asm/atomic.h>

#include "aoe.h"

/* Both these are from aoeblock.c */
extern struct aoeblkdev *abd_head;
extern rwlock_t abd_lock;

/* Counter for how many devices we have exported */
atomic_t active_devices = ATOMIC_INIT(0);

static struct sk_buff *skb_check(struct sk_buff *skb)
{
	if (skb_is_nonlinear(skb))
		if ((skb = skb_share_check(skb, GFP_ATOMIC)))
			if (skb_linearize(skb) < 0) {
				dev_kfree_skb(skb);
				return NULL;
			}
	return skb;
}

/* This function is called when a new packet is recieved, it runs 
 * in softirq-context and can do just about anything but sleep */
static int
aoenet_rcv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt)
{
	struct aoe_hdr *h;
	struct aoeblkdev *abd = NULL;
	unsigned short shelf;
	unsigned short slot;

	skb = skb_check(skb);
	if (!skb)
		goto out;

	h = (struct aoe_hdr *)skb->mac_header;

	if ((h->ver_flags & AOE_FLAG_RSP) == 1)
		goto out_kfree_skb; /* This was a responce packet */

	/* Retrieve major and minor */
	shelf = ntohs(h->shelf);
	slot = h->slot;

	/* Verify that this packet was for us */
	if ((abd = find_aoedevice(shelf, slot, ifp->ifindex))) {
		/* Make sure we recieved the request on a valid interface */
		if (abd->ifindex != 0 && abd->ifindex != ifp->ifindex)
			goto out_kfree_skb;	/* Invalid interface */

		/* If so, put it in the queue for processing */
		aoewq_addreq(skb, ifp, abd);
		goto out;
	} else {
		if ((shelf == 0xffff) && (slot == 0x00ff) && (abd_head != NULL)) {
			/* This was a broadcast, so we send the same packet to
			 * all queues and inc the ref-counter on the skb */
			read_lock(&abd_lock);
			list_for_each_entry(abd, &abd_head->list, list)
			    if (abd->ifindex == 0
				|| abd->ifindex == ifp->ifindex) {
				atomic_inc(&skb->users);
				aoewq_addreq(skb, ifp, abd);
			}
			read_unlock(&abd_lock);

			/* Subtle - Will now fall through to dev_kfree_skb() */

		} else
			printk(KERN_INFO
			       "aoe: aoenet_rcv unknown device %d %d\n", shelf,
			       slot);
	}

	/* Failure */
      out_kfree_skb:
	dev_kfree_skb(skb);

      out:
	return (0);
}

static struct packet_type aoe_pt = {
	.type = __constant_htons(PRIV_ETH_P_AOE),
	.func = aoenet_rcv,
};

int aoenet_init(void)
{
	/* WTF, this is unsafe .. but there is no atomic_test_and_inc() :( 
	   what to do? what to do... ? */

	if (atomic_read(&active_devices) == 0) {
		atomic_inc(&active_devices);
		dev_add_pack(&aoe_pt);
	}

	return 0;
}

void aoenet_exit(void)
{
	if (atomic_dec_and_test(&active_devices))
		dev_remove_pack(&aoe_pt);
}
