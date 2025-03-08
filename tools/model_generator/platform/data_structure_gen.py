from __future__ import annotations
import sys
import os
import copy
import json
from tools.model_generator.platform.common.common_parser import CommonParser
from tools.model_generator.platform.common.data_structure_extractor import *
from tools.model_generator.platform.common.data_structure import data_type
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
def ExtractStructList(struct_list: dict[str,data_type],s:data_type):
    struct_list[s.name] = s
    for key,d in s.variable_list.items():
        if d.typ_str == "struct":
            ExtractStructList(struct_list,d)
if __name__ == "__main__":
    out_path = sys.argv[1]
    for arg_id in range(2,len(sys.argv)):
        LoadJson(sys.argv[arg_id])
    db_ = DataStructureDB()
    for name,v in db_.data_structure.items():
        CreateDir(copy.copy(out_path),name.replace(".","/").replace("/"+name.split(".")[-1], ""))
        DataStructureExtractor.ExtractStructure(out_path,v)
