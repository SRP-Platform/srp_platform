#!/usr/bin/env python3
"""keygen - writes a new MAVLink 2 signing key (32 random bytes as 64 hex characters).

    keygen.py /etc/srp/mavlink/lora.key

The file is created readable by its owner only and an existing file is never overwritten
(delete it first to rotate the key). Copy it over a secure channel to the other end of the link
and never commit it. The key is not printed.
"""
import os
import secrets
import sys


def main(argv):
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        fd = os.open(argv[0], os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        print(f"keygen: {argv[0]} already exists", file=sys.stderr)
        return 1
    with os.fdopen(fd, "w") as f:
        f.write(secrets.token_hex(32) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
