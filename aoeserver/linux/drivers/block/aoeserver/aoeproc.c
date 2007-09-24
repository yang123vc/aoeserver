/*                                            
 *  linux/drivers/block/aoeserver/aoeproc.c
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
  * The functions in this file handles everything that has do with
  * our proc-file /proc/aoeserver.
  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include "aoe.h"

#define PROCFSNAME "aoeserver"
#define NARGSMAX 5     /* Maximum number of arguments we currently support */
struct proc_dir_entry *procfile = NULL;

/* imported from aoeblock.c */
extern struct aoeblkdev *abd_head;
extern rwlock_t abd_lock;

/* Print out information about our devices when someone reads from 
 * /proc/aoeserver */
static int aoeproc_seq_show(struct seq_file *s, void *p)
{
	struct accesslist *acl = NULL;
	struct aoeblkdev *abd = NULL;
	struct net_device *dev = NULL;

	/* Header */
	seq_printf(s, "#%s              %s     %s     %s\n",
		   "<device>", "<shelf>", "<slot>", "<interface>");

	/* If the linked list is empty, we have nothing more to give */
	if (abd_head == NULL)
		return (0);

	read_lock(&abd_lock);
	{
		list_for_each_entry(abd, &abd_head->list, list) {
			seq_printf(s, "%-25s %-10d %-10d", abd->name,
				   abd->shelf, abd->slot);
			if (abd->ifindex > 0)
				if ((dev = dev_get_by_index(abd->ifindex))) {
					seq_printf(s, "%s", dev->name);
					dev_put(dev);
				}
			seq_putc(s, '\n');
		}

		seq_printf(s, "\n# access lists\n");
		seq_printf(s, "#%s     %s       %s  \n",
			   "<shelf>", "<slot>", "<allowed host>");

		list_for_each_entry(abd, &abd_head->list, list)
		    if (abd->acl != NULL) {
			read_lock(&abd->acl_lock);
			list_for_each_entry(acl, &abd->acl->list, list) {
				seq_printf(s, "%-14d %-10d",
					   abd->shelf, abd->slot);

				seq_printf(s, "%02X:%02X:%02X:%02X:%02X:%02X",
					   acl->h_source[0],
					   acl->h_source[1],
					   acl->h_source[2],
					   acl->h_source[3],
					   acl->h_source[4], acl->h_source[5]);
				seq_putc(s, '\n');
			}
			read_unlock(&abd->acl_lock);
		}
	}
	read_unlock(&abd_lock);

	return (0);
}

static int aoeproc_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, aoeproc_seq_show, NULL);
}

int ascii2mac(const char *addr, unsigned char *res)
{
	int ret = 0;
	unsigned int conv[6];
	ret = sscanf(addr, "%X:%X:%X:%X:%X:%X",
		     &conv[0], &conv[1], &conv[2], &conv[3], &conv[4],
		     &conv[5]);

	if (ret != 6)
		return (-EINVAL);

	/* convert from unsigned int to unsigned char */
	res[0] = (unsigned char)conv[0];
	res[1] = (unsigned char)conv[1];
	res[2] = (unsigned char)conv[2];
	res[3] = (unsigned char)conv[3];
	res[4] = (unsigned char)conv[4];
	res[5] = (unsigned char)conv[5];

	return (0);
}

/* Restrict access */
int cmd_mask(int argc, char **argv)
{
	unsigned char h_source[ETH_ALEN];
	unsigned short slot;
	unsigned short shelf;

	if (argc < 4)
		return (-EINVAL);

	/* Convert slot and shelf */
	shelf = simple_strtoul(argv[1], NULL, 0);
	slot = simple_strtoul(argv[2], NULL, 0);

	/* Try to convert mac address */
	if (ascii2mac(argv[3], h_source) != 0)
		return (-EINVAL);

	if (aoeblock_mask(shelf, slot, h_source) == 0)
		return (0);
	else
		return (-EINVAL);
}

/* Restrict access */
int cmd_rmmask(int argc, char **argv)
{
	unsigned char h_source[ETH_ALEN];
	unsigned short slot;
	unsigned short shelf;

	if (argc < 4)
		return (-EINVAL);

	/* Convert slot and shelf */
	shelf = simple_strtoul(argv[1], NULL, 0);
	slot = simple_strtoul(argv[2], NULL, 0);

	/* Try to convert mac address */
	if (ascii2mac(argv[3], h_source) != 0)
		return (-EINVAL);

	if (aoeblock_rmmask(shelf, slot, h_source) == 0)
		return (0);
	else
		return (-EINVAL);
}

/* Register a block device */
int cmd_register(int argc, char **argv)
{
	char *device;
	struct net_device *dev;
	unsigned short slot;
	unsigned short shelf;
	int ifindex = 0;

	if (argc < 4)
		return (-EINVAL);

	device = argv[1];

	/* Convert slot and shelf */
	shelf = simple_strtoul(argv[2], NULL, 0);
	slot = simple_strtoul(argv[3], NULL, 0);

	/* If interface was specifies, convert it to ifindex */
	if (argc == 5) {
		if (argv[4] == NULL) {
			printk("argv[4] == NULL, argc == %d \n", argc);
			return (-EINVAL);
		}

		dev = dev_get_by_name(argv[4]);
		if (dev == NULL)
			return (-EINVAL);

		ifindex = dev->ifindex;

		/* Subtle - get_dev_by_name() incremented the usage count */
		dev_put(dev);
	}

	if (slot > 255)
		return (-EINVAL);

	if (aoeblock_register(device, shelf, slot, ifindex) != 0)
		return (-EINVAL);
	else
		return (0);

}

