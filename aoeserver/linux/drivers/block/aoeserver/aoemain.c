/*                                            
 *  linux/drivers/block/aoeserver/aoemain.c
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
  * The functions in this file takes care of loading and unloading the 
  * aoeserver-module. 
  */

#include <linux/kernel.h>
#include <linux/module.h>
#include "aoe.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wowie wowie@hack.se");
MODULE_DESCRIPTION("ATA over Ethernet storage target driver");

static int __init aoe_init(void)
{
	aoeproc_init();
	return (0);
}

static void aoe_exit(void)
{

	aoeblock_exit();
	aoeproc_exit();
}

module_init(aoe_init);
module_exit(aoe_exit);
