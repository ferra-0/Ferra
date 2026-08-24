#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "tokens.h"


enum class BType {
    UNKNOWN, VOID, INT, F64, BOOL, STR, PTR, ARR, OBJ, FUNC, STRUCT, TUPLE,
    INT_ARR, F64_ARR, BOOL_ARR, STR_ARR, PTR_ARR,  
    
    I8, I16, I32, I64,
    
    U8, U16, U32, U64,
    
    F32,
    
    I8_PTR, I16_PTR, I32_PTR, I64_PTR,
    U8_PTR, U16_PTR, U32_PTR, U64_PTR,
    F32_PTR, F64_PTR, STR_PTR,
    
    I8_PTR_ARR, I16_PTR_ARR, I32_PTR_ARR, I64_PTR_ARR,
    U8_PTR_ARR, U16_PTR_ARR, U32_PTR_ARR, U64_PTR_ARR,
    F32_PTR_ARR, F64_PTR_ARR, STR_PTR_ARR,
    
    I8_ARR, I16_ARR, I32_ARR, I64_ARR,
    U8_ARR, U16_ARR, U32_ARR, U64_ARR,
    F32_ARR,
    
    ISIZE, USIZE,

    
    HEX
};

inline const std::string& type_name(BType t) {
    static const std::string names[] = {
        "any", "nul", "int", "f64", "bol", "str", "ptr", "arr", "obj", "func", "stct", "tup",
        "int[]", "f64[]", "bol[]", "str[]", "ptr[]",
        "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64",
        "f32",
        "i8*", "i16*", "i32*", "i64*",
        "u8*", "u16*", "u32*", "u64*",
        "f32*", "f64*", "str*",
        "i8*[]", "i16*[]", "i32*[]", "i64*[]",
        "u8*[]", "u16*[]", "u32*[]", "u64*[]",
        "f32*[]", "f64*[]", "str*[]",
        "i8[]", "i16[]", "i32[]", "i64[]",
        "u8[]", "u16[]", "u32[]", "u64[]",
        "f32[]",
        "isize", "usize", "hex"
    };
    return names[static_cast<int>(t)];
}


inline BType get_array_elem_type(BType t) {
    switch (t) {
        case BType::INT_ARR: return BType::INT;
        case BType::F64_ARR: return BType::F64;
        case BType::BOOL_ARR: return BType::BOOL;
        case BType::STR_ARR: return BType::STR;
        case BType::PTR_ARR: return BType::PTR;
        
        case BType::I8_ARR: return BType::I8;
        case BType::I16_ARR: return BType::I16;
        case BType::I32_ARR: return BType::I32;
        case BType::I64_ARR: return BType::I64;
        case BType::U8_ARR: return BType::U8;
        case BType::U16_ARR: return BType::U16;
        case BType::U32_ARR: return BType::U32;
        case BType::U64_ARR: return BType::U64;
        case BType::F32_ARR: return BType::F32;
        
        case BType::I8_PTR_ARR: return BType::I8_PTR;
        case BType::I16_PTR_ARR: return BType::I16_PTR;
        case BType::I32_PTR_ARR: return BType::I32_PTR;
        case BType::I64_PTR_ARR: return BType::I64_PTR;
        case BType::U8_PTR_ARR: return BType::U8_PTR;
        case BType::U16_PTR_ARR: return BType::U16_PTR;
        case BType::U32_PTR_ARR: return BType::U32_PTR;
        case BType::U64_PTR_ARR: return BType::U64_PTR;
        case BType::F32_PTR_ARR: return BType::F32_PTR;
        case BType::F64_PTR_ARR: return BType::F64_PTR;
        case BType::STR_PTR_ARR: return BType::STR_PTR;
        default: return BType::UNKNOWN;
    }
}


inline bool is_array_type(BType t) {
    return t == BType::ARR || t == BType::INT_ARR || t == BType::F64_ARR || 
           t == BType::BOOL_ARR || t == BType::STR_ARR || t == BType::PTR_ARR ||
           t == BType::I8_ARR || t == BType::I16_ARR || t == BType::I32_ARR ||
           t == BType::I64_ARR || t == BType::U8_ARR || t == BType::U16_ARR ||
           t == BType::U32_ARR || t == BType::U64_ARR || t == BType::F32_ARR ||
           t == BType::I8_PTR_ARR || t == BType::I16_PTR_ARR || t == BType::I32_PTR_ARR ||
           t == BType::I64_PTR_ARR || t == BType::U8_PTR_ARR || t == BType::U16_PTR_ARR ||
           t == BType::U32_PTR_ARR || t == BType::U64_PTR_ARR || t == BType::F32_PTR_ARR ||
           t == BType::F64_PTR_ARR || t == BType::STR_PTR_ARR;
}


