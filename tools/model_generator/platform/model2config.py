from __future__ import annotations
import sys
import json
import jsonpickle
import copy
from datetime import date
from tools.model_generator.platform.common.common_parser import CommonParser
from tools.model_generator.platform.interface.interface_db import InterfaceDb, Interface
from tools.model_generator.platform.deployment.interface import *
from tools.model_generator.platform.component.component_db import ComponentDb, Component
from tools.model_generator.platform.component.component import IpcItem
from tools.model_generator.platform.common.data_structure import Structure
from tools.model_generator.platform.common.data_structure_extractor import DataStructureExtractor
from tools.model_generator.platform.common.data_structure_db import DataStructureDB


def CreateLoggerJson(app: Component):
        out_json = {}
        
        out_json["app_id"] = app.logger.app_id
        out_json["app_des"] = app.logger.app_des
        out_json["log_level"] = app.logger.log_level
        out_json["log_mode"] = app.logger.log_mode
        
        return out_json
def CreateSrpAppJson(app: Component):
        out_json = {}
        out_json["app_name"] = app.name
        out_json["app_id"] = app.id
        out_json["bin_path"] = f"/srp/opt/{app.name.split("/")[-1]}/bin/{app.name.split("/")[-1]}"
        out_json["parms"] = app.parms
        out_json["fg_list"] = app.functional_groups
        return out_json

if __name__ == "__main__":
    out_path = sys.argv[1]
    input_path = sys.argv[2]
    db = None
    model: Component = None

    items: dict[str,None] = {}

    with open(input_path,"r") as file:
         temp = file.read()
         db = jsonpickle.decode(temp)
    for key, obj in db.items():
         if type(obj) == Component:
              model = copy.copy(obj)
    assert model != None
    for key, item in model.required.items():
         if type(item) == IpcItem:
            items[key] = [item ,db[item.depl]]
    for key, item in model.provide.items():
         if type(item) == IpcItem:
            items[key] = [item ,db[item.depl]]
    with open(out_path+"/logger.json","w") as out_file:
        json.dump(CreateLoggerJson(model),out_file,indent=2)
    with open(out_path+"/srp_app_config.json","w") as out_file:
        json.dump(CreateSrpAppJson(model),out_file,indent=2)