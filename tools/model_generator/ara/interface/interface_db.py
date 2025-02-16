from __future__ import annotations

from tools.model_generator.ara.interface.interface import *
from tools.model_generator.ara.common.singleton_lib import SingletonMeta

class InterfaceDb(metaclass=SingletonMeta):
    def __init__(self):
        self.interfaces:dict[str,Interface] = {}
    def AddInterface(self, interface:Interface):
        self.interfaces[interface.package+"/"+interface.name] = interface