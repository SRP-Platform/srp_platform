#!/bin/bash

# Wykrywanie właściwej wersji tar
if [[ "$OSTYPE" == "darwin"* ]]; then
    TAR_BIN="/opt/homebrew/bin/gtar"
    if [ ! -f "$TAR_BIN" ]; then
        echo "Błąd: Plik $TAR_BIN nie istnieje. Zainstaluj gnu-tar (brew install gnu-tar)."
        exit 1
    fi
else
    TAR_BIN="tar"
fi

# Tworzy pusty plik tar
$TAR_BIN -cf "$1" -T /dev/null

# Łączenie plików
for i in "${@:2}"; do
    $TAR_BIN --concatenate --file="$1" "$i"
done