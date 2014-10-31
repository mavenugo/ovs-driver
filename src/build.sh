#!/bin/bash

gcc -I../ -I../include -I../third-party/ovs/include -I../third-party/ovs -o sp-ovs-driver sp-ovs-driver.c -L../lib -lopenvswitch -lofproto  -lrt -lm -pthread -w
