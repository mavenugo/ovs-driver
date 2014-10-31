#!/bin/bash

gcc -I../ -I../include -I../third-party/ovs/include -I../third-party/ovs -o ovs-driver ovs-driver.c -L../lib -lopenvswitch -lofproto  -lrt -lm -pthread -w
