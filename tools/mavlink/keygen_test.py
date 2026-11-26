import os
import re
import stat
import subprocess
import sys

import pytest

import keygen


def test_writes_a_64_hex_key_readable_by_the_owner_only(tmp_path):
    path = tmp_path / "lora.key"
    assert keygen.main([str(path)]) == 0
    assert re.fullmatch(r"[0-9a-f]{64}\n", path.read_text())
    assert stat.S_IMODE(os.stat(path).st_mode) == 0o600


def test_does_not_overwrite_an_existing_key(tmp_path):
    path = tmp_path / "lora.key"
    path.write_text("old\n")
    assert keygen.main([str(path)]) == 1
    assert path.read_text() == "old\n"


def test_does_not_print_the_key(tmp_path):
    path = tmp_path / "lora.key"
    out = subprocess.run([sys.executable, keygen.__file__, str(path)], capture_output=True, text=True)
    assert out.returncode == 0 and out.stdout == "" and out.stderr == ""


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
