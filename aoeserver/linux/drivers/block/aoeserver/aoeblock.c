/*                                            
 *  linux/drivers/block/aoeserver/aoeblock.c
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
 * The functions in this file takes care of the exported blockdevices, 
 * keeps a track of shared devices, open and close, reading and writing 
 * to block-devices. Most of the function, expect for find_aoedevice, runs
 * in process context and may sleep. 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/skbuff.h>
#include <linux/hdreg.h>
#include <linux/list.h>
#include <asm/fcntl.h>

#include "aoe.h"

struct aoeblkdev *abd_head = NULL;
DEFINE_RWLOCK(abd_lock);

/* Find the appropriate device in the device list */
/* This function is called from aoenet.c in soft-irq-context and
 * must therefore never sleep */
struct aoeblkdev *find_aoedevice(int shelf, int slot, int ifindex)
{
	struct aoeblkdev *abd = NULL;

	if (abd_head == NULL)
		return (NULL);

	read_lock(&abd_lock);
	{
		if (abd_head == NULL)
			return (NULL);

		list_for_each_entry(abd, &abd_head->list, list)
		    if (abd->shelf == shelf && abd->slot == slot &&
			(abd->ifindex == ifindex || abd->ifindex == 0
			 || ifindex == 0)) {
			read_unlock(&abd_lock);
			return (abd);
		}
	}
	read_unlock(&abd_lock);
	return (NULL);
}

/* Add a device to the device list */
int aoeblock_register(char *device, int shelf, int slot, int ifindex)
{
	struct aoeblkdev *abd;
	struct file *fp = NULL;
	struct inode *inode = NULL;
	struct address_space *mapping = NULL;

	if (!device)
		return -1;

	/* Make sure nothing else is exported on this shelf,slot,ifindex */
	if (find_aoedevice(shelf, slot, ifindex) ||
	    find_aoedevice(shelf, slot, 0)) {
		printk(KERN_ERR
		       "WARNING: Selected configuration already in use\n");
		return (-1);
	}

	fp = filp_open(device, O_RDONLY, 00);
	if (IS_ERR(fp)) {
		printk(KERN_ERR
		       "WARNING: Failed to open device: %s\n", device);
		return (-1);
	}

	printk("Exporting: %s\n", device);

	abd = kmalloc(sizeof(*abd), GFP_KERNEL);
	if (abd == NULL) {
		printk("kmalloc failed!\n");
		filp_close(fp, NULL);
		return (-1);
	}

	/* fillout struct */
	abd->fp = fp;
	abd->shelf = shelf;
	abd->slot = slot;
	atomic_set(&abd->queuecounter, 0);
	abd->size = i_size_read(fp->f_mapping->host) >> 9;
	abd->acl_lock = RW_LOCK_UNLOCKED;
	abd->acl = NULL;

	strncpy(abd->name, device, 30);

	/* If we dont care about interface, so we set ifindex to zero to 
	 * indicate that we dont care on wich interface the request came in */
	abd->ifindex = ifindex;

	/* Start the queue */
	aoewq_init(abd);

	/* Setup initial cfg-data for device */
	memset(abd->cfg_data, 255, 1024);
	strncpy(abd->cfg_data, device, 1024);
	abd->cfg_len = strlen(abd->cfg_data);

	write_lock(&abd_lock);
	{
		/* If we havent initialised the list-head before */
		if (abd_head == NULL) {
			/* First entry */
			abd_head = kmalloc(sizeof(*abd_head), GFP_KERNEL);
			/* Make sure all pointers are null */
			abd_head->acl = NULL;
			abd_head->fp = NULL;
			abd_head->kaoed_wq = NULL;

			/* init the linked list */
			INIT_LIST_HEAD(&abd_head->list);
		}

		/* Add this new entry */
		list_add(&(abd->list), &(abd_head->list));
	}
	write_unlock(&abd_lock);

	/* We are ready to recieve network traffic */
	aoenet_init();

	return (0);
}

