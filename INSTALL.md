# Building and Installing RDPMux

In general, RDPMux should work on any recent Linux distribution. It has been tested on Fedora 23, Arch Linux, and Ubuntu 16.04. It should work on Ubuntu 14.04 without any modification as long as the dependencies have been installed, although this has not been tested extensively. Anything much older than that, and we make no guarantees.

## Dependencies

RDPMux depends on any version of the following libraries:
* GLibmm 2.4
* Msgpack-C++
* Pixman
* Boost.Program_options
* ZeroMQ

RDPMux also depends on FreeRDP 2.0. Since there hasn't been a formal release of FreeRDP 2.0 yet, RDPMux has been built and tested against what is essentially the FreeRDP project's git master. Once FreeRDP formally releases v2.0, we will depend on explicit versions as per usual. 

Note: RDPMux will definitely _not_ build on any 1.x release of FreeRDP. 

## Building and Installing RDPMux

RDPMux uses CMake as its build system. Once you have all your dependencies in order, run the following commands:

```
cmake .
make
sudo make install
```
