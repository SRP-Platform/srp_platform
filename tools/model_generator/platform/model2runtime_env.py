from __future__ import annotations
import sys
import os
import jsonpickle
import copy
from datetime import date
from tools.model_generator.platform.common.common_parser import CommonParser
from tools.model_generator.platform.interface.interface_db import InterfaceDb, Interface
from tools.model_generator.platform.deployment.interface import *
from tools.model_generator.platform.component.component_db import ComponentDb, Component
from tools.model_generator.platform.component.component import IpcItem, DiagItem
from tools.model_generator.platform.common.data_structure import Structure
from tools.model_generator.platform.common.data_structure_extractor import DataStructureExtractor
from tools.model_generator.platform.common.data_structure_db import DataStructureDB
from tools.model_generator.platform.diag.diag_jobs import ServiceType

OUTPUT_FILE_START = f"""/**
 * @file initialization.cc
 * platform Env Generator by Bartosz Snieg (snieg45@gmail.com)
 * @brief 
 * @version 0.1
 * @date """+str(date.today())+""""
 * 
 * @copyright Copyright (c) 2024
 * 
 * !! Automatically generated file please do not change anything !!
 *
 */
#include "platform/com/initialization.h"

#include <stop_token>

#include "platform/core/model/com_model_item.h"
#include "platform/core/model/diag_model_item.h"
#include "platform/core/model_db.h"
#include "platform/log/logging_menager.h"

namespace platform {
namespace core {
namespace {
    std::stop_source platform_stop_source_{};
}
Result<void> InitializeDb(ModelDataBase& db_) noexcept;
Result<void> InitializeLogger() noexcept;
void InitCom() noexcept;
Result<void> Initialize() noexcept {
  std::ignore = InitializeLogger();
  const auto platform_logger_ = log::LoggingMenager::GetInstance()->CreateLogger("core");
  platform_logger_.LogInfo() << "Logger initialized";
  {
    auto& db_ = ModelDataBase::GetInstance();
    platform_logger_.LogInfo() << "Instances of the model database have been created";
    std::ignore = InitializeDb(db_);
    platform_logger_.LogInfo() << "Models added";
  }
  
  platform_logger_.LogInfo() << "platform environment has been initialized";
  InitCom();
  platform_logger_.LogInfo() << "platform::COM environment has been initialized";
  return {};
}

Result<void> Deinitialize() noexcept {
  platform_stop_source_.request_stop();
  const auto platform_logger_ = log::LoggingMenager::GetInstance()->CreateLogger("core");
  platform_logger_.LogInfo() << "platform environment has been deinitialized";
  
  return {}; 
}

"""

OUTPUT_FILE_END = """}  // namespace core
}  // namespace platform
"""


def CreateLogerInit(model:Component)->str:
        res = """
Result<void> InitializeLogger() noexcept {
    log::LoggingMenager::Create(\""""+model.logger.GetAppId()+"""\","""+model.logger.GetLogMode()+""","""+model.logger.GetLogLevel()+""");
"""
        have_platform = False
        for ctx in model.logger.ctx_list:
            if ctx.GetCtxId() == "platform-":
                have_platform = True
            res+="""    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger(\""""+ctx.GetCtxId()+"""\", \""""+ctx.ctx_des+"""\", """+ctx.GetLogLevel()+""");
"""
        if not have_platform:
            res +="""
    std::ignore = log::LoggingMenager::GetInstance()->CreateLogger("core", "Default ctx for platform", log::LogLevel::kInfo);
"""          
        
        res +="""
    return {};
}
"""
        return res

def CreateInterfaceDepl(key, item: InterfaceDepl,sec) -> str:
    res = "    {\n"
    res += f"""      const platform::com::model::ServiceModel service_model{"{"}{hex(item.common_conf.ServiceId).upper().replace("X","x")}U,{hex(sec.instance).upper().replace("X","x")}U{"}"};
"""
    res += """      const platform::com::InstanceIdentifierContainer container{service_model,{\n"""
    typ = "",
    if type(sec) == IpcItem:
         typ = "platform::core::model::ModelCom::ComType::kIPC"
    for k,i in item.EndpointList.items():
         res+=f"""          {"{"}\"{k}\",platform::com::model::EndpointModel{"{"}{hex(i.EndpointId).upper().replace("X","x")}{"}},"}\n"""
    if res[-2:] == ',\n':
         res = res[:-2]+"\n"
    res += "      }};\n"
    res += f"""      std::ignore = db_.AddNewItem("{key}",platform::core::model::ModelCom(container,{typ}));\n"""
    res +="    }\n"
    return res
def CreatInitCom(model: Component) -> str:
    #  ;
     return """
void InitCom() noexcept {
    platform::com::Initialize("""+hex(model.id).upper().replace("X","x")+""");
}
"""
def CreateDiagDepl(key, item: DiagItem, model: DiagJobDepl, service: ServiceType):

    address = (service.service_id<<8*service.max_sub_id)+model.subs_service_id
    print((service.service_id<<8*service.max_sub_id))
    print(service.service_id)
    print(model.subs_service_id)
    res = f"""    std::ignore = db_.AddNewItem("{key}",platform::core::model::ModelUds({hex(service.service_id)},{hex(model.subs_service_id)},\"{address}\", platform::core::model::ModelUds::Direction::kOut));\n"""
    return res


def CreateDbInit(model:Component,items)->str:
    res = """
Result<void> InitializeDb(ModelDataBase& db_) noexcept {

"""
    for key, itemd in items.items():
        item = itemd[0]
        depl = itemd[1]
        print(depl)
        if type(depl) == InterfaceDepl:
            res+=CreateInterfaceDepl(key,depl,item)+"\n"
        elif type(depl) == DiagJobDepl:
            res+=CreateDiagDepl(key,item,depl,itemd[2])+"\n"
    res += """    return {};
}
"""
    return res

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
        elif type(item) == DiagItem:
             items[key] = [item, db[item.model], db[db[item.model].service_type]]
    with open(out_path+"/initialization.cc","w") as out_file:
            out_file.write(OUTPUT_FILE_START+CreateLogerInit(model)+CreateDbInit(model,items)+CreatInitCom(model)+OUTPUT_FILE_END)