/* unregister a blockdevice */
int cmd_unregister(int argc, char **argv)
{
	char *device;
	struct net_device *dev;
	unsigned short slot;
	unsigned short shelf;
	int ifindex = 0;

	if (argc < 4)
		return (-EINVAL);

	device = argv[1];

	/* Convert slot and shelf */
	shelf = simple_strtoul(argv[2], NULL, 0);
	slot = simple_strtoul(argv[3], NULL, 0);

	if (slot > 255)
		return (-EINVAL);

	/* If interface was specifies, convert it to ifindex */
	if (argc == 5) {
		if (argv[4] == NULL) {
			printk("argv[4] == NULL, argc == %d \n", argc);
			return (-EINVAL);
		}

		dev = dev_get_by_name(argv[4]);
		if (dev == NULL)
			return (-EINVAL);

		ifindex = dev->ifindex;

		/* Subtle - get_dev_by_name() incremented the usage count */
		dev_put(dev);
	}

	if (aoeblock_unregister(device, shelf, slot, ifindex) != 0)
		return (-EINVAL);
	else
		return (0);

}

/* Read and parce a command from userland */
int aoeproc_write(struct file *fp, const char *buffer, size_t count,
		  loff_t * data)
{
	char cmd[100];
	int nargs = 0;
	int arg0 = CMDEINVAL;
	unsigned int cmdlen;
	char *argv[NARGSMAX + 1];

	if (count > (sizeof(cmd) - 1))
		return (-EINVAL);

	if (copy_from_user(cmd, buffer, count))
		return (-EFAULT);

	/* ZT string */
	cmd[count] = (char)'\0';
	cmdlen = strlen(cmd);

	/* Remove trailing \n */
	if (cmd[cmdlen - 1] == '\n')
		cmd[--cmdlen] = 0;

	/* argv[0] is the command */
	argv[0] = cmd;

	if (strncmp(argv[0], "add", 3) == 0)
		arg0 = CMDREG;
	else if (strncmp(argv[0], "del", 3) == 0)
		arg0 = CMDUNREG;
	else if (strncmp(argv[0], "hostmask", 8) == 0)
		arg0 = CMDHOSTMASK;
	else if (strncmp(argv[0], "rmmask", 6) == 0)
		arg0 = CMDRMMASK;

	if (arg0 == CMDEINVAL)
		goto parse_error;

	/* Pick out arguments */
	for (nargs = 1; nargs < NARGSMAX; nargs++) {
		/* Parse next argument */
		argv[nargs] = strstr(argv[nargs - 1], " ");
		if (argv[nargs] == NULL)
			break;

		/* Sanity check */
		if ((argv[nargs] - cmd) > count)
			goto parse_error;

		/* ZT string */
		*(argv[nargs]++) = 0;
	}

	switch (arg0) {
	case CMDREG:
		if (cmd_register(nargs, argv) != 0)
			goto parse_error;
		break;

	case CMDUNREG:
		if (cmd_unregister(nargs, argv) != 0)
			goto parse_error;
		break;

	case CMDHOSTMASK:
		if (cmd_mask(nargs, argv) != 0)
			goto parse_error;
		break;

	case CMDRMMASK:
		if (cmd_rmmask(nargs, argv) != 0)
			goto parse_error;
		break;

	default:
		printk(KERN_ERR "aoeproc.c: Unknown command\n");
		goto parse_error;
	}

	return (count);

      parse_error:
	return (-EINVAL);
}

/* Describes file operations for /proc/aoeserver */
static struct file_operations proc_seq_file_ops = {
	.open = aoeproc_seq_open,
	.read = seq_read,
	.write = aoeproc_write,
	.llseek = seq_lseek,
	.release = single_release
};

/* Create /proc/aoeserver */
int aoeproc_init(void)
{
	procfile = create_proc_entry(PROCFSNAME, 0644, NULL);

	if (NULL == procfile) {
		printk(KERN_ERR "Failed to create proc-entry!\n");
		remove_proc_entry(PROCFSNAME, &proc_root);
		return -ENOMEM;
	}

	procfile->owner = THIS_MODULE;
	procfile->proc_fops = &proc_seq_file_ops;
	procfile->mode = S_IFREG | S_IRUGO;
	procfile->uid = 0;
	procfile->gid = 0;
	procfile->size = 6;

	return (0);
}

/* Remove /proc/aoeserver */
int aoeproc_exit(void)
{
	if (NULL != procfile) {
	       remove_proc_entry(PROCFSNAME, &proc_root);
	       procfile = NULL;
	       return (0);
	} else
	       printk(KERN_INFO "aoeproc_exit called when procfile == NULL\n");

	return (0);
}
