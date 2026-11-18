from __future__ import annotations
from tools.model_generator.ara.app.adaptive_application import *
from tools.model_generator.ara.app.adaptive_application_db import AdaptiveApplicationDb
from tools.model_generator.ara.mavlink import mavlink_links as mavlink_links_lib
from tools.model_generator.ara.mavlink.mavlink_app import ValidateApp
class AdaptiveApplicationParser:
    def ParseJson(json_object):
        s_name = list(json_object["adaptive_application"].keys())[0]
        name = ""
        package = json_object["package"]
        if "package" in json_object:
            name = json_object["package"]+"."
        name += s_name
        
        if name in AdaptiveApplicationDb().app_list:
            return
        
        json_object = json_object["adaptive_application"]
        temp_app = AdaptiveApplication(name,AdaptiveApplicationParser.ParseLogger(json_object[s_name]["app"]["logger"]),json_object[s_name]["app"]["functional_groups"])
        if "parms" in json_object[s_name]["app"]:
            temp_app.parms = json_object[s_name]["app"]["parms"]
        if "mavlink" in json_object[s_name]:
            temp_app.mavlink_cfg = mavlink_links_lib.ParseAppConfig(json_object[s_name]["mavlink"], name)
        if "mavlink_links" in json_object[s_name]:
            temp_app.mavlink_links = mavlink_links_lib.ParseLinks(json_object[s_name]["mavlink_links"], name)
        if "provide" in json_object[s_name]:
            for item in json_object[s_name]["provide"]:
               if item["on"] == "ipc" or  item["on"] == "udp":
                    mavlink_links_lib.RejectMavlinkKeys(item, name)
                    t_name:str = item["name"]
                    orginal_name = t_name
                    if "as" in t_name:
                       temp = t_name.split(" as ")
                       orginal_name = temp[0]
                       t_name = temp[1]
                    print (orginal_name+" -> "+t_name)
                    assert t_name not in temp_app.provide_someip
                    port = ""
                    if "port" in item:
                        port = item["port"]
                    temp_app.provide_someip[t_name] = SomeipItem(item["on"],port,item["instance"],SomeIpDb().FindService(orginal_name,package),"kOut")
               
               elif item["on"] == "mavlink":
                    AdaptiveApplicationParser.ParseMavlinkItem(temp_app, temp_app.provide_someip, item, package, name, "kOut")

               elif item["on"] == "diag":
                    t_name:str = item["name"]
                    orginal_name = t_name
                    if "as" in t_name:
                       temp = t_name.split(" as ")
                       orginal_name = temp[0]
                       t_name = temp[1]
                    print (orginal_name+" -> "+t_name)
                    assert t_name not in temp_app.provide_uds
                    temp_app.provide_uds[t_name] = UdsItem(DiagDb().FindJob(orginal_name,package))
                    
               elif item["on"] == "monitor":
                    t_name:str = item["name"]
                    orginal_name = t_name
                    if "as" in t_name:
                       temp = t_name.split(" as ")
                       orginal_name = temp[0]
                       t_name = temp[1]
                    print (orginal_name+" -> "+t_name)
                    assert t_name not in temp_app.provide_dtc
                    temp_app.provide_dtc[t_name] = DtcItem(DiagDb().FindDtc(orginal_name,package))
              
              
        if "require" in json_object[s_name]:
            for item in json_object[s_name]["require"]:
               if item["on"] == "ipc" or  item["on"] == "udp":
                    mavlink_links_lib.RejectMavlinkKeys(item, name)
                    t_name:str = item["name"]
                    orginal_name = t_name
                    if "as" in t_name:
                       temp = t_name.split(" as ")
                       orginal_name = temp[0]
                       t_name = temp[1]
                    print (orginal_name+" -> "+t_name)
                    assert t_name not in temp_app.provide_someip
                    port = ""
                    if "port" in item:
                        port = item["port"]
                    temp_app.require_someip[t_name] = SomeipItem(item["on"],port,item["instance"],SomeIpDb().FindService(orginal_name,package),"kIn")
               
               elif item["on"] == "mavlink":
                    AdaptiveApplicationParser.ParseMavlinkItem(temp_app, temp_app.require_someip, item, package, name, "kIn")

            #    elif item["on"] == "diag":
            #         t_name:str = item["name"]
            #         orginal_name = t_name
            #         if "as" in t_name:
            #            temp = t_name.split(" as ")
            #            orginal_name = temp[0]
            #            t_name = temp[1]
            #         print (orginal_name+" -> "+t_name)
            #         assert t_name not in temp_app.provide_uds
            #         temp_app.provide_uds[t_name] = UdsItem(DiagDb().FindJob(orginal_name,package))
                    
            #    elif item["on"] == "monitor":
            #         t_name:str = item["name"]
            #         orginal_name = t_name
            #         if "as" in t_name:
            #            temp = t_name.split(" as ")
            #            orginal_name = temp[0]
            #            t_name = temp[1]
            #         print (orginal_name+" -> "+t_name)
            #         assert t_name not in temp_app.provide_dtc
            #         temp_app.provide_dtc[t_name] = DtcItem(DiagDb().FindDtc(orginal_name,package))   
   
        ValidateApp(name, temp_app.mavlink_cfg, temp_app.mavlink_links, temp_app.GetMavlinkEntries())
        AdaptiveApplicationDb().app_list[name] = temp_app

    def ParseMavlinkItem(temp_app, target, item, package, app_name, direction):
        t_name:str = item["name"]
        orginal_name = t_name
        if " as " in t_name:
            temp = t_name.split(" as ")
            orginal_name = temp[0]
            t_name = temp[1]
        print (orginal_name+" -> "+t_name)
        if t_name in temp_app.provide_someip or t_name in temp_app.require_someip:
            raise mavlink_links_lib.MavlinkModelError(
                f"{app_name}.{t_name}: powtórzona nazwa wpisu (nazwa po 'as' musi być unikalna "
                "w provide i require aplikacji)")
        service = SomeIpDb().FindService(orginal_name,package)
        mav = mavlink_links_lib.ParseServiceItem(item, t_name, temp_app.mavlink_links, app_name, service, direction)
        # SomeipItem (on="mavlink") pozwala wygenerować klasy proxy/skeleton jak dla ipc/udp.
        someip_item = SomeipItem("mavlink","",item["instance"],service,direction)
        someip_item.mavlink = mav
        target[t_name] = someip_item

    def ParseLogger(json_object) -> LogerConfig:
        logger = LogerConfig(json_object["app_id"],json_object["app_des"],json_object["log_level"],json_object["log_mode"])
        if "ctx" in json_object:
            for item in json_object["ctx"]:
                des = ""
                if "ctx_des" in item:
                    des = item["ctx_des"]
                logger.ctx_list.append(LoggerCtx(item["ctx_id"],item["log_level"],des))
        return logger
    