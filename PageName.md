# Introduction #

This is a multithreaded AoE Target emulator for the linux-kernel.


# Details #

The current status is that it compiles, but INIT\_WORK() needs to be checked and tested. (work in progress)

Todo:

- Add access-support, ie we should only reply to requests that comes from MAC's that are allowed to see the device. (NOT a security feature but more of a way to make sure no misbehaving client can cause problems)

- Display shared disks as local disks to enable the AoE server to see the disk as the clients sees it to enable the server to take part of a OCFS2 or GFS cluster.

- During high load all clients should have equal priority access to the device, including local access. This is to make sure that local access don't starve the networked clients. This will be done by dropping requests from the client that sends the most requests.

- Have a look at the 2.6.23 userspace driver API and see if it's possible to implement the driver via that so we can get around the problems with the everchanging-kernel API.






