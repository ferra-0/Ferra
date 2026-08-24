#pragma once

#include "ast.h"
#include "template_instantiator.h"
#include <string>
#include <sstream>
#include <unordered_map>
#include <unordered_set>


enum class IRType {
    I1,      
    I8,      
    I16,     
    I32,     
    I64,     
    F32,     
    F64,     
    I8_PTR,  
    I16_PTR, 
    I32_PTR, 
    I64_PTR, 
    F32_PTR, 
    F64_PTR, 
    PTR,     
    VOID,    
    ARR,     
    STRUCT,  
    UNKNOWN
};

inline std::string llvm_type_str(IRType t) {
    switch (t) {
        case IRType::I1: return "i1";
        case IRType::I8: return "i8";
        case IRType::I16: return "i16";
        case IRType::I32: return "i32";
        case IRType::I64: return "i64";
        case IRType::F32: return "float";
        case IRType::F64: return "double";
        case IRType::I8_PTR: return "i8*";
        case IRType::I16_PTR: return "i16*";
        case IRType::I32_PTR: return "i32*";
        case IRType::I64_PTR: return "i64*";
        case IRType::F32_PTR: return "float*";
        case IRType::F64_PTR: return "double*";
        case IRType::PTR: return "ptr";
        case IRType::VOID: return "void";
        case IRType::ARR: return "i64*";  
        case IRType::STRUCT: return "%struct";  
        default: return "i64";
    }
}

inline std::string llvm_ptr_type_str(IRType t) {
    if (t == IRType::PTR) return "ptr";
    return llvm_type_str(t) + "*";
}

inline IRType btype_to_ir(BType t) {
    switch (t) {
        case BType::INT: return IRType::I64;
        case BType::F64: return IRType::F64;
        case BType::BOOL: return IRType::I1;
        case BType::STR: return IRType::I8_PTR;
        
        
        
        case BType::FUNC: return IRType::I64_PTR;
        case BType::PTR: return IRType::I64_PTR;  
        
        
        
        case BType::VOID: return IRType::I8_PTR;
        case BType::ARR: return IRType::ARR;
        case BType::STRUCT:
        case BType::TUPLE: return IRType::STRUCT;
        case BType::INT_ARR: return IRType::ARR;
        case BType::F64_ARR: return IRType::ARR;
        case BType::BOOL_ARR: return IRType::ARR;
        case BType::STR_ARR: return IRType::ARR;
        case BType::PTR_ARR: return IRType::ARR;
        
        case BType::I8: return IRType::I8;
        case BType::I16: return IRType::I16;
        case BType::I32: return IRType::I32;
        case BType::I64: return IRType::I64;
        
        case BType::U8: return IRType::I8;
        case BType::U16: return IRType::I16;
        case BType::U32: return IRType::I32;
        case BType::U64: return IRType::I64;
        case BType::HEX: return IRType::I64;
        case BType::ISIZE:
        case BType::USIZE:
            return sizeof(void*) == 8 ? IRType::I64 : IRType::I32;
        
        case BType::F32: return IRType::F32;
        
        case BType::I8_PTR: return IRType::I8_PTR;
        case BType::I16_PTR: return IRType::I16_PTR;
        case BType::I32_PTR: return IRType::I32_PTR;
        case BType::I64_PTR: return IRType::I64_PTR;
        case BType::U8_PTR: return IRType::I8_PTR;
        case BType::U16_PTR: return IRType::I16_PTR;
        case BType::U32_PTR: return IRType::I32_PTR;
        case BType::U64_PTR: return IRType::I64_PTR;
        case BType::F32_PTR: return IRType::F32_PTR;
        case BType::F64_PTR: return IRType::F64_PTR;
        case BType::STR_PTR: return IRType::I8_PTR;
        default: return IRType::UNKNOWN;
    }
}

