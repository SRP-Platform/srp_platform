"""tshark + Lua harness: a pcap (UDP 14550) of MAVLink frames -> the generated Lua dissector -> fields.

    pcap = write_pcap(path, [frame1, frame2])
    rows = run_tshark(pcap, lua_path, ["mavlink_proto.msgid", "mavlink_proto.sysid"])

The Wireshark dissector is a deliverable, so a missing tshark fails the test (it never skips).
"""
import os
import shutil
import socket
import struct
import subprocess

UDP_PORT = 14550


def require_tshark():
    assert shutil.which("tshark"), "tshark is required to test the generated Lua dissector"


def _ip_checksum(hdr):
    s = sum(struct.unpack("!10H", hdr))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def _udp_ip_packet(payload, sport=14551, dport=UDP_PORT):
    udp = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0,
                      socket.inet_aton("127.0.0.1"), socket.inet_aton("127.0.0.1"))
    hdr = hdr[:10] + struct.pack("!H", _ip_checksum(hdr)) + hdr[12:]
    return hdr + udp


def write_pcap(path, datagrams):
    """Each element of `datagrams` is one UDP datagram (bytes; it may hold several frames)."""
    with open(path, "wb") as f:
        f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 101))  # LINKTYPE_RAW
        for i, d in enumerate(datagrams):
            pkt = _udp_ip_packet(d)
            f.write(struct.pack("<IIII", 1700000000 + i, 0, len(pkt), len(pkt)))
            f.write(pkt)
    return path


def run_tshark(pcap, lua, fields, timeout=60):
    """Returns one row (a list of strings, one per field) per frame."""
    cmd = ["tshark", "-X", f"lua_script:{lua}", "-r", pcap, "-T", "fields", "-E", "separator=|",
           "-E", "occurrence=a", "-E", "aggregator=,"]
    for f in fields:
        cmd += ["-e", f]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                       env=dict(os.environ, TZ="UTC"))
    if p.returncode != 0:
        raise RuntimeError(f"tshark rc={p.returncode}: {p.stderr}")
    return [line.split("|") for line in p.stdout.splitlines()]
