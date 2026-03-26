from __future__ import annotations
from tools.model_generator.platform.ecu.ecu_db import EcuDb, Ecu, NetworkItem
from datetime import datetime


class NetworkExtractor:
    def ExtractNetworkConfigSh(ecu:Ecu) -> str:
        res = f"""
#!/bin/sh
################################################################################
#
#   Copyright (c) {datetime.today().year} Bartosz Snieg.
#
################################################################################
#
"""
        items = NetworkExtractor._ExtractNetworkInterfaces(ecu.transport_layer)
        print(items)

        return res
    def _ExtractNetworkInterfaces(list):
        res = {}
        for key,item in list.items():
            if type(item) == NetworkItem:
                res[key.split("/")[-1]] = item
        return res