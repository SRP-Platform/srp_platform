from __future__ import annotations

class Method:
    def __init__(self, name:str, input_type:str, output_type:str):
        self.name = name
        self.input_type = input_type
        self.output_type = output_type

class Broadcast:
    def __init__(self,name:str, output_type:str):
        self.name = name
        self.output_type = output_type

class Attribute(Broadcast):
    def __init__(self, name, output_type, readonly:bool):
        super().__init__(name, output_type)
        self.readonly = readonly

class Interface:
    def __init__(self, name:str, package:str, major_version:int, minor_version:int,
                 methods:dict[str,Method], broadcasts:dict[str,Broadcast], 
                 attributes:dict[str,Attribute]):
        self.name = name
        self.package = package
        self.major_version = major_version
        self.minor_version = minor_version
        self.methods = methods
        self.broadcasts = broadcasts
        self.attributes = attributes
