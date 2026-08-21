#pragma once

#include "ast.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>


struct TypeSubstitution {
    std::unordered_map<std::string, BType> mapping;
    std::unordered_map<std::string, TypeRef> type_ref_mapping;
    
    bool has(const std::string& name) const {
        return mapping.find(name) != mapping.end();
    }
    
    BType get(const std::string& name) const {
        auto it = mapping.find(name);
        if (it != mapping.end()) return it->second;
        return BType::UNKNOWN;
    }

    TypeRef get_type_ref(const std::string& name) const {
        auto ref = type_ref_mapping.find(name);
        if (ref != type_ref_mapping.end()) return ref->second;

        TypeRef result;
        result.base = get(name);
        return result;
    }
};




inline std::string mangle_template_name(const std::string& fn_name, const std::vector<BType>& type_args) {
    std::string result = fn_name;
    for (BType t : type_args) {
        std::string type_str = type_name(t);
        
        if (type_str.size() >= 2 && type_str.substr(type_str.size() - 2) == "[]") {
            type_str = type_str.substr(0, type_str.size() - 2) + "_arr";
        }
        
        if (!type_str.empty() && type_str.back() == '*') {
            type_str.pop_back();
            type_str += "_ptr";
        }
        
        size_t pos = 0;
        while ((pos = type_str.find('*', pos)) != std::string::npos) {
            type_str.replace(pos, 1, "_ptr");
            pos += 4; 
        }
        result += "__" + type_str;
    }
    return result;
}

inline std::string mangle_template_name(const std::string& fn_name,
                                        const std::vector<TypeRef>& type_args) {
    std::string result = fn_name;
    for (const TypeRef& type_arg : type_args) {
        std::string type_str = mangle_type_ref(type_arg);
        size_t pos = 0;
        while ((pos = type_str.find("[]", pos)) != std::string::npos) {
            type_str.replace(pos, 2, "_arr");
            pos += 4;
        }
        pos = 0;
        while ((pos = type_str.find('*', pos)) != std::string::npos) {
            type_str.replace(pos, 1, "_ptr");
            pos += 4;
        }
        result += "__" + type_str;
    }
    return result;
}




std::unique_ptr<FnDecl> clone_and_substitute(
    const FnDecl& original,
    const TypeSubstitution& subst,
    const std::string& new_name
);




std::unique_ptr<Expr> clone_expression(const Expr& original);



std::unique_ptr<Stmt> clone_statement(const Stmt& original);


class TemplateRegistry {
public:
    
    void register_template(const FnDecl& fn);
    
    
    bool is_template(const std::string& name) const;
    
    
    const std::vector<std::string>* get_type_params(const std::string& name) const;
    
    
    
    FnDecl* instantiate(const std::string& fn_name, const std::vector<BType>& type_args);
    FnDecl* instantiate(const std::string& fn_name, const std::vector<TypeRef>& type_args);
    
    
    std::vector<FnDecl*> get_instantiations() const;
    
    
    std::vector<std::unique_ptr<FnDecl>> take_instantiations();
    
    
    
    bool infer_type_args(const std::string& fn_name, 
                         const std::vector<BType>& arg_types,
                         std::vector<BType>& out_type_args) const;

private:
    
    std::unordered_map<std::string, const FnDecl*> templates;
    
    
    std::unordered_map<std::string, std::vector<std::string>> template_type_params;
    
    
    std::unordered_map<std::string, std::unique_ptr<FnDecl>> instantiations;
};
