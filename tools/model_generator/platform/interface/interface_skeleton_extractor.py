from __future__ import annotations
from datetime import date
from tools.model_generator.platform.interface.interface import *
from tools.model_generator.platform.common.data_structure_db import DataStructureDB
class InterfaceSkeletonExtractor:
    def ExtractSkeleton(out_path:str, model:Interface):
        header_guard = model.package.upper().replace("/","_")[1:]+"_"+model.name.upper()+"_INTERFACE_H_"
        namespace_list = model.package[1:].split("/")
        includes = ""
        temp_s = "  ////////////// METHODS SECTION //////////////\n"
        for key, item in model.methods.items():
            var1 = DataStructureDB.ConvertVariable(item.output_type)
            var2 = DataStructureDB.ConvertVariable(item.input_type)
            if var1 is not None:
                for include in var1.GetInclude():
                    if include not in includes:
                        includes+=include+"\n"
                var1= var1.GetTypeOnly()
            else:
                var1 = item.output_type.replace(".","::")
                include = f"#include \"{item.output_type.replace(".","/")}.h\""
                if include not in includes:
                        includes+=include+"\n"
            if var2 is not None:
                for include in var2.GetInclude():
                    if include not in includes:
                        includes+=include+"\n"
                var2= var2.GetTypeOnly()
            else:
                var2 = item.input_type.replace(".","::")
                include = f"#include \"{item.input_type.replace(".","/")}.h\""
                if include not in includes:
                        includes+=include+"\n"
            temp_s+=f"""  using {item.name}T = ::platform::com::skeleton::MethodImpl<{var1}, {var2}>;
  {item.name}T {item.name}{"{"}"{item.name}", *this{"}"};

"""
        temp_s += "  ////////////// EVENTS SECTION //////////////\n"
        for key, item in model.broadcasts.items():
            var1 = DataStructureDB.ConvertVariable(item.output_type)
            if var1 is not None:
                for include in var1.GetInclude():
                    if include not in includes:
                        includes+=include+"\n"
                var1= var1.GetTypeOnly()
            else:
                var1 = item.output_type.replace(".","::")
                include = f"#include \"{item.output_type.replace(".","/")}.h\""
                if include not in includes:
                        includes+=include+"\n"
            temp_s+=f"""  using {item.name}T = ::platform::com::skeleton::Event<{var1}>;
  {item.name}T {item.name}{"{"}"{item.name}", *this{"}"};
  
"""
        res = f"""/**
* @file {model.name.split("/")[-1]}Interface.h
* Data structure by Bartosz Snieg (snieg45@gmail.com)
* @brief 
* @version 0.1
* @date """+str(date.today())+f""""
* 
* @copyright Copyright (c) 2024
* 
* !! Automatically generated file please do not change anything !!
*
*/
#ifndef {header_guard}
#define {header_guard}

#include <utility>
#include <memory>

#include "platform/com/skeleton/event.h"
#include "platform/com/skeleton/impl/method.h"
#include "platform/com/skeleton/skeleton.h"
#include "platform/com/skeleton/skeleton_packet_interpreter.h"
{includes}
"""
        for name in namespace_list:
            res+=f"""namespace {name} {"{"}\n"""
        res+=f"""class {model.name}Interface : public ::platform::com::skeleton::interpreter::SkeletonPacketInterpreter {"{"}\n"""
        res+=f""" public:
  explicit {model.name}Interface(
      const ::platform::com::InstanceIdentifierContainer container,
      std::shared_ptr<::platform::com::skeleton::wrapper::ISkeletonComWrapper> com_wrapper)
      : ::platform::com::skeleton::interpreter::
            SkeletonPacketInterpreter(container, com_wrapper)  {{}}

  ~{model.name}Interface() noexcept = default;
  explicit {model.name}Interface({model.name}Interface&&) noexcept = delete;
  explicit {model.name}Interface({model.name}Interface&) noexcept = delete;
  {model.name}Interface operator=({model.name}Interface&) noexcept = delete;
  {model.name}Interface operator=({model.name}Interface&&) noexcept = delete;

"""
        res+=temp_s

        res+="};\n"
        res+=f"""
 using {model.name}Skeleton = ::platform::com::skeleton::Skeleton<{model.name}Interface>;
"""
        for name in namespace_list[::-1]:
            res+=f"""{"}"}  // namespace {name}\n"""
        res+= f"""#endif  // {header_guard}
"""
        with open(out_path+model.package+"/"+model.name+".h","w") as file:
            file.write(res)