inline bool is_pointer_type(BType t) {
    return t == BType::I8_PTR || t == BType::I16_PTR || t == BType::I32_PTR ||
           t == BType::I64_PTR || t == BType::U8_PTR || t == BType::U16_PTR ||
           t == BType::U32_PTR || t == BType::U64_PTR || t == BType::F32_PTR ||
           t == BType::F64_PTR || t == BType::STR_PTR;
}


inline bool is_struct_type(BType t) {
    return t == BType::STRUCT;
}

inline bool is_tuple_type(BType t) {
    return t == BType::TUPLE;
}

inline bool is_aggregate_type(BType t) {
    return is_struct_type(t) || is_tuple_type(t);
}


inline BType get_pointer_base_type(BType t) {
    switch (t) {
        case BType::I8_PTR: return BType::I8;
        case BType::I16_PTR: return BType::I16;
        case BType::I32_PTR: return BType::I32;
        case BType::I64_PTR: return BType::I64;
        case BType::U8_PTR: return BType::U8;
        case BType::U16_PTR: return BType::U16;
        case BType::U32_PTR: return BType::U32;
        case BType::U64_PTR: return BType::U64;
        case BType::F32_PTR: return BType::F32;
        case BType::F64_PTR: return BType::F64;
        case BType::STR_PTR: return BType::STR;
        case BType::PTR: return BType::I64;  
        default: return BType::UNKNOWN;
    }
}




struct TypeRef {
    BType base = BType::UNKNOWN;
    std::string name;
    std::vector<TypeRef> type_args;
    bool is_pointer = false;
    bool is_array = false;
    
    
    bool pass_by_value = false;
};

inline BType pointer_type_for(BType base) {
    switch (base) {
        case BType::I8: return BType::I8_PTR;
        case BType::I16: return BType::I16_PTR;
        case BType::I32: return BType::I32_PTR;
        case BType::I64: return BType::I64_PTR;
        case BType::U8: return BType::U8_PTR;
        case BType::U16: return BType::U16_PTR;
        case BType::U32: return BType::U32_PTR;
        case BType::U64: return BType::U64_PTR;
        case BType::ISIZE:
        case BType::USIZE:
            return sizeof(void*) == 8 ? BType::I64_PTR : BType::I32_PTR;
        case BType::HEX: return BType::U64_PTR;
        case BType::F32: return BType::F32_PTR;
        case BType::F64: return BType::F64_PTR;
        case BType::STR: return BType::STR_PTR;
        default: return BType::PTR;
    }
}

inline BType array_type_for(BType base) {
    switch (base) {
        case BType::INT: return BType::INT_ARR;
        case BType::F64: return BType::F64_ARR;
        case BType::BOOL: return BType::BOOL_ARR;
        case BType::STR: return BType::STR_ARR;
        case BType::PTR: return BType::PTR_ARR;
        case BType::I8: return BType::I8_ARR;
        case BType::I16: return BType::I16_ARR;
        case BType::I32: return BType::I32_ARR;
        case BType::I64: return BType::I64_ARR;
        case BType::U8: return BType::U8_ARR;
        case BType::U16: return BType::U16_ARR;
        case BType::U32: return BType::U32_ARR;
        case BType::U64: return BType::U64_ARR;
        case BType::ISIZE: return sizeof(void*) == 8 ? BType::I64_ARR : BType::I32_ARR;
        case BType::USIZE: return sizeof(void*) == 8 ? BType::U64_ARR : BType::U32_ARR;
        case BType::HEX: return BType::U64_ARR;
        case BType::F32: return BType::F32_ARR;
        case BType::I8_PTR: return BType::I8_PTR_ARR;
        case BType::I16_PTR: return BType::I16_PTR_ARR;
        case BType::I32_PTR: return BType::I32_PTR_ARR;
        case BType::I64_PTR: return BType::I64_PTR_ARR;
        case BType::U8_PTR: return BType::U8_PTR_ARR;
        case BType::U16_PTR: return BType::U16_PTR_ARR;
        case BType::U32_PTR: return BType::U32_PTR_ARR;
        case BType::U64_PTR: return BType::U64_PTR_ARR;
        case BType::F32_PTR: return BType::F32_PTR_ARR;
        case BType::F64_PTR: return BType::F64_PTR_ARR;
        case BType::STR_PTR: return BType::STR_PTR_ARR;
        default: return BType::ARR;
    }
}

