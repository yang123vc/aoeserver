/*                                            
 *  linux/drivers/block/aoeserver/aoepacket.c
 * 
 * Ata Over Ethernet storage target for Linux.
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
 * The functions in this file takes packets from the workqueue and start the
 * processing, it handles config-requests by itself and dispatches any ATA
 * commands to aoeblock.c. This file contains all functions related to 
 * packet transfer. Most of these function run in the process context of 
 * one of the worker threads. 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ata.h>
#include <linux/hdreg.h>
#include <linux/netdevice.h>

#include "aoe.h"

/* This is the entry-point for the workerqueue kaoed, this function takes
 * care of newly recieved aoe-packets and dispatches them either to the
 * ata-handler or to the config-handler. kaoed() runs in the process-context
 * of one of the kaoed-threads. As of now there are the only two commands 
 * that are specified in the ATA over Ethernet specification (ATA & CFG) */
void kaoed(struct work_struct *data)
{
	struct aoerequest  *work    = container_of(data, struct aoerequest, work);


	/* We are ready to handle more packets */
	(void)aoedecqueue(work->abd);

	/* In the skb we find our aoe request frame */
	work->aoereq = (struct aoe_hdr *)work->skb_req->mac_header;

	/* Allocate an skb reply buffer of suitable size */
	work->skb_rep = create_skb(work->ifp, work);

	if (!work->skb_rep) {
		printk("aoepacket(): Failed to create_skb\n");
		goto out;
	}

	switch (work->aoereq->cmd) {

	case AOE_CMD_ATA:
		handleata(work);
		break;

	case AOE_CMD_CFG:
		handleconfig(work);
		break;

	default:
		printk("aoepacket(): Unknown command: %d!\n",
		       work->aoereq->cmd);
		work->aoerep->ver_flags |= AOE_FLAG_ERR;
		work->aoerep->error |= AOE_ERR_BADCMD;
		aoexmit(work);
		break;
	}

	/* Done */
	return;

      out:
	aoereq_destroy(work);
	return;

}

/* This function processes and replies to aoe-config requests */
void handleconfig(struct aoerequest *work)
{
	struct aoe_cfghdr *reply;
	struct aoe_cfghdr *request;
	unsigned char *cfgdatareq;
	unsigned char *cfgdatarep;

	/* Verify against acl */
	if (aoeblock_acl(work->abd, work->aoereq->eth.h_source) != 0) {
		printk("handleconfig: request blocked by acl!\n");
		goto no_xmit;
	}

	/* Make space for our cfg-header */
	skb_put(work->skb_rep, sizeof(struct aoe_cfghdr));

	/* Point to aoehdr */
	request =
	    (struct aoe_cfghdr *)((unsigned char *)work->skb_req->mac_header +
				  sizeof(struct aoe_hdr));
	reply =
	    (struct aoe_cfghdr *)((unsigned char *)work->skb_rep->data +
				  sizeof(struct aoe_hdr));

	/* Point to cfg data */
	cfgdatareq = (unsigned char *)request + sizeof(struct aoe_cfghdr);
	cfgdatarep = (unsigned char *)reply + sizeof(struct aoe_cfghdr);

	/* The number of packets we can queue */
	reply->queuelen = cpu_to_be16(20);

	reply->firmware = cpu_to_be16(0x4000);
	reply->notused = 0;	/*reserved */

	/* Copy cmd in reply */
	reply->aoever_cmd = 0;
	reply->aoever_cmd = request->aoever_cmd & 0x0f;
	reply->aoever_cmd |= 0x10;	/* AOE Version one */

	reply->data_len = 0;

	switch (request->aoever_cmd & 0x0f) {
	case 0:		/* read */
		skb_put(work->skb_rep, work->abd->cfg_len);
		memcpy(cfgdatarep, work->abd->cfg_data, work->abd->cfg_len);
		reply->data_len = cpu_to_be16(work->abd->cfg_len);

		break;		/* ---------- */

	case 1:		/* respond to exact match */
		if (memcmp(work->abd->cfg_data, cfgdatareq, work->abd->cfg_len)
		    == 0) {
			memcpy(cfgdatarep, work->abd->cfg_data,
			       work->abd->cfg_len);
			reply->data_len = cpu_to_be16(work->abd->cfg_len);
		} else
			goto no_xmit;

		break;		/* ---------- */

	case 2:		/* respond on partial match */
		if (memcmp(work->abd->cfg_data, cfgdatareq,
			   be16_to_cpu(request->data_len)) == 0) {
			skb_put(work->skb_rep, work->abd->cfg_len);
			memcpy(cfgdatarep, work->abd->cfg_data,
			       work->abd->cfg_len);
			reply->data_len = cpu_to_be16(work->abd->cfg_len);
		} else
			goto no_xmit;

		break;		/* ---------- */

	case 3:		/* Set config string if empty */

		if (work->abd->cfg_len == 0 &&
		    (be16_to_cpu(request->data_len) <= 1024)) {
			memcpy(work->abd->cfg_data,
			       cfgdatarep, be16_to_cpu(request->data_len));
			work->abd->cfg_len = be16_to_cpu(request->data_len);
		} else {
			work->aoerep->ver_flags |= AOE_FLAG_ERR;
			work->aoerep->error |= AOE_ERR_CFG_SET;
		}

		break;		/* ---------- */

	case 4:		/* Set config string */

		if (be16_to_cpu(request->data_len) <= 1024) {
			memcpy(work->abd->cfg_data,
			       cfgdatarep, be16_to_cpu(request->data_len));
			work->abd->cfg_len = be16_to_cpu(request->data_len);
		} else {
			work->aoerep->ver_flags |= AOE_FLAG_ERR;
			work->aoerep->error |= AOE_ERR_BADARG;
		}

		break;

	default:
		work->aoerep->ver_flags |= AOE_FLAG_ERR;
		work->aoerep->error |= AOE_ERR_BADCMD;
		printk(KERN_ERR "handleconfig(): Unknown command: %d\n",
		       request->aoever_cmd);
		break;
	}

	aoexmit(work);
	return;

      no_xmit:
	aoereq_destroy(work);
	return;

}

