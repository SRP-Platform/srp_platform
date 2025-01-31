#!/bin/bash

INTERFACE="tap0"
IP_ADDRESS="192.168.10.4"
NETMASK="255.255.255.0"

sudo ip tuntap add dev $INTERFACE mode tap

sudo ip addr add $IP_ADDRESS/$NETMASK dev $INTERFACE

sudo ip link set dev $INTERFACE up