inline BType ir_to_btype(IRType t) {
    switch (t) {
        case IRType::I64: return BType::INT;
        case IRType::F64: return BType::F64;
        case IRType::I1: return BType::BOOL;
        case IRType::I8_PTR: return BType::STR;
        case IRType::I16_PTR: return BType::I16_PTR;
        case IRType::I32_PTR: return BType::I32_PTR;
        case IRType::I64_PTR: return BType::I64_PTR;
        case IRType::F32_PTR: return BType::F32_PTR;
        case IRType::F64_PTR: return BType::F64_PTR;
        case IRType::VOID: return BType::VOID;
        case IRType::ARR: return BType::ARR;
        case IRType::STRUCT: return BType::STRUCT;
        
        case IRType::I8: return BType::I8;
        case IRType::I16: return BType::I16;
        case IRType::I32: return BType::I32;
        
        case IRType::F32: return BType::F32;
        default: return BType::UNKNOWN;
    }
}


inline int getTypeSize(BType t) {
    switch (t) {
        case BType::VOID: return static_cast<int>(sizeof(void*));
        case BType::INT: return 8;
        case BType::F64: return 8;
        case BType::BOOL: return 1;
        case BType::STR: return static_cast<int>(sizeof(void*));
        case BType::FUNC: return static_cast<int>(sizeof(void*));
        case BType::PTR: return static_cast<int>(sizeof(void*));
        case BType::ARR: return static_cast<int>(sizeof(void*));
        case BType::STRUCT: return 0;  
        case BType::INT_ARR: return static_cast<int>(sizeof(void*));
        case BType::F64_ARR: return static_cast<int>(sizeof(void*));
        case BType::BOOL_ARR: return static_cast<int>(sizeof(void*));
        case BType::STR_ARR: return static_cast<int>(sizeof(void*));
        case BType::PTR_ARR: return static_cast<int>(sizeof(void*));
        
        case BType::I8: return 1;
        case BType::I16: return 2;
        case BType::I32: return 4;
        case BType::I64: return 8;
        
        case BType::U8: return 1;
        case BType::U16: return 2;
        case BType::U32: return 4;
        case BType::U64: return 8;
        case BType::HEX: return 8;
        case BType::ISIZE:
        case BType::USIZE:
            return static_cast<int>(sizeof(void*));
        
        case BType::F32: return 4;
        
        case BType::I8_PTR: return static_cast<int>(sizeof(void*));
        case BType::I16_PTR: return static_cast<int>(sizeof(void*));
        case BType::I32_PTR: return static_cast<int>(sizeof(void*));
        case BType::I64_PTR: return static_cast<int>(sizeof(void*));
        case BType::U8_PTR: return static_cast<int>(sizeof(void*));
        case BType::U16_PTR: return static_cast<int>(sizeof(void*));
        case BType::U32_PTR: return static_cast<int>(sizeof(void*));
        case BType::U64_PTR: return static_cast<int>(sizeof(void*));
        case BType::F32_PTR: return static_cast<int>(sizeof(void*));
        case BType::F64_PTR: return static_cast<int>(sizeof(void*));
        case BType::STR_PTR: return static_cast<int>(sizeof(void*));
        
        case BType::I8_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::I16_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::I32_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::I64_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::U8_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::U16_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::U32_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::U64_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::F32_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::F64_PTR_ARR: return static_cast<int>(sizeof(void*));
        case BType::STR_PTR_ARR: return static_cast<int>(sizeof(void*));
        default: return 0;
    }
}


struct LLVMVar {
    std::string name;
    std::string alloca;     
    IRType type;
    BType elem_type;        
    int array_size;         
    std::string struct_name; 
    bool struct_pointer_slot = false;
    bool used = false;
    bool owns_struct_pointer = false;
    std::string drop_enabled_alloca;
    bool is_const = false; 
    BType source_type = BType::UNKNOWN; 
    bool is_function_pointer = false;
    bool function_signature_known = false;
    IRType function_return_type = IRType::UNKNOWN;
    std::vector<IRType> function_argument_types;
    bool inline_struct_array = false;
};

