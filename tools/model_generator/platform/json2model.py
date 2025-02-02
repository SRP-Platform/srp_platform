from __future__ import annotations
import sys
import os
import jsonpickle
import copy
import json
from tools.model_generator.platform.common.common_parser import CommonParser
from tools.model_generator.platform.interface.interface_db import InterfaceDb
from tools.model_generator.platform.deployment.deployment_db import DeploymentDb
from tools.model_generator.platform.component.component_db import ComponentDb
from tools.model_generator.platform.common.data_structure_db import DataStructureDB
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
    out_path = sys.argv[2]
    component_name = sys.argv[1]
    src_path = ""
    for val in sys.argv:
        if ".json" in val:
            if os.path.isfile(val):
                LoadJson(val)

    out_json = copy.copy(InterfaceDb().interfaces)
    out_json.update(DataStructureDB().data_structure)
    out_json.update(DeploymentDb().list)
    if component_name == "/NONE":
        component_name = list(ComponentDb().list.keys())[0]
    out_json[component_name] = (ComponentDb().list[component_name])
    assert len(out_json) == (len(InterfaceDb().interfaces)+len(DeploymentDb().list)+1+len(DataStructureDB().data_structure))
    json_string = jsonpickle.encode(out_json)
    json_t = json.loads(json_string)
    with open(out_path,"w") as file:
        s = json.dumps(json_t,indent = 2)
        file.write(s)

