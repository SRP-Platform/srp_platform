"""Testy keygen: uprawnienia 0600, format (64 hex = 32 B), losowosc, brak nadpisania, odmowa zapisu w repo, brak klucza w wyjsciu."""
import os
import stat
import sys

import pytest

import keygen


def read_key(path):
    text = open(path).read()
    assert text.endswith("\n") and len(text.strip()) == 64
    return bytes.fromhex(text.strip())


def test_creates_32_byte_hex_key_with_0600(tmp_path):
    p = str(tmp_path / "sub" / "lora.key")
    assert keygen.main([p, "--allow-in-repo"]) == 0
    assert stat.S_IMODE(os.stat(p).st_mode) == 0o600
    assert len(read_key(p)) == 32


def test_new_parent_directory_is_0700(tmp_path):
    p = str(tmp_path / "newdir" / "k.key")
    keygen.generate(p, allow_in_repo=True)
    assert stat.S_IMODE(os.stat(os.path.dirname(p)).st_mode) == 0o700


def test_symlink_into_git_tree_is_refused(tmp_path):
    repo = tmp_path / "repo"
    (repo / ".git").mkdir(parents=True)
    link = tmp_path / "link.key"
    link.symlink_to(repo / "k.key")
    with pytest.raises(PermissionError):
        keygen.generate(str(link), force=True)
    assert not (repo / "k.key").exists()


def test_symlink_outside_git_tree_writes_the_target(tmp_path):
    target = tmp_path / "real.key"
    link = tmp_path / "link.key"
    link.symlink_to(target)
    keygen.generate(str(link), force=True)
    assert len(target.read_text().strip()) == 64


def test_two_keys_differ(tmp_path):
    a, b = str(tmp_path / "a.key"), str(tmp_path / "b.key")
    keygen.generate(a, allow_in_repo=True)
    keygen.generate(b, allow_in_repo=True)
    assert read_key(a) != read_key(b)


def test_existing_file_is_not_overwritten_without_force(tmp_path, capsys):
    p = str(tmp_path / "k.key")
    keygen.generate(p, allow_in_repo=True)
    first = open(p).read()
    assert keygen.main([p, "--allow-in-repo"]) == 1
    assert open(p).read() == first
    assert "juz istnieje" in capsys.readouterr().err
    assert keygen.main([p, "--allow-in-repo", "--force"]) == 0
    assert open(p).read() != first


def test_force_tightens_permissions_of_existing_file(tmp_path):
    p = str(tmp_path / "k.key")
    open(p, "w").write("x")
    os.chmod(p, 0o644)
    keygen.generate(p, force=True, allow_in_repo=True)
    assert stat.S_IMODE(os.stat(p).st_mode) == 0o600


def test_refuses_to_write_inside_git_repo(tmp_path, capsys):
    (tmp_path / ".git").mkdir()
    p = str(tmp_path / "keys" / "k.key")
    assert keygen.main([p]) == 1
    assert not os.path.exists(p) and "repozytorium" in capsys.readouterr().err


def test_key_never_printed(tmp_path, capsys):
    p = str(tmp_path / "k.key")
    keygen.main([p, "--allow-in-repo"])
    out = capsys.readouterr()
    key = open(p).read().strip()
    assert key not in out.out and key not in out.err


def test_key_has_enough_entropy_across_many_keys(tmp_path):
    keys = set()
    for i in range(50):
        p = str(tmp_path / f"{i}.key")
        keygen.generate(p, allow_in_repo=True)
        keys.add(open(p).read())
    assert len(keys) == 50


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v", "-p", "no:cacheprovider"]))
