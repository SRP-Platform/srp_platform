from __future__ import annotations
from tools.model_generator.ara.common.singleton_lib import SingletonMeta
from tools.model_generator.ara.component.component import Component
class ComponentDb(metaclass=SingletonMeta):
    def __init__(self) -> None:
        self.list: dict[str,Component] = {}