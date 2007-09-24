/*                                            
 *  linux/drivers/block/aoeserver/aoe.h
 * 
 * ATA Over Ethernet storage target for Linux.
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
  * Definitions for the ATA over Ethernet protocol.
  */


#define PRIV_ETH_P_AOE 0x88A2

#include <linux/workqueue.h> /* work_struct */
#include <linux/if_ether.h>	/* eth-struct used in aoe-header */

/* Valid commands for aoeproc.c */
#define CMDEINVAL   ((int)( -1))
#define CMDREG      ((int)(  0))
#define CMDUNREG    ((int)(  1))
#define CMDHOSTMASK ((int)(  3))
#define CMDRMMASK   ((int)(  4))

/* Bit field in ver_flags of aoe-header */
#define AOE_FLAG_RSP (1<<3)
#define AOE_FLAG_ERR (1<<2)

/* Error codes for the error field in the aoe header */
#define AOE_ERR_BADCMD       1	/* Unrecognized command code */
#define AOE_ERR_BADARG       2	/* Bad argument parameter */
#define AOE_ERR_DEVUNAVAIL   3	/* Device unavailable */
#define AOE_ERR_CFG_SET      4	/* Config string present */
#define AOE_ERR_UNSUPVER     5	/* Unsupported version */

/* Commands in cmd-field of aoe-header */
#define AOE_CMD_ATA 0
#define AOE_CMD_CFG 1

struct aoe_hdr {
	struct ethhdr eth;   /* ethernet header, see linux/if_ether.h */
	u8 ver_flags;	     /* bit field: 7-4 ver, 3 rsp, 2 error, 0&1 zero */
	u8 error;	     /* If error bit is set, fill out this field */
	u16 shelf;
	u8 slot;
	u8 cmd;		     /* ATA or CFG */
	u32 tag;	     /* Uniqueue request tag */
} __attribute__ ((packed));

#define MAXATALBA (int)(0x0fffffff)

/* Bitfields for the flags field in the aoe ata header */
#define AOE_ATAFLAG_LBA48 (1 << 6)
#define AOE_ATAFLAG_ASYNC (1 << 1)
#define AOE_ATAFLAG_WRITE (1 << 0)

struct aoe_atahdr {
	u8 flags;		/* bitfield: 7 zero, 6 LBA48, 5 zero
				 * 4 Device/head, 3 zero, 2 zero, 
				 * 1 Async IO, 0 Read/Write */
	u8 err_feature;		/* Check linux/hdreg.h for status codes */
	u8 nsect;		/* Number of sectors to transfer */
	u8 cmdstat;		/* cmd in request and status in reply */
	u8 lba[6];		/* 48bit LBA addressing */
	u8 notused[2];		/* reserved */
} __attribute__ ((packed));

struct aoe_cfghdr {
	u16 queuelen;		/* The number of requests we can queue */
	u16 firmware;		/* Firmware version */
	u8 notused;		/* Reserved field */
	u8 aoever_cmd;		/* bit field: 7-4 aoe-version, 3 - 0 cmd */
	u16 data_len;		/* data length */
} __attribute__ ((packed));

/* List of hosts allowed to see this device */
struct accesslist {
	struct list_head list;
	unsigned char h_source[ETH_ALEN];
};

struct aoeblkdev {
	struct list_head list;	/* see linux/list.h */
	struct file *fp;	/* Pointer to open device */
	u8 name[32];		/* Name of the open device */
	int ifindex;	    /* if (>1) We only accept traffic on this device */
	u8 cfg_data[1024];	/* Config data */
	u16 cfg_len;		/* Lenght of config data */
	u64 size;		/* size of the device */
	u16 shelf;
	u8 slot;
	rwlock_t acl_lock;	/* lock for accessing the access list */
	struct accesslist *acl;	/* Access list for this device */
	struct workqueue_struct *kaoed_wq;  /* Each device has its own wq */
	atomic_t queuecounter;	/* How many packets that are currently in the queue */
};

/* struct used to queue work with the kaoed thread and kernel block io */
struct aoerequest {
	struct work_struct work;	/* struct used for work-queues */
	struct sk_buff *skb_req;	/* pointer to the original request */
	struct sk_buff *skb_rep;	/* pointer to our reply */
	struct net_device *ifp;	/* interface that the request came in on */

	/* pointers into the skb-structs */
	struct aoe_hdr *aoereq;
	struct aoe_hdr *aoerep;
	/* Only valid if this is an ata-request */
	struct aoe_atahdr *atarequest;
	struct aoe_atahdr *atareply;

	/* Pointer to the block device to tranfser to/from */
	struct aoeblkdev *abd;
};

/* aoenet.c */
int aoenet_init(void);
void aoenet_exit(void);
/* end aoenet.c */

/* aoepacket.c */
void kaoed(struct work_struct *data);
void aoepacket(struct aoerequest *work);
void handleata(struct aoerequest *work);
void handleconfig(struct aoerequest *work);
struct sk_buff *create_skb(struct net_device *outdev, struct aoerequest *work);
void aoexmit(struct aoerequest *work);
/* end aoepacket.c */

/* aoerblock.c */
int aoeblock_init(void);
void aoeblock_exit(void);
int bldev_transfer(struct aoerequest *work);
int bldev_identify(struct aoerequest *work);
int aoeblock_register(char *device, int major, int minor, int ifindex);
int aoeblock_unregister(char *device, int major, int minor, int ifindex);
struct aoeblkdev *find_aoedevice(int major, int minor, int ifindex);
int aoeblock_mask(unsigned short shelf, unsigned short slot,
		  unsigned char *h_source);
int aoeblock_rmmask(unsigned short shelf, unsigned short slot,
		    unsigned char *h_source);
int aoeblock_acl(struct aoeblkdev *abd, unsigned char *h_source);
/* end aoeblock.c */

/* aoewq.c */
void aoewq_init(struct aoeblkdev *abd);
void aoewq_exit(struct aoeblkdev *abd);
void aoewq_addreq(struct sk_buff *skb, struct net_device *ifp,
		  struct aoeblkdev *abd);
int submit_work(struct aoerequest *workreq, int type);
void aoereq_destroy(struct aoerequest *work);
void aoedecqueue(struct aoeblkdev *abd);
int aoecheckqueue(struct aoeblkdev *abd);
/* end aoewq.c */

/* aoeproc.c */
int aoeproc_init(void);
int aoeproc_exit(void);
/* end aoeproc.c */
