# RDPMux

## Table of contents
1. [Introduction](#introduction)
2. [Rationale](#rationale)
3. [Protocol](#protocol)
4. [FAQ](#faq)

## Introduction
RDPMux provides multiplexed RDP servers for virtual machines. 

It communicates with VMs via librdpmux, which implements the communication protocol and exposes an API for hypervisors to hook into. More information about the communication protocol is in the PROTOCOL section of this document.

For build and installation instructions, see [INSTALL.md](./INSTALL.md).

If you'd like to try RDPMux out, we have a reference implementation of the librdpmux integration for QEMU. Since the librdpmux integration code has not been merged into QEMU upstream (yet!), [get our custom fork here](https://github.com/datto/qemu/tree/librdpmux-integration). 

For usage instructions, see [USAGE.md](./USAGE.md).

## Rationale

RDPMux was initially intended as a project to build support for the FreeRDP library into QEMU, in much the same way as SPICE. However, licensing incompatibilities necessitated the decision to split the RDP server functionality into its own project. This also has the advantage of making RDPMux more general than just providing an RDP frontend for QEMU virtual machines.

## Protocol
For complete documentation on the wire protocol used to communicate with the hypervisors, see the documentation in [librdpmux](https://github.com/datto/librdpmux).

For information on the RDP protocol itself, refer to [FreeRDP](https://github.com/freerdp/FreeRDP) and [Microsoft's own RDP documentation](https://msdn.microsoft.com/en-us/library/cc240445.aspx).

## FAQ

**Why didn't you build this into QEMU the way SPICE does it?**

Several reasons. In no particular order:
* This code links against FreeRDP, which is licensed incompatibly to QEMU. Upstream would never have accepted this code on that basis alone.
* By shipping this code in two logical pieces, it makes it easier for us to ship updates and fixes in a non-disruptive manner.
* SPICE actually doesn't build their code directly into QEMU; the SPICE "server" (that interfaces directly with the VM guest) is implemented as a library, and clients are obviously standalone applications.

**Why are you using Nanomsg? That project seems to be abandoned!**

Well, possibly. The situation with Nanomsg is still up in the air. As it stands, we don't use Nanomsg for very much more than a small full-duplex socket library anyway, so it's a fairly minimal concern to us if it's unsupported.
