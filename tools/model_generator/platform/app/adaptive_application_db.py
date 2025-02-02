from __future__ import annotations
from tools.model_generator.platform.common.singleton_lib import SingletonMeta
from tools.model_generator.platform.app.adaptive_application import AdaptiveApplication

class AdaptiveApplicationDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.app_list: dict[str,AdaptiveApplication] = {}