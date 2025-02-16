from __future__ import annotations

from tools.model_generator.ara.common.singleton_lib import SingletonMeta
from tools.model_generator.ara.deployment.interface import *
class DeploymentDb(metaclass=SingletonMeta):
    def __init__(self):
        self.list = {}
    def InsterModel(self,name:str,model):
        if name not in self.list:
            self.list[name] = model