#!/bin/bash
#!/bin/bash
if [ $# -lt 1 ]; then
	echo "Usage: $0 <cmd> {options} " 
	echo "cmd: add / del <path to device> <shelf> <slot> [interface]"
	echo "cmd: hostmask <shelf> <slot> <mac address>"
	echo "cmd: rmmask   <shelf> <slot> <mac address>"
	exit 1
fi

echo $* > /proc/aoeserver
