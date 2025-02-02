#!/bin/bash

INTERFACE="tap0"
IP_ADDRESS="192.168.10.4"
NETMASK="255.255.255.0"

ip tuntap add dev $INTERFACE mode tap

ip addr add $IP_ADDRESS/$NETMASK dev $INTERFACE

ip link set dev $INTERFACE up
