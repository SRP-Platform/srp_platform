from __future__ import annotations
import sys
import json
import os
from tools.model_generator.ara.someip.lib.someip_parser import SomeIpParser
from tools.model_generator.ara.common.common_parser import CommonParser
from tools.model_generator.ara.common.model_error import ModelError

def LoadJson(path:str):
    CommonParser.LoadJson(path)

if __name__ == "__main__":
    try:
        LoadJson(sys.argv[1])
    except ModelError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
