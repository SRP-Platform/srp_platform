import sys
import os
import json
from tools.model_generator.platform.sm.db import DB
from tools.model_generator.platform.sm.sm_parser import SmParser
from tools.model_generator.platform.sm.config_extractor import ConfigExtractor

out_path = sys.argv[1]
input_file = sys.argv[2]
db_ = DB();
with open(input_file,"r") as input_f:
    json_o = json.load(input_f)
    SmParser.Read(json_o,db_)
    ConfigExtractor.ExtractConfigFile(out_path,db_)
