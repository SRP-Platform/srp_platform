from __future__ import annotations
from tools.model_generator.ara.someip.lib.someip_db import *
from tools.model_generator.ara.common.data_structure import *

class SomeIPGenerator:
    def CreateDataStructure(main_path:str, structure: data_type):
        dir_list = structure.name.split(".")
        print(dir)