"""Wspólny wyjątek walidacji modelu mavlink."""
from __future__ import annotations


class MavlinkModelError(Exception):
    """Błąd modelu mavlink (komunikat zawiera aplikację/usługę i element)."""