/* Remove a device from the device list */
int aoeblock_unregister(char *device, int shelf, int slot, int ifindex)
{
	struct aoeblkdev *abd;
	struct list_head *pos, *q;
	struct list_head *aclpos, *aclq;
	struct accesslist *acl;

	write_lock(&abd_lock);
	{
		if (abd_head != NULL)
			list_for_each_safe(pos, q, &abd_head->list) {
			abd = list_entry(pos, struct aoeblkdev, list);

			if ((abd->shelf == shelf) &&
			    (abd->slot == slot) && (abd->ifindex == ifindex)) {
			        /* We remove it from the list before we 
				 * stop the workqueue */
				list_del(pos);

				/* No more work can be added, we can safely 
				 * flush the queue and kill the worker thread*/
				aoewq_exit(abd);

				if (abd->fp && !IS_ERR(abd->fp))
					filp_close(abd->fp, NULL);

				/* Free ACL */
				write_lock(&abd->acl_lock);
				if (abd->acl != NULL) {
					list_for_each_safe(aclpos, aclq,
							   &abd->acl->list) {
						acl =
						    list_entry(aclpos,
							       struct
							       accesslist,
							       list);
						list_del(aclpos);
						kfree(acl);
					}
					kfree(abd->acl);
				}
				write_unlock(&abd->acl_lock);

				kfree(abd);

				/* Decrement counter for network */
				aoenet_exit();
			}
			}
	}
	write_unlock(&abd_lock);

	return (0);
}

/* Verify a source address against the access control list */
int aoeblock_acl(struct aoeblkdev *abd, unsigned char *h_source)
{
	struct accesslist *acl;
	int ret = -EPERM;

	if (abd->acl == NULL)
		return (0);

	read_lock(&abd->acl_lock);
	list_for_each_entry(acl, &abd->acl->list, list)
	    if (memcmp(acl->h_source, h_source, ETH_ALEN) == 0) {
		ret = 0;
		break;
	}
	read_unlock(&abd->acl_lock);

	return (ret);
}

/* Add an access list entry to a specific block device */
int aoeblock_mask(unsigned short shelf,
		  unsigned short slot, unsigned char *h_source)
{
	struct aoeblkdev *abd;
	struct accesslist *acl;

	abd = find_aoedevice(shelf, slot, 0);
	if (abd == NULL)
		return (-EINVAL);

	acl = kmalloc(sizeof(*acl), GFP_KERNEL);
	if (acl == NULL)
		return (-ENOMEM);

	/* Add requested address to acl */
	memcpy(acl->h_source, h_source, ETH_ALEN);

	write_lock(&abd->acl_lock);
	{
		/* If we havent initialised the list-head before */
		if (abd->acl == NULL) {
			abd->acl = kmalloc(sizeof(*abd->acl), GFP_KERNEL);
			if (abd->acl == NULL) {
				write_unlock(&abd->acl_lock);
				return (-ENOMEM);
			}
			INIT_LIST_HEAD(&abd->acl->list);
		}

		/* Add entry */
		list_add(&(acl->list), &(abd->acl->list));
	}

	write_unlock(&abd->acl_lock);

	return (0);
}

/* Remove an entry from the access list for the specified device */
int aoeblock_rmmask(unsigned short shelf,
		    unsigned short slot, unsigned char *h_source)
{
	struct aoeblkdev *abd;
	struct accesslist *acl;
	struct list_head *pos, *q;

	abd = find_aoedevice(shelf, slot, 0);
	if (abd == NULL)
		return (-EINVAL);

	write_lock(&abd->acl_lock);
	if (abd->acl != NULL) {
		list_for_each_safe(pos, q, &abd->acl->list) {
			acl = list_entry(pos, struct accesslist, list);

			/* if match, remove from list */
			if (memcmp(acl->h_source, h_source, ETH_ALEN) == 0)
				list_del(pos);

			kfree(acl);
		}

		/* Is this the last entry ? */
		if (abd->acl->list.next == &abd->acl->list) {
			/* the list head points to itself, 
			   there are no entries left */
			kfree(abd->acl);
			abd->acl = NULL;
		}
	}
	write_unlock(&abd->acl_lock);

	return (0);
}

/* The aoe target server is shuting down, we need to shutdown all devices,
 * kill off the worker threads and remove the devices from the device list */
void aoeblock_exit(void)
{
	struct aoeblkdev *abd;
	struct accesslist *acl;
	struct list_head *pos, *q;
	struct list_head *aclpos, *aclq;

	write_lock(&abd_lock);
	{
		if (abd_head != NULL)
			list_for_each_safe(pos, q, &abd_head->list) {
			abd = list_entry(pos, struct aoeblkdev, list);

			/* Remove from list */
			list_del(pos);

			/* Flush and kill thread */
			aoewq_exit(abd);

			/* Decrement network active counter */
			aoenet_exit();

			/* Close file descriptor */
			if (abd->fp && !IS_ERR(abd->fp))
				filp_close(abd->fp, NULL);

			/* Remove ACL */
			write_lock(&abd->acl_lock);
			if (abd->acl != NULL) {
				list_for_each_safe(aclpos, aclq,
						   &abd->acl->list) {
					acl =
					    list_entry(aclpos,
						      struct accesslist, list);
					list_del(aclpos);
					kfree(acl);
				}
				kfree(abd->acl);
			}
			write_unlock(&abd->acl_lock);

			/* Free the last traces of our block device */
			kfree(abd);
			}
	}
	write_unlock(&abd_lock);

	kfree(abd_head);
}

