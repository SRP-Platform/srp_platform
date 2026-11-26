"""Error of a model (JSON) file: the message names the application, service, element or key."""
from __future__ import annotations


class ModelError(Exception):
    """A model is invalid; generator entry points print it as `ERROR: <message>` and exit 1."""