struct LLVMStructInfo {
    std::string name;
    std::string template_name;  
    bool is_opaque = false;
    bool is_tuple = false;
    std::vector<TypeRef> template_args;
    std::vector<TypeRef> tuple_element_types;
    std::vector<BType> field_types;
    std::vector<std::string> field_names;
    std::vector<std::string> field_annotations;  
    std::vector<bool> field_inline_struct_arrays;
    std::unordered_map<std::string, size_t> field_indices;
};

struct LLVMLoopTarget {
    std::string break_label;
    std::string continue_label;
    size_t cleanup_depth = 0;
};

class LLVMEmitter {
public:
    std::ostringstream out;
    std::ostringstream globals;
    std::ostringstream body;
    std::ostringstream anonymous_functions;
    std::ostringstream struct_defs;  
    
    int ssa_counter = 0;
    int label_counter = 0;
    int str_counter = 0;
    int anonymous_counter = 0;
    std::unordered_map<std::string, LLVMVar> vars;
    std::unordered_map<std::string, IRType> func_types;
    std::unordered_map<std::string, BType> func_return_btypes;
    std::unordered_map<std::string, std::vector<bool>> func_param_by_value;
    std::unordered_map<std::string, std::vector<std::string>>
        func_param_struct_names;
    std::unordered_map<std::string, std::vector<IRType>> func_arg_types;
    std::unordered_map<std::string, const FnDecl*> extern_functions;
    std::unordered_set<std::string> variadic_functions;
    std::unordered_map<std::string, std::vector<std::string>>
        extern_variadic_fixed_types;
    std::unordered_map<std::string, IRType> global_vars; 
    std::unordered_map<std::string, BType> global_btypes; 
    std::unordered_set<std::string> global_consts; 
    std::unordered_map<std::string, LLVMStructInfo> structs; 
    std::unordered_map<std::string, const StructDecl*> struct_templates;
    std::unordered_map<const Expr*, TypeRef> expected_tuple_types;
    std::unordered_set<std::string> inline_struct_array_types;
    std::unordered_map<std::string, std::string> drop_functions; 
    std::vector<LLVMLoopTarget> loop_targets;
    std::vector<std::vector<std::string>> cleanup_scopes;
    TemplateRegistry template_registry;  
    BType current_fn_return_type = BType::UNKNOWN;  
    
    std::string next_ssa() { return "%t" + std::to_string(ssa_counter++); }
    std::string next_local_alloca(const std::string& name) {
        return "%" + name + "_ptr." + std::to_string(ssa_counter++);
    }
    std::string next_label(const std::string& base) { return base + std::to_string(label_counter++); }
    std::string next_str_name() { return "@.str." + std::to_string(str_counter++); }
    
    
    void emit_program(const Program& prog);
    void emit_function(const FnDecl& fn);
    void emit_statement(const Stmt* stmt, int indent = 1);
    void emit_cleanup_from(size_t first_scope, const std::string& pad);
    std::string emit_expression(const Expr* expr);
    bool emit_struct_constructor(const VarDeclStmt& var,
                                 const std::string& struct_name);
    
    
    std::string emit_lvalue(const Expr* expr);
    
    
    void emit_string_literal(const std::string& s);
    std::string emit_array_literal(const ArrayExpr* array, BType element_type);
    std::string get_llvm_type(BType t) {
        if (is_array_type(t)) {
            BType elem_type = get_array_elem_type(t);
            if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
            return llvm_type_str(btype_to_ir(elem_type)) + "*";
        }
        return llvm_type_str(btype_to_ir(t));
    }
    
    
    std::string get_struct_type_str(const std::string& struct_name);
    std::string resolve_tuple_type(const TypeRef& type_ref);
    
    
    BType get_expr_type(const Expr* expr);

    
    std::string get_expr_struct_name(const Expr* expr);

    
    
    bool resolve_call_target(const CallExpr* call, std::string& callee_name,
                             bool report_errors = true);

    
    
    BType infer_operator_return_type(FnDecl& fn);

    
    
    std::string resolve_struct_type(const TypeRef& type_ref);
    
    
    void collect_structs(const Program& prog);
    
    
    void emit_struct_defs();
};




std::string generate_llvm_ir(Program& prog);
