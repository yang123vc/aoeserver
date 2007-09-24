/*                                            
 *  README.txt
 * 
 * Ata Over Ethernet storage target for Linux.
 */

/* 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *  Copyright (C) 2005 - 2007  wowie@pi.nxs.se
 */


 
Aoeserver is an in-kernel Ata Over Ethernet Storage target driver used
to emulate a Coraid EtherDriver Blade. It is partly based on vblade and
the aoe-client from the Linux 2.6-kernel. 

For more information on AoE, have a look at the specifications,
 http://www.coraid.com/documents/AoEDescription.pdf
 http://www.coraid.com/documents/AoEr8.txt

Also, your client experience will be greatly improved by the userland-tools:
  http://sourceforge.net/projects/aoetools/
  

This driver has only a few advantages against vlabde, the userland
implementation of the same protocol: 

  * aoeserver has a slightly better performance during normal operation.
  * aoeserver has significantly better performance when exporting serveral
    target drives.
  * aoeserver supports masking of different hosts.
  
On the other hand, there are a few drawbacks as compared to vblade:
 * vblade is userland software, making it mutch easier to maintain and
   port to other platforms. 
 * aoeserver doesnt comply fully with the standard due to the hostmasking.
 
For a referance on another in-kernel implementation of the AoE-protocol,
see http://lpk.com.price.ru/lelik/AoE/vblade-kernel-0.3.2.tar.gz, the main
differance between vblade-kernel and aoeserver is that vblade-kernel uses
only a single kernel-thread and is completely un-buffered on the server-side.
aoeserver uses multiple kernel-threads in order to handle server-side caching,
making it faster, quite a lot faster especially when exporting small targets.

Usage, 

  To add a drive, echo the command "add" followed by the appropriate
  options to /proc/aoeserver. The options are 
     device-name   -  for instance /dev/hda
     shelf         -  shelf-number 0 - 65545 (use 0 to begin with)
     slot          -  slot number 0 - 255 (start with 1 and go upwards)
     interface     -  If you want to export the device on a specific if
     
  For instace, to add the device /dev/md0 with shelf 0 and slot 3 on eth1 use
  the following command: "echo add /dev/md0 0 3 eth1 > /proc/aoeserver"
  If you omit the interface, the device will be exported on all available
  ethernet-interfaces. 
  
  To remove the interface, use the command "del" with the same syntax as 
  for adding devices, if you specified an interface when you added the
  interface you need to specify the interface when removing it as well. 
  
  Hostmasks are used to restrict visabiltity of a device to a specific host.
  THIS IS NOT A SECURITY FEATURE, its strictly an administrative feature that
  can be used to filter incoming identify requests, it doesnt filter
  read/write calls for performance-reasons so it cant be used for security.
  
  To add a hostmask you must first export the device, then add the hostmask
  to the specific shelf and slot-number. To restrict e0.3 to the machine with
  the mac-address 00:01:02:03:04:05 use the command "hostmask", 
  "echo hostmask 0 3 00:01:02:03:04:05 > /proc/aoeserver". You can remove
  the hostmask again using the same syntax and the command "rmmask". 
  Since its possible that you want to add a hostmask after adding the device,
  the aoeserver will _not_ broadcast the presence of the device as startup.
  
  The status of all devices, as well as the access-lists associated with them
  can be viewed by reading from /proc/aoeserver, for instance using cat; 
  cat /proc/aoeserver 
  
  A small shellscript aoectrl.sh is included to help out with the syntax
  for controling the aoeserver storage target. 
  
  

     