/* this function moves data to or from disk, it is called in the process
 * context of kaoed and can sleep in order to wait for disk-io */
int bldev_transfer(struct aoerequest *work)
{
	loff_t ppos = 0;
	int i;
	unsigned char *p;
	char *buff;

	/* Make space for our ata-header */
	skb_put(work->skb_rep, sizeof(struct aoe_atahdr));

	/* Convert LBA-address */
	{
		p = work->atarequest->lba;

		for (i = 0; i < 6; i++)
			ppos |= (long long)(*p++) << i * 8;

		if ((work->atarequest->flags & AOE_ATAFLAG_LBA48) == 1)
			ppos = ppos & 0x0000ffffffffffffLL;	// full 48
		else
			ppos = ppos & 0x0fffffff;
	}

	/* Convert to byte offset */
	ppos *= 512;

	switch (work->atarequest->cmdstat) {
	case WIN_READ:
	case WIN_READ_EXT:

		/* Make space for data in reply packet */
		skb_put(work->skb_rep, (work->atarequest->nsect * 512));

		/* reply skb */
		buff = (char *)work->atareply + sizeof(struct aoe_atahdr);

		generic_file_read(work->abd->fp, buff,
				  (work->atarequest->nsect * 512), &ppos);

		break;

	case WIN_WRITE:
	case WIN_WRITE_EXT:

		/* request skb */
		buff = (char *)work->atarequest + sizeof(struct aoe_atahdr);

		generic_file_write(work->abd->fp, buff,
				   (work->atarequest->nsect * 512), &ppos);

		break;

	default:
		/* We didnt understand the command */

		/* aoe error fields */
		work->aoerep->ver_flags |= AOE_FLAG_ERR;
		work->aoerep->error |= AOE_ERR_BADARG;

		/* ata error fields */
		work->atareply->cmdstat |= ERR_STAT;
		work->atareply->err_feature |= ABRT_ERR;
		break;

	}

	/* Send packet */
	aoexmit(work);

	return (0);
}

/* This function replies to an 'identify'-request */
int bldev_identify(struct aoerequest *work)
{
	struct hd_driveid *id;	/* see linux/hdreg.h */

	/* Verify against acl */
	if (aoeblock_acl(work->abd, work->aoereq->eth.h_source) != 0) {
		aoereq_destroy(work);
		return (0);
	}

	/* Allocate space for our reply */
	skb_put(work->skb_rep, 512);

	/* Create a pointer to the data-section of the reply */
	id = (struct hd_driveid *)((char *)work->atareply +
				   sizeof(struct aoe_atahdr));

	/* zero out everything */
	memset(id, 0, 512);

	strcpy(id->model, "123456789");
	strncpy(id->serial_no, work->abd->name, 19);

	/* Set up plain old CHS, its obsolete, but the aoe-client for 
	 * Linux sometimes uses it, so we fill out the fields anyway.
	 * This wont be all that accurate, but i thinks it will work */
	id->cur_cyls = __cpu_to_le16(((work->abd->size >> 8) >> 6));
	id->cur_heads = __cpu_to_le16(255);
	id->cur_sectors = __cpu_to_le16(64);

	/* We support LBA */
	id->capability |= __cpu_to_le16(2);   /* Seccond bit == supports LBA */
	if (work->abd->size > MAXATALBA)
		id->lba_capacity = cpu_to_le32(MAXATALBA);
	else
		id->lba_capacity = cpu_to_le32(work->abd->size);

	/* We support LBA 48 */
	id->command_set_2 |= __cpu_to_le16(((1 << 10))); /* We support LBA48 */
	id->cfs_enable_2  |= __cpu_to_le16(((1 << 10))); /* We use LBA48 */
	id->lba_capacity_2 = __cpu_to_le64(work->abd->size);

	/* We are done, queue reply for transfer */
	aoexmit(work);

	return (0);
}
