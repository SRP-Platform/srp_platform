from __future__ import annotations
import sys
import os
import jsonpickle
import copy
import json
from tools.model_generator.ara.common.common_parser import CommonParser
from tools.model_generator.ara.interface.interface_db import InterfaceDb, Interface
from tools.model_generator.ara.deployment.deployment_db import DeploymentDb
from tools.model_generator.ara.component.component_db import ComponentDb, Component
from tools.model_generator.ara.component.component import IpcItem
from tools.model_generator.ara.common.data_structure import Structure
from tools.model_generator.ara.common.data_structure_extractor import DataStructureExtractor
from tools.model_generator.ara.interface.interface_proxy_extractor import InterfaceProxyExtractor
from tools.model_generator.ara.common.data_structure_db import DataStructureDB
COMMON_TYPES = ["/uint8",
                "/uint16",
                "/uint32",
                "/uint64",
                "/int8",
                "/int16",
                "/int32",
                "/int64",
                "/float32",
                "/float64",
                "/bool",
                "/string",
                "/void"]

def CreateDir(start:str,finish:str):
    for p in finish.split("/"):
            start+="/"+p
            try:
                os.makedirs(start)
            except:
                pass


def CheckType(typ,db,src_db):
    if typ not in COMMON_TYPES:
        temp = src_db[typ]
        if temp not in db:
            db.append(temp)
def LoadJson(path:str):
    CommonParser.LoadJson(path)
def CreateDir(start:str,finish:str):
    for p in finish.split("/"):
            start+="/"+p
            try:
                os.makedirs(start)
            except:
                pass
if __name__ == "__main__":
    out_path = sys.argv[1]
    input_path = sys.argv[2]
    db = None
    model: Component = None
    structure_to_gen: list[Structure] = []
    proxy_list: list[Interface] = []
    skeleton_list: list[Interface] = []
    with open(input_path,"r") as file:
         temp = file.read()
         db = jsonpickle.decode(temp)
    for key, obj in db.items():
         if type(obj) == Component:
              model = copy.copy(obj)

    assert model != None

    for key, obj in model.required.items():
        if type(obj) in [IpcItem]:
             proxy_list.append(copy.copy(db[obj.model]))
    for key, obj in model.provide.items():
        if type(obj) in [IpcItem]:
             skeleton_list.append(copy.copy(db[obj.model]))

    for item in proxy_list:
        for key, obj in item.methods.items():
            CheckType("/"+obj.input_type.replace(".","/"),structure_to_gen,db)
            CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
        for key, obj in item.attributes.items():
             CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
        for key, obj in item.broadcasts.items():
             CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
    for item in skeleton_list:
        for key, obj in item.methods.items():
            CheckType("/"+obj.input_type.replace(".","/"),structure_to_gen,db)
            CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
        for key, obj in item.attributes.items():
             CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
        for key, obj in item.broadcasts.items():
             CheckType("/"+obj.output_type.replace(".","/"),structure_to_gen,db)
    

    for struc in structure_to_gen:
        CreateDir(copy.copy(out_path),struc.name.replace(".","/")[0:struc.name.rfind(".")])
        DataStructureExtractor.ExtractStructure(copy.copy(out_path), struc)
    for p_model in proxy_list:
         CreateDir(copy.copy(out_path),p_model.package[1:])
         InterfaceProxyExtractor.ExtractProxy(copy.copy(out_path),p_model)