inline BType type_ref_to_btype(const TypeRef& type_ref) {
    BType result = type_ref.base;
    if (type_ref.is_pointer) result = pointer_type_for(result);
    if (type_ref.is_array) result = array_type_for(result);
    return result;
}

inline std::string type_ref_to_string(const TypeRef& type_ref) {
    if (type_ref.base == BType::TUPLE && !type_ref.type_args.empty()) {
        std::string result = "(";
        for (size_t i = 0; i < type_ref.type_args.size(); ++i) {
            if (i > 0) result += ", ";
            result += type_ref_to_string(type_ref.type_args[i]);
        }
        result += ")";
        if (type_ref.is_pointer) result += "*";
        if (type_ref.is_array) result += "[]";
        if (type_ref.pass_by_value) result += "!";
        return result;
    }

    std::string result;
    if ((type_ref.base == BType::STRUCT || type_ref.base == BType::UNKNOWN) &&
        !type_ref.name.empty()) {
        result = type_ref.name;
    } else {
        result = type_name(type_ref.base);
    }

    if (!type_ref.type_args.empty()) {
        result += "<";
        for (size_t i = 0; i < type_ref.type_args.size(); ++i) {
            if (i > 0) result += ",";
            result += type_ref_to_string(type_ref.type_args[i]);
        }
        result += ">";
    }
    if (type_ref.is_pointer) result += "*";
    if (type_ref.is_array) result += "[]";
    if (type_ref.pass_by_value) result += "!";
    return result;
}

inline std::string mangle_type_ref(const TypeRef& type_ref) {
    if (type_ref.base == BType::TUPLE) {
        std::string result = "tup";
        for (const auto& arg : type_ref.type_args) {
            result += "__" + mangle_type_ref(arg);
        }
        if (type_ref.is_pointer) result += "_ptr";
        if (type_ref.is_array) result += "_arr";
        return result;
    }

    std::string result;
    if ((type_ref.base == BType::STRUCT || type_ref.base == BType::UNKNOWN) &&
        !type_ref.name.empty()) {
        result = type_ref.name;
    } else {
        result = type_name(type_ref.base);
    }
    for (const auto& arg : type_ref.type_args) {
        result += "__" + mangle_type_ref(arg);
    }
    if (type_ref.is_pointer) result += "_ptr";
    if (type_ref.is_array) result += "_arr";
    return result;
}

inline std::string mangle_template_struct_name(const std::string& name,
                                               const std::vector<TypeRef>& args) {
    std::string result = name;
    for (const auto& arg : args) {
        result += "__" + mangle_type_ref(arg);
    }
    return result;
}


struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string node_type() const = 0;
};


struct Program;
struct Expr;
struct FnDecl;


struct Stmt : ASTNode {
    virtual ~Stmt() = default;
};




struct BlockStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
    
    
    
    bool is_declaration_group = false;
    std::string node_type() const override { return "BlockStmt"; }
};


struct VarDeclStmt : Stmt {
    std::string name;
    BType type = BType::UNKNOWN;
    TypeRef type_ref;
    std::unique_ptr<class Expr> initializer;
    bool has_constructor_call = false; 
    std::vector<std::unique_ptr<class Expr>> constructor_args;
    std::unique_ptr<class Expr> array_size;  
    bool is_const = false;
    std::string type_annotation;  
    std::string struct_name;     
    std::string node_type() const override { return "VarDeclStmt"; }
};



struct TupleDestructureStmt : Stmt {
    std::vector<std::string> names;
    std::unique_ptr<class Expr> initializer;
    bool is_const = false;
    std::string node_type() const override { return "TupleDestructureStmt"; }
};


struct AssignStmt : Stmt {
    std::string name;
    std::unique_ptr<class Expr> value;
    std::string assignment_op = "=";
    std::string node_type() const override { return "AssignStmt"; }
};


struct ArrayAssignStmt : Stmt {
    std::string array_name;
    std::unique_ptr<class Expr> index;
    std::unique_ptr<class Expr> value;
    std::string assignment_op = "=";
    std::string node_type() const override { return "ArrayAssignStmt"; }
};


struct DerefAssignStmt : Stmt {
    std::unique_ptr<class Expr> pointer;  
    std::unique_ptr<class Expr> value;    
    std::string assignment_op = "=";
    std::string node_type() const override { return "DerefAssignStmt"; }
};



struct MemberAssignStmt : Stmt {
    std::unique_ptr<class Expr> lhs;
    std::unique_ptr<class Expr> value;
    std::string assignment_op = "=";
    std::string node_type() const override { return "MemberAssignStmt"; }
};


