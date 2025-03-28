from __future__ import annotations
from datetime import date
from tools.model_generator.platform.common.data_structure import *

class DataStructureExtractor:
    
    def ExtractStructure(out_path:str, struct:data_type):
        if struct.typ_str == "struct":
            with open(out_path+"/"+struct.name.replace(".","/")+".h","w") as out_file:
                namespace_list = struct.name.split(".")
                file = f"""/**
* @file {struct.name.replace(".","/").split("/")[-1]}.h
* Data structure by Bartosz Snieg (snieg45@gmail.com)
* @brief 
* @version 0.1
* @date """+str(date.today())+""""
* 
* @copyright Copyright (c) 2024
* 
* !! Automatically generated file please do not change anything !!
*
*/
"""
                file += """#ifndef """+(struct.name.upper().replace(".","_"))+"_H_\n"
                file += """#define """+(struct.name.upper().replace(".","_"))+"_H_\n\n"
                file+="""#include <vector>\n"""
                file+="""#include <bit>\n"""
                file+="""#include "platform/core/result.h"\n"""
                file+="""#include "platform/com/com_error_domain.h"\n"""
                file+="""#include "core/data/type_converter.h"\n"""
                file+="""#include <optional>\n"""
                
                for key,var in struct.variable_list.items():
                    if var.typ_str == "struct":
                        if f"""#include "{var.name.replace(".","/")}.h"\n""" not in file:
                            file+=f"""#include "{var.name.replace(".","/")}.h"\n"""
                    else:
                        for i in var.GetInclude():
                            if i not in file:
                                file+=f"""{i}\n"""
                file+="\n"
                for i in range(len(namespace_list)-1):
                    file+="""namespace """+namespace_list[i]+""" {\n"""
                file +="""\nstruct """+namespace_list[-1]+"""{\n"""
                
                for key,var in struct.variable_list.items():
                    if var.typ_str == "struct":
                        file +="""  """+var.name.replace(".","::")+""" """+key+""";\n"""
                    else:
                        file +="""  """+var.typ_str+""" """+key+""";\n"""
                file+="};\n\n"
                for i in range(len(namespace_list)-1):
                    file+="""}  // namespace """+namespace_list[i]+"""\n"""
                    
                file+="namespace srp {\n"
                file+="namespace data {\n"
                file+="""
template <>
struct Convert<"""+struct.name.replace(".","::")+"""> {
  static std::optional<"""+struct.name.replace(".","::")+"""> Conv(
      const std::vector<std::uint8_t>& in) {
    """+struct.name.replace(".","::")+""" res {};\n"""
                file+="    if(in.size() < "+str(struct.GetSize())+") {\n"
                file+="      return std::nullopt;\n"
                file+="    }\n"
                index = 0
                for k,v in struct.variable_list.items():
                    file+="    {\n"
                    if v.typ_str == "struct":
                        file+="      const auto tem_v = srp::data::Convert<"+v.name.replace(".","::")+">::Conv(std::vector<uint8_t>{in.begin()+"+str(index)+", in.begin()+"
                    else:
                        file+="      const auto tem_v = srp::data::Convert<"+v.typ_str+">::Conv(std::vector<uint8_t>{in.begin()+"+str(index)+", in.begin()+"
                    file+=str(index+v.GetSize())+"});\n"
                    file+="      if(!tem_v.has_value()) {\n"
                    file+="        return std::nullopt;\n"
                    file+="      }\n"
                    if v.typ_str not in ["std::uint8_t","std::int8_t","bool"]: 
                        file+="      if constexpr (std::endian::native == std::endian::"+struct.endian+") {\n"
                        file+="        res."+k+" = tem_v.value();\n"
                        file+="      } else {\n"
                        if v.typ_str == "struct":
                            file+="        res."+k+" = srp::data::EndianConvert<"+v.name.replace(".","::")+">::Conv(tem_v.value());\n"
                        else:
                            file+="        res."+k+" = srp::data::EndianConvert<"+v.typ_str+">::Conv(tem_v.value());\n"
                        file+="      }\n"
                    else:
                        file+="        res."+k+" =tem_v.value();\n"
                    file+="    }\n"
                    index+=v.GetSize()
                file+="""
    return res;
  }
};
template <>
struct Convert2Vector<"""+struct.name.replace(".","::")+"""> {
  static std::vector<uint8_t> Conv(const """+struct.name.replace(".","::")+"""& in) {
      std::vector<uint8_t> out{};
"""
                for key,var in struct.variable_list.items():
                    if var.typ_str not in ["std::uint8_t","std::int8_t","bool"]: 
                        file+="""      if constexpr (std::endian::native == std::endian::"""+struct.endian+""") {\n"""
                        if var.typ_str == "struct":
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.name.replace(".","::")+""">::Conv(in."""+key+""");\n"""
                        else:
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.typ_str+""">::Conv(in."""+key+""");\n"""
                        file+="""        out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());\n"""
                        file+="""      } else {\n"""
                        if var.typ_str == "struct":
                            file+="          const auto temp_v = srp::data::EndianConvert<"+var.name.replace(".","::")+">::Conv(in."+key+");\n"
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.name.replace(".","::")+""">::Conv(temp_v);\n"""
                        else:
                            file+="        const auto temp_v = srp::data::EndianConvert<"+var.typ_str+">::Conv(in."+key+");\n"
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.typ_str+""">::Conv(temp_v);\n"""

                        file+="""        out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());\n"""
                        file+="""      }\n"""
                    else:
                        file+="""      {\n"""
                        if var.typ_str == "struct":
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.name.replace(".","::")+""">::Conv(in."""+key+""");\n"""
                        else:
                            file+="""        const auto temp_r_v = srp::data::Convert2Vector<"""+var.typ_str+""">::Conv(in."""+key+""");\n"""
                        file+="""        out.insert(out.end(), temp_r_v.begin(), temp_r_v.end());\n"""
                        file+="""      }\n"""
                file+="""
    return out;
  }
};\n"""
                file+="}  // namespace data\n"
                file+="}  // namespace srp\n"
                file += """\n#endif  // """+(struct.name.upper().replace(".","_"))+"_H_\n"
                out_file.write(file)