/* This function takes care of incomming ata-requets. It does some basic
 * sanity checking and parses the ata-header to figure out if its a 
 * read/write or a device identify-request before it dispatches the 
 * request either to blkdev_tranfser for block io or to bldev_identify
 * for identification requests. handleata is executed in the process 
 * context of kaoed. */
void handleata(struct aoerequest *work)
{

	/* Make space for our ata-header */
	skb_put(work->skb_rep, sizeof(struct aoe_atahdr));

	/* Point to ata-header in request and reply */
	work->atarequest = (struct aoe_atahdr *)
	    ((unsigned char *)work->skb_req->mac_header + sizeof(struct aoe_hdr));

	work->atareply = (struct aoe_atahdr *)
	    ((unsigned char *)work->skb_rep->mac_header + sizeof(struct aoe_hdr));

	/* Copy the ata-header from the request to the reply */
	memcpy(work->atareply, work->atarequest, sizeof(*work->atareply));

	/* So far, everything is ok */
	work->atareply->err_feature = 0;
	work->atareply->cmdstat = READY_STAT;

	/* Santity check the sector count */
	if (work->atarequest->nsect > 2) {
		printk(KERN_ERR
		       "aoe: atatransfer(): error sector count %d in req!n",
		       work->atarequest->nsect);
		goto error_xmit;
	}

	switch (work->atarequest->cmdstat) {
	case WIN_READ:
	case WIN_READ_EXT:
	case WIN_WRITE:
	case WIN_WRITE_EXT:

		if (work->abd == NULL) {
			printk(KERN_ERR "read/write to unknown device!!\n");
			goto error_xmit;
		}

		bldev_transfer(work);
		goto no_xmit;
		break;

	case WIN_IDENTIFY:

		bldev_identify(work);
		goto no_xmit;
		break;

	default:

		/* We didnt understand the command :( */
		goto error_xmit;
		break;

	}

      error_xmit:
	work->atareply->err_feature = ABRT_ERR;
	work->atareply->cmdstat = ERR_STAT | READY_STAT;
	aoexmit(work);

      no_xmit:
	return;
}

/* This function creates an AOE-response packet using the information in the 
 * requests, it copies the tag and moves the source-address from the request 
 * into the destination field of the reply; as well as some other stuff.
 * create_skb() is executed in the process-context of kaoed.
 * On success it returns a pointer to the newly created skb-buffer and 
 * points work->skb_rep to the same buffer */
struct sk_buff *create_skb(struct net_device *outdev, struct aoerequest *work)
{
	/* We allocate ETH_FRAME_LEN(1514)-bytes since 
	   this is the common case */
	work->skb_rep = alloc_skb(ETH_FRAME_LEN, GFP_DMA);
	if (!work->skb_rep) {
		printk("create_skb(): Unable to allocate skb!\n");
		return (NULL);
	}

	work->skb_rep->network_header = work->skb_rep->mac_header = work->skb_rep->data;
	work->skb_rep->protocol = __constant_htons(PRIV_ETH_P_AOE);
	work->skb_rep->priority = 0;
	work->skb_rep->next = work->skb_rep->prev = NULL;
	work->skb_rep->ip_summed = CHECKSUM_NONE;

	/* Make space for our aoe-header */
	skb_put(work->skb_rep, sizeof(struct aoe_hdr));

	/* Point into the skb-structs for request and reply */
	work->aoerep = (struct aoe_hdr *)work->skb_rep->data;
	work->aoereq = (struct aoe_hdr *)work->skb_req->mac_header;

	/* We copy the entire header from the request into the reply */
	memcpy((void *)work->aoerep, (void *)work->aoereq,
	       sizeof(struct aoe_hdr));

	/* We send the reply to the source of the request */
	memcpy(work->aoerep->eth.h_dest, work->aoereq->eth.h_source, ETH_ALEN);

	/* Set src-addr to our interface address */
	memcpy(work->aoerep->eth.h_source, outdev->dev_addr, ETH_ALEN);

	/* Reply is going out on the same interface 
	   that the request came in on */
	work->skb_rep->dev = outdev;

	work->aoerep->ver_flags |= AOE_FLAG_RSP;	/* response flag */

	/* Set the appropriate major and minor number */
	work->aoerep->shelf = htons(work->abd->shelf);
	work->aoerep->slot = work->abd->slot;

	return (work->skb_rep);
}

/* This function takes a work-struct as an argument and sends out the reply 
 * Since it uses dev_queue_xmit it can not be used from interrupt context */
void aoexmit(struct aoerequest *work)
{
	dev_queue_xmit(work->skb_rep);
	work->skb_rep = NULL;
	aoereq_destroy(work);

}
