XCP-ng Standard VGA Display Driver
==================================

This driver provides high-resolution consoles and resolution switching for
Windows UEFI VMs running on the XCP-ng hypervisor.

To install this package, right-click the INF file and select "Install".
Alternatively, you can install it with the pnputil.exe command:

    pnputil.exe -i -a xstdvga.inf

The VGA option of the VM must be set to "std" (the default on XCP-ng VMs).
To check your VM's configuration, use the following command:

    xe vm-param-get uuid=<UUID> param-name=platform param-key=vga

To configure your VM to use the "std" VGA emulation, use the following command:

    xe vm-param-set uuid=<UUID> platform:vga=std