struct IfStmt : Stmt {
    std::unique_ptr<class Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
    std::string node_type() const override { return "IfStmt"; }
};


struct ForStmt : Stmt {
    std::string var_name;
    BType var_type = BType::INT;
    std::unique_ptr<class Expr> bound;
    std::unique_ptr<Stmt> body;
    std::string node_type() const override { return "ForStmt"; }
};


struct ForWhileStmt : Stmt {
    std::unique_ptr<class Expr> condition;
    std::unique_ptr<Stmt> body;
    std::string node_type() const override { return "ForWhileStmt"; }
};


struct ReturnStmt : Stmt {
    std::unique_ptr<class Expr> value;
    std::string node_type() const override { return "ReturnStmt"; }
};


struct NodropStmt : Stmt {
    std::string name;
    std::string node_type() const override { return "NodropStmt"; }
};

struct DropNowStmt : Stmt {
    std::unique_ptr<class Expr> value;
    std::string node_type() const override { return "DropNowStmt"; }
};


struct BreakStmt : Stmt {
    std::string node_type() const override { return "BreakStmt"; }
};

struct ContinueStmt : Stmt {
    std::string node_type() const override { return "ContinueStmt"; }
};


struct MatchStmt : Stmt {
    std::unique_ptr<class Expr> value;
    std::vector<std::pair<std::unique_ptr<class Expr>, std::unique_ptr<Stmt>>> cases;
    std::unique_ptr<Stmt> default_case;
    std::string node_type() const override { return "MatchStmt"; }
};


struct ExprStmt : Stmt {
    std::unique_ptr<class Expr> expression;
    std::string node_type() const override { return "ExprStmt"; }
};


struct LLStmt : Stmt {
    std::string llvm_code;
    std::string node_type() const override { return "LLStmt"; }
};


struct LLHStmt : Stmt {
    std::string llvm_code;
    std::string node_type() const override { return "LLHStmt"; }
};


struct TakeStmt : Stmt {
    std::string path;
    std::string node_type() const override { return "TakeStmt"; }
};


struct PluginStmt : Stmt {
    std::string path;
    std::string node_type() const override { return "PluginStmt"; }
};


struct StructField {
    std::string name;
    BType type = BType::UNKNOWN;
    TypeRef type_ref;
    std::string type_annotation;  
    std::string struct_name;      
};

struct StructDecl : Stmt {
    std::string name;
    std::vector<std::string> type_params;
    std::vector<StructField> fields;
    bool is_drop = false;
    bool is_extern = false;
    std::string node_type() const override { return "StructDecl"; }
};


struct ParamDecl {
    std::string name;
    BType type = BType::UNKNOWN;
    TypeRef type_ref;
    std::string type_annotation;  
    std::string struct_name;
};



struct Expr : ASTNode {
    BType btype = BType::UNKNOWN;
};


struct NumberExpr : Expr {
    double value;
    bool is_float;
    
    
    std::string literal;
    std::string node_type() const override { return "NumberExpr"; }
};

struct StringExpr : Expr {
    std::string value;
    std::string node_type() const override { return "StringExpr"; }
};

struct BoolExpr : Expr {
    bool value;
    std::string node_type() const override { return "BoolExpr"; }
};

struct NullExpr : Expr {
    std::string node_type() const override { return "NullExpr"; }
};


struct VariableExpr : Expr {
    std::string name;
    std::string node_type() const override { return "VariableExpr"; }
};


struct BinaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::string node_type() const override { return "BinaryExpr"; }
};


struct UnaryExpr : Expr {
    std::string op;
    std::unique_ptr<Expr> operand;
    std::string node_type() const override { return "UnaryExpr"; }
};


struct TernaryExpr : Expr {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> then_expr;
    std::unique_ptr<Expr> else_expr;
    std::string node_type() const override { return "TernaryExpr"; }
};


struct CallExpr : Expr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    std::vector<BType> template_args;  
    
    
    
    bool is_method_call = false;
    std::string node_type() const override { return "CallExpr"; }
};


struct AnonymousFnExpr : Expr {
    std::vector<std::string> type_params;  
    std::vector<ParamDecl> params;
    BType return_type = BType::UNKNOWN;
    std::string return_type_annotation;
    std::unique_ptr<Stmt> body;
    std::string node_type() const override { return "AnonymousFnExpr"; }
};


struct ArrayExpr : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    std::string node_type() const override { return "ArrayExpr"; }
};

struct TupleExpr : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    std::string node_type() const override { return "TupleExpr"; }
};



