#!/usr/bin/env python3
"""keygen - generuje klucz podpisu MAVLink v2 (32 losowe bajty jako 64 znaki hex) do pliku z uprawnieniami 0600.

Uzycie:
    keygen.py /etc/srp/mavlink/lora.key          # nowy plik; istniejacy = blad
    keygen.py KLUCZ.key --force                  # nadpisanie (np. rotacja klucza)

Ten sam plik wskazuje `signing.key_file` w modelu aplikacji (po obu stronach lacza klucz musi byc identyczny: skopiuj plik
bezpiecznym kanalem na stacje naziemna). Klucza NIGDY nie wolno commitowac do repozytorium - narzedzie odmawia zapisu
wewnatrz drzewa git (chyba ze --allow-in-repo, tylko do testow). Klucz nie jest wypisywany na stdout/stderr.
"""
import argparse
import os
import secrets
import sys

KEY_BYTES = 32


def inside_git_repo(path):
    d = os.path.dirname(os.path.realpath(path))  # realpath: symlink do pliku w repo tez jest repo
    while True:
        if os.path.exists(os.path.join(d, ".git")):
            return True
        parent = os.path.dirname(d)
        if parent == d:
            return False
        d = parent


def generate(path, force=False, allow_in_repo=False):
    """Zapisuje klucz do `path` (0600). Zwraca sciezke; rzuca FileExistsError / PermissionError."""
    if inside_git_repo(path) and not allow_in_repo:
        raise PermissionError(f"{path}: wewnatrz repozytorium git - klucz nie moze trafic do repo")
    target = os.path.realpath(path)  # zapis do rzeczywistego pliku (to, co sprawdzil guard)
    directory = os.path.dirname(target)
    os.makedirs(directory, mode=0o700, exist_ok=True)  # nowe katalogi tylko dla wlasciciela
    flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if force else os.O_EXCL)
    fd = os.open(target, flags, 0o600)
    try:
        os.fchmod(fd, 0o600)  # takze przy nadpisaniu istniejacego pliku o luznych uprawnieniach
        os.write(fd, (secrets.token_bytes(KEY_BYTES).hex() + "\n").encode("ascii"))
    finally:
        os.close(fd)
    return path


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("path", help="plik wyjsciowy (np. /etc/srp/mavlink/lora.key)")
    p.add_argument("--force", action="store_true", help="nadpisz istniejacy plik")
    p.add_argument("--allow-in-repo", action="store_true", help="pozwol na zapis w drzewie git (tylko testy)")
    a = p.parse_args(argv)
    try:
        generate(a.path, a.force, a.allow_in_repo)
    except FileExistsError:
        print(f"keygen: {a.path} juz istnieje (uzyj --force, aby nadpisac)", file=sys.stderr)
        return 1
    except PermissionError as e:
        print(f"keygen: {e}", file=sys.stderr)
        return 1
    print(f"keygen: zapisano {a.path} (0600, {KEY_BYTES} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
