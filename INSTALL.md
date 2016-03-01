# Building and Installing RDPMux

In general, RDPMux should work on any recent Linux distribution. It has been tested on Fedora 23, Arch Linux, and Ubuntu 16.04. It should work on Ubuntu 14.04 without any modification as long as the dependencies have been installed, although this has not been tested extensively. Anything much older than that, and we make no guarantees.

## Dependencies

RDPMux depends on any version of the following libraries:
* GLibmm 2.4
* Msgpack-C++
* Pixman
* Boost.Program_options

RDPMux also depends on FreeRDP. RDPMux has been tested and is known to work with [FreeRDP commit be8f8f72387e7878717b6f04c9a87f999449d20d](https://github.com/FreeRDP/FreeRDP/tree/be8f8f72387e7878717b6f04c9a87f999449d20d). This is essentially a commit picked at random that we froze on for development work, since the FreeRDP project hasn't formally released a new version in almost three years. Theoretically, any later commit should work just as well, though the FreeRDP project is still in the middle of changing their API so there is no guarantee.

Note: RDPMux will definitely _not_ build on any 1.x release of FreeRDP. 

## Building and Installing RDPMux

RDPMux uses CMake as its build system. Once you have all your dependencies in order, run the following commands:

```
cmake .
make
sudo make install
```
