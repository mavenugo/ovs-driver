ovs-driver
==========
A simple driver library for OVS that makes it easy for applications from other languages (like Go) to easily consume OVS libraries.

OVS source is added as a git sub-module under third-party.
Please follow these steps to successfully clone and build the ovs-driver library

1. git clone --recursive https://github.com/mavenugo/ovs-driver
   * This will also pull the ovs sub-module into third-party/
   * If the cloning is done without the --recursive, you can use this command *git submodule update --init --recursive* to pull and update the submodule

2. Install all the build tools pre-req as specified in target-specific ovs INSTALL documents.

3. execute build.sh from the third-party/ directory
   * This should build the OVS source in third-party and copy the static libraries into lib/ directory

4. build ovs-driver by running make in the root directory.
   * This should build and copy libovsdriver.a into the lib/ directory

5. Refer to the ovs-driver.h in include/ directory and start using the libraries in your application.
