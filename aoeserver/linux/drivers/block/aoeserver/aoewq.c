/*
 *  linux/drivers/block/aoeserver/aoewq.c
 * 
 *  Implementation of an in kernel Ata Over Ethernet storage target for Linux.
 */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *  Copyright (C) 2005  wowie@pi.nxs.se
 */

/*
 * The functions in this file handles the workqueues used by the aoeserver. 
 * They will start and stop the worker-thread and add jobs to the queue.
 * 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <asm/atomic.h>

#include "aoe.h"

/* Create the workqueue and name it 'kaoed[MAJOR:MINOR];' */
void aoewq_init(struct aoeblkdev *blkdev)
{
	char buff[32];

	if (blkdev) {
		sprintf(buff, "kaoed[%d:%d]", blkdev->shelf, blkdev->slot);

		printk(KERN_NOTICE,"Starting %s\n", buff);
		blkdev->kaoed_wq = create_workqueue(buff);

		if (blkdev->kaoed_wq == NULL)
			printk(KERN_ERR
			       "aoewq_init(): Failed to start workqueue\n");
	} else
		printk(KERN_ERR "aoewq_init(): blkdev == NULL\n");

}

/* Kill the workqueue process */
void aoewq_exit(struct aoeblkdev *blkdev)
{

	if (blkdev && blkdev->kaoed_wq) {

		printk("Stopping kaoed[%d:%d]\n", blkdev->shelf, blkdev->slot);

		/* Flush the workqueue before removing it */
		flush_workqueue(blkdev->kaoed_wq);

		/* Remove the workque */
		destroy_workqueue(blkdev->kaoed_wq);

		return;
	} else
		printk(KERN_ERR
		       "aoewq_exit(): exit called when kaoed_wq == NULL!\n");

}

/* Add work to the workque - called from aoenet.c */
/* This function is executed in softirq-context when a packet arrieves */
void aoewq_addreq(struct sk_buff *skb, struct net_device *ifp,
		  struct aoeblkdev *abd)
{
	struct aoerequest *workreq;

	if (abd && atomic_read(&abd->queuecounter) > 20) {
		printk(KERN_ERR "aoewq_addwork(): aoequeue to large %d\n",
		       atomic_read(&abd->queuecounter));

		dev_kfree_skb(skb);
		return;
	}

	workreq = kmalloc(sizeof(*workreq), GFP_ATOMIC);
	if (!workreq) {
		printk(KERN_ERR
		       "aoewq_addwork(): Failed to allocate workrequest!\n");
		dev_kfree_skb(skb);
		return;		/* -ENOMEM; */
	}

	/* Create the work-struct */
	/* aoepacket() will be called with the workreq struct 
	 * as an argument in the context of our kaoed-kernel-thread
	 * at an approriate time in the future. */

	INIT_WORK(&workreq->work, kaoed );

	workreq->skb_req = skb;
	workreq->ifp = ifp;
	workreq->skb_rep = NULL;
	workreq->abd = abd;

	if ((abd != NULL) &&
	    (abd->kaoed_wq != NULL) &&
	    queue_work(abd->kaoed_wq, &workreq->work)) {
		/* Increment queue-counter */
		atomic_inc(&abd->queuecounter);
	} else {
		printk("aoewq_addreq() failed to submit request to queue!\n");
		aoereq_destroy(workreq);
	}

	/* The workqreq-struct will be kfree():d later */
	return;
}

/* When the jobb is done this function is called to decrease the
 * queuecounter so that we can take on more work */
void aoedecqueue(struct aoeblkdev *abd)
{
	if (abd != NULL)
		atomic_dec(&abd->queuecounter);
}

/* Return the number of packets in the queue */
int aoecheckqueue(struct aoeblkdev *abd)
{
	if (abd != NULL)
		return (atomic_read(&abd->queuecounter));
	else
		return (0);
}

/* Free a work-request and the skb it points to */
void aoereq_destroy(struct aoerequest *workreq)
{
	if (workreq && workreq->skb_req != NULL)
		dev_kfree_skb(workreq->skb_req);

	if (workreq && workreq->skb_rep != NULL)
		dev_kfree_skb(workreq->skb_rep);

	kfree(workreq);
}
