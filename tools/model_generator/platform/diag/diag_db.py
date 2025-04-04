from __future__ import annotations
from tools.model_generator.platform.common.singleton_lib import SingletonMeta
from tools.model_generator.platform.diag.diag_jobs import *
from tools.model_generator.platform.diag.diag_dtc import diag_dtc
class DiagDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.items: dict[str,None] = {}
