from __future__ import annotations
from tools.model_generator.platform.common.singleton_lib import SingletonMeta
from tools.model_generator.platform.component.component import Component
class ComponentDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.list: dict[str,Component] = {}