struct StructLiteralField {
    std::string name;
    std::unique_ptr<Expr> value;
};

struct StructLiteralExpr : Expr {
    TypeRef type_ref;
    std::string struct_name;
    std::vector<StructLiteralField> fields;
    std::string node_type() const override { return "StructLiteralExpr"; }
};


struct IndexExpr : Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::string node_type() const override { return "IndexExpr"; }
};


struct SizeofExpr : Expr {
    std::string name;           
    std::unique_ptr<Expr> expr; 
    std::string node_type() const override { return "SizeofExpr"; }
};


struct RefExpr : Expr {
    std::unique_ptr<Expr> operand;
    std::string node_type() const override { return "RefExpr"; }
};


struct DerefExpr : Expr {
    std::unique_ptr<Expr> operand;
    std::string node_type() const override { return "DerefExpr"; }
};


struct AsExpr : Expr {
    std::unique_ptr<Expr> operand;
    std::string type_annotation;  
    std::string node_type() const override { return "AsExpr"; }
};


struct MemberExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string member;
    std::string struct_name;  
    std::string node_type() const override { return "MemberExpr"; }
};


struct FnDecl {
    std::string name;
    std::vector<std::string> type_params;  
    std::vector<ParamDecl> params;
    BType return_type = BType::UNKNOWN;
    TypeRef return_type_ref;
    std::string return_type_annotation;  
    std::unique_ptr<Stmt> body;
    bool is_method = false;
    bool is_drop = false;
    std::string method_owner;
    std::string method_name;
    bool is_operator = false;
    std::string operator_symbol;
    bool force_inline = false;
    bool force_noinline = false;
    bool is_extern = false;
    bool is_variadic = false;
};

inline std::string mangle_method_name(const std::string& owner,
                                      const std::string& method) {
    return "__method__" + owner + "__" + method;
}




inline std::string operator_method_name(const std::string& op,
                                        size_t operand_count) {
    if (op == "[]") return "__operator_index";
    if (op == "+") return operand_count == 0 ? "__operator_pos" : "__operator_add";
    if (op == "-") return operand_count == 0 ? "__operator_neg" : "__operator_sub";
    if (op == "*") return "__operator_mul";
    if (op == "/") return "__operator_div";
    if (op == "%") return "__operator_mod";
    if (op == "<<") return "__operator_shl";
    if (op == ">>") return "__operator_shr";
    if (op == "<") return "__operator_lt";
    if (op == ">") return "__operator_gt";
    if (op == "<=") return "__operator_le";
    if (op == ">=") return "__operator_ge";
    if (op == "is" || op == "==") return "__operator_eq";
    if (op == "not" || op == "!=") return "__operator_ne";
    if (op == "&") return "__operator_bit_and";
    if (op == "|") return "__operator_bit_or";
    if (op == "#") return "__operator_bit_xor";
    if (op == "~") return "__operator_bit_not";
    if (op == "and") return "__operator_and";
    if (op == "or") return "__operator_or";
    if (op == "!") return "__operator_not";
    if (op == ":") return "__operator_colon";
    if (op == "+=") return "__operator_add_assign";
    if (op == "-=") return "__operator_sub_assign";
    if (op == "*=") return "__operator_mul_assign";
    if (op == "/=") return "__operator_div_assign";
    if (op == "%=") return "__operator_mod_assign";
    if (op == "<<=") return "__operator_shl_assign";
    if (op == ">>=") return "__operator_shr_assign";
    if (op == "&=") return "__operator_bit_and_assign";
    if (op == "|=") return "__operator_bit_or_assign";
    if (op == "#=") return "__operator_bit_xor_assign";
    return "__operator_unknown";
}

inline bool is_compound_assignment_operator(const std::string& op) {
    static const std::vector<std::string> operators = {
        "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "#="
    };
    return std::find(operators.begin(), operators.end(), op) != operators.end();
}

inline bool is_assignment_operator(const std::string& op) {
    return op == "=" || is_compound_assignment_operator(op);
}

inline std::string compound_base_operator(const std::string& op) {
    if (!is_compound_assignment_operator(op)) return op;
    return op.substr(0, op.size() - 1);
}


struct Program {
    std::vector<std::unique_ptr<FnDecl>> functions;
    std::vector<std::unique_ptr<Stmt>> statements;
};


std::unique_ptr<Program> parse_ast(std::vector<Token>& tokens);


inline bool is_type_param(const std::string& name, const std::vector<std::string>& type_params) {
    for (const auto& tp : type_params) {
        if (tp == name) return true;
    }
    return false;
}
