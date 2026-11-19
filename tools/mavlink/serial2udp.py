#!/usr/bin/env python3
"""serial2udp - forwards MAVLink frames from a UART (or pty) to UDP, ONE FRAME PER DATAGRAM.

For live Wireshark: the Lua dissector that `ara_mavlink_dialect` generates (<dialect>.lua)
registers on UDP 14550 and needs a whole frame per datagram. The frames are parsed with the
pymavlink module of the same dialect (<dialect>.py), so v1, v2 and signed frames are handled,
bytes that do not form a valid frame are skipped and the parser resynchronizes by itself.
Signatures are forwarded, not checked.

    serial2udp.py /dev/ttyUSB0 srp_example.py --baud 57600
    wireshark -k -i lo -f "udp port 14550" -X lua_script:srp_example.lua
"""
import argparse
import importlib.util
import os
import select
import socket
import sys
import termios
import tty

BAUDS = {rate: getattr(termios, f"B{rate}") for rate in (
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600)}


def open_port(path, baud):
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY)
    try:
        tty.setraw(fd, termios.TCSANOW)  # TCSAFLUSH would drop bytes that already arrived
        attrs = termios.tcgetattr(fd)
        attrs[4] = attrs[5] = BAUDS[baud]
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except termios.error:
        pass  # a pty or a FIFO has no line speed
    return fd


def run(device, dialect, baud=57600, host="127.0.0.1", port=14550, stop=None):
    """Forwards frames until `stop` (a threading.Event) is set; returns the number forwarded."""
    spec = importlib.util.spec_from_file_location("serial2udp_dialect", dialect)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    parser = mod.MAVLink(None)
    parser.robust_parsing = True  # a bad byte or CRC yields BAD_DATA (dropped below), not an exception
    fd, sock, sent = open_port(device, baud), socket.socket(socket.AF_INET, socket.SOCK_DGRAM), 0
    try:
        while not (stop and stop.is_set()):
            if not select.select([fd], [], [], 0.05)[0]:
                continue
            data = os.read(fd, 4096)
            if not data:
                break
            for msg in parser.parse_buffer(data) or []:
                if msg.get_type() != "BAD_DATA":
                    sock.sendto(msg.get_msgbuf(), (host, port))
                    sent += 1
    finally:
        os.close(fd)
        sock.close()
    return sent


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("device", help="UART or pty, for example /dev/ttyUSB0")
    p.add_argument("dialect", help="pymavlink module of the CPU dialect (<dialect>.py)")
    p.add_argument("--baud", type=int, default=57600, choices=sorted(BAUDS))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=14550)
    a = p.parse_args(argv)
    try:
        run(a.device, a.dialect, a.baud, a.host, a.port)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
