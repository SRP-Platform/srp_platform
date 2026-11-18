"""Harness tshark + Lua: pcap (UDP 14550) z ramkami MAVLink -> dissector Lua -> pola.

Uzycie (test):
    pcap = write_pcap(path, [frame1, frame2])
    rows = run_tshark(pcap, lua_path, ["mavlink_proto.msgid", "mavlink_proto.SRP_EXAMPLE_STATUS_temp"])
Brak tshark => available() zwraca powod i testy robia skip (lokalnie oba narzedzia sa).
"""
import os
import shutil
import struct
import subprocess

UDP_PORT = 14550


def available():
    """None gdy tshark jest; inaczej tekst powodu skip."""
    if not shutil.which("tshark"):
        return "brak tshark w PATH"
    return None


def _ip_checksum(hdr):
    s = sum(struct.unpack("!10H", hdr))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def udp_ip_packet(payload, sport=14551, dport=UDP_PORT, src="127.0.0.1", dst="127.0.0.1"):
    import socket
    udp = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0,
                      socket.inet_aton(src), socket.inet_aton(dst))
    hdr = hdr[:10] + struct.pack("!H", _ip_checksum(hdr)) + hdr[12:]
    return hdr + udp


def write_pcap(path, datagrams, dport=UDP_PORT):
    """Kazdy element `datagrams` to jeden datagram UDP (bytes; moze zawierac wiele ramek)."""
    with open(path, "wb") as f:
        f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 101))  # LINKTYPE_RAW
        for i, d in enumerate(datagrams):
            pkt = udp_ip_packet(d, dport=dport)
            f.write(struct.pack("<IIII", 1700000000 + i, 0, len(pkt), len(pkt)))
            f.write(pkt)
    return path


def run_tshark(pcap, lua, fields, extra=None, timeout=60):
    """Zwraca liste wierszy (lista stringow, po jednym na pole; wielokrotne wartosci polaczone ',')."""
    cmd = ["tshark", "-X", f"lua_script:{lua}", "-r", pcap, "-T", "fields", "-E", "separator=|", "-E", "occurrence=a",
           "-E", "aggregator=,"]
    for f in fields:
        cmd += ["-e", f]
    cmd += extra or []
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=dict(os.environ, TZ="UTC"))
    if p.returncode != 0:
        raise RuntimeError(f"tshark rc={p.returncode}: {p.stderr}")
    return [line.split("|") for line in p.stdout.splitlines()]


def run_tshark_text(pcap, lua, timeout=60):
    """Pelny dekod (-V) do diagnostyki."""
    p = subprocess.run(["tshark", "-X", f"lua_script:{lua}", "-r", pcap, "-V"], capture_output=True, text=True,
                       timeout=timeout, env=dict(os.environ, TZ="UTC"))
    return p.stdout + p.stderr
