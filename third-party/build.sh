#!/bin/bash

# Please install all the OVS Build pre-reqs
# sudo apt-get install linux-headers-3.13.0-32-generic build-essential autoconf openssh-server libtool

cd ovs && ./boot.sh && ./configure && make
