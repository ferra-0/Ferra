#include "llvm_emitter.h"
#include "global.h"
#include "file.h"
#include "platform.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

std::string current_function_name;

namespace {

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool is_ir_integer(IRType type) {
    switch (type) {
        case IRType::I1:
        case IRType::I8:
        case IRType::I16:
        case IRType::I32:
        case IRType::I64:
            return true;
        default:
            return false;
    }
}

int ir_integer_bits(IRType type) {
    switch (type) {
        case IRType::I1: return 1;
        case IRType::I8: return 8;
        case IRType::I16: return 16;
        case IRType::I32: return 32;
        case IRType::I64: return 64;
        default: return 0;
    }
}

bool is_ir_float(IRType type) {
    return type == IRType::F32 || type == IRType::F64;
}

bool is_ir_pointer(IRType type) {
    switch (type) {
        case IRType::PTR:
        case IRType::I8_PTR:
        case IRType::I16_PTR:
        case IRType::I32_PTR:
        case IRType::I64_PTR:
        case IRType::F32_PTR:
        case IRType::F64_PTR:
        case IRType::ARR:
            return true;
        default:
            return false;
    }
}

std::string llvm_ir_type_name(IRType type) {
    if (type == IRType::PTR || type == IRType::ARR) return "i8*";
    return llvm_type_str(type);
}

IRType parse_ll_ir_type(std::string text) {
    text = trim_copy(std::move(text));
    const size_t parameter_name = text.find('%');
    if (parameter_name != std::string::npos) {
        text = trim_copy(text.substr(0, parameter_name));
    }

    if (text == "void") return IRType::VOID;
    if (text == "i1") return IRType::I1;
    if (text == "i8") return IRType::I8;
    if (text == "i16") return IRType::I16;
    if (text == "i32") return IRType::I32;
    if (text == "i64") return IRType::I64;
    if (text == "float") return IRType::F32;
    if (text == "double") return IRType::F64;
    if (text == "ptr") return IRType::PTR;
    if (text == "i8*") return IRType::I8_PTR;
    if (text == "i16*") return IRType::I16_PTR;
    if (text == "i32*") return IRType::I32_PTR;
    if (text == "i64*") return IRType::I64_PTR;
    if (text == "float*") return IRType::F32_PTR;
    if (text == "double*") return IRType::F64_PTR;
    return IRType::UNKNOWN;
}

std::string normalize_opaque_ptr_tokens(const std::string& source) {
    std::string result;
    result.reserve(source.size() + 8);

    for (size_t i = 0; i < source.size();) {
        const bool starts_ptr = i + 3 <= source.size() && source.compare(i, 3, "ptr") == 0;
        const bool left_boundary = i == 0 ||
            (!std::isalnum(static_cast<unsigned char>(source[i - 1])) && source[i - 1] != '_');
        const bool right_boundary = i + 3 == source.size() ||
            (!std::isalnum(static_cast<unsigned char>(source[i + 3])) && source[i + 3] != '_');

        if (starts_ptr && left_boundary && right_boundary) {
            result += "i8*";
            i += 3;
        } else {
            result.push_back(source[i]);
            ++i;
        }
    }
    return result;
}

std::vector<std::string> split_ll_args(const std::string& args) {
    std::vector<std::string> result;
    size_t start = 0;
    int depth = 0;

    for (size_t i = 0; i <= args.size(); ++i) {
        const char ch = i < args.size() ? args[i] : ',';
        if (ch == '(' || ch == '[' || ch == '{' || ch == '<') ++depth;
        if (ch == ')' || ch == ']' || ch == '}' || ch == '>') --depth;
        if (ch == ',' && depth == 0) {
            std::string part = trim_copy(args.substr(start, i - start));
            if (!part.empty() && part != "...") result.push_back(std::move(part));
            start = i + 1;
        }
    }
    return result;
}

std::unordered_map<const LLVMEmitter*, std::unordered_map<std::string, std::string>> g_global_struct_types;
std::unordered_map<const LLVMEmitter*, bool> g_has_global_init;

bool coerce_ir_value(
    LLVMEmitter& emitter,
    std::string& value,
    IRType from,
    IRType to,
    const std::string& pad = "  ",
    bool source_is_unsigned = false
) {
    if (to == IRType::UNKNOWN || from == IRType::UNKNOWN || from == to) return true;

    const std::string from_name = llvm_ir_type_name(from);
    const std::string to_name = llvm_ir_type_name(to);

    if (is_ir_pointer(from) && is_ir_pointer(to)) {
        if (from_name == to_name) return true;
        std::string converted = emitter.next_ssa();
        emitter.body << pad << converted << " = bitcast " << from_name << " " << value
                     << " to " << to_name << "\n";
        value = converted;
        return true;
    }

    if (is_ir_integer(from) && is_ir_integer(to)) {
        std::string converted = emitter.next_ssa();
        const int from_bits = ir_integer_bits(from);
        const int to_bits = ir_integer_bits(to);

        if (to == IRType::I1) {
            emitter.body << pad << converted << " = icmp ne " << from_name << " " << value
                         << ", 0\n";
        } else if (from == IRType::I1) {
            emitter.body << pad << converted << " = zext i1 " << value
                         << " to " << to_name << "\n";
        } else if (from_bits < to_bits) {
            emitter.body << pad << converted << " = "
                         << (source_is_unsigned ? "zext " : "sext ")
                         << from_name << " " << value
                         << " to " << to_name << "\n";
        } else if (from_bits > to_bits) {
            emitter.body << pad << converted << " = trunc " << from_name << " " << value
                         << " to " << to_name << "\n";
        } else {
            return true;
        }
        value = converted;
        return true;
    }

    if (is_ir_integer(from) && is_ir_float(to)) {
        std::string converted = emitter.next_ssa();
        emitter.body << pad << converted << " = "
                     << (source_is_unsigned ? "uitofp " : "sitofp ")
                     << from_name << " " << value
                     << " to " << to_name << "\n";
        value = converted;
        return true;
    }

    if (is_ir_float(from) && is_ir_integer(to)) {
        std::string converted = emitter.next_ssa();
        if (to == IRType::I1) {
            emitter.body << pad << converted << " = fcmp one " << from_name << " " << value
                         << ", 0.0\n";
        } else {
            emitter.body << pad << converted << " = fptosi " << from_name << " " << value
                         << " to " << to_name << "\n";
        }
        value = converted;
        return true;
    }

    if (from == IRType::F32 && to == IRType::F64) {
        std::string converted = emitter.next_ssa();
        emitter.body << pad << converted << " = fpext float " << value << " to double\n";
        value = converted;
        return true;
    }

    if (from == IRType::F64 && to == IRType::F32) {
        std::string converted = emitter.next_ssa();
        emitter.body << pad << converted << " = fptrunc double " << value << " to float\n";
        value = converted;
        return true;
    }

    if (is_ir_pointer(from) && to == IRType::I1) {
        std::string converted = emitter.next_ssa();
        emitter.body << pad << converted << " = icmp ne " << from_name << " " << value
                     << ", null\n";
        value = converted;
        return true;
    }

    return false;
}

bool is_runtime_top_level_statement(const Stmt* stmt) {
    return dynamic_cast<const AssignStmt*>(stmt) != nullptr ||
           dynamic_cast<const ArrayAssignStmt*>(stmt) != nullptr ||
           dynamic_cast<const MemberAssignStmt*>(stmt) != nullptr ||
           dynamic_cast<const DerefAssignStmt*>(stmt) != nullptr ||
           dynamic_cast<const NodropStmt*>(stmt) != nullptr ||
           dynamic_cast<const DropNowStmt*>(stmt) != nullptr ||
           dynamic_cast<const ExprStmt*>(stmt) != nullptr ||
           dynamic_cast<const IfStmt*>(stmt) != nullptr ||
           dynamic_cast<const ForStmt*>(stmt) != nullptr ||
           dynamic_cast<const ForWhileStmt*>(stmt) != nullptr ||
           dynamic_cast<const MatchStmt*>(stmt) != nullptr ||
           dynamic_cast<const BlockStmt*>(stmt) != nullptr;
}

bool is_platform_call(const Expr* expr) {
    auto* call = dynamic_cast<const CallExpr*>(expr);
    return call && !call->is_method_call && call->callee == "platform" &&
           call->args.empty();
}

bool evaluate_compile_time_condition(const Expr* expr, bool& result) {
    if (!expr) return false;

    if (auto* boolean = dynamic_cast<const BoolExpr*>(expr)) {
        result = boolean->value;
        return true;
    }

    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr);
        unary && unary->op == "!") {
        bool operand = false;
        if (!evaluate_compile_time_condition(unary->operand.get(), operand)) {
            return false;
        }
        result = !operand;
        return true;
    }

    auto* binary = dynamic_cast<const BinaryExpr*>(expr);
    if (!binary) return false;

    if (binary->op == "and" || binary->op == "or") {
        bool left = false;
        bool right = false;
        if (!evaluate_compile_time_condition(binary->left.get(), left) ||
            !evaluate_compile_time_condition(binary->right.get(), right)) {
            return false;
        }
        result = binary->op == "and" ? left && right : left || right;
        return true;
    }

    const StringExpr* platform_name = nullptr;
    if (is_platform_call(binary->left.get())) {
        platform_name = dynamic_cast<const StringExpr*>(binary->right.get());
    } else if (is_platform_call(binary->right.get())) {
        platform_name = dynamic_cast<const StringExpr*>(binary->left.get());
    }
    if (!platform_name) return false;

    const bool equal = platform_name->value == compiler_platform_name();
    if (binary->op == "is" || binary->op == "==") {
        result = equal;
        return true;
    }
    if (binary->op == "not" || binary->op == "!=") {
        result = !equal;
        return true;
    }
    return false;
}

void collect_active_llh(const Stmt* stmt, std::vector<const LLHStmt*>& result) {
    if (!stmt) return;

    if (auto* llh = dynamic_cast<const LLHStmt*>(stmt)) {
        result.push_back(llh);
        return;
    }
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& child : block->statements) {
            collect_active_llh(child.get(), result);
        }
        return;
    }
    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        bool condition = false;
        if (evaluate_compile_time_condition(ifs->condition.get(), condition)) {
            collect_active_llh(
                condition ? ifs->then_branch.get() : ifs->else_branch.get(), result);
        } else {
            
            
            collect_active_llh(ifs->then_branch.get(), result);
            collect_active_llh(ifs->else_branch.get(), result);
        }
    }
}

} 

std::string get_array_ptr_type(BType elem_type) {
    if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
    return llvm_type_str(btype_to_ir(elem_type)) + "*";
}

std::string get_array_ptr_ptr_type(BType elem_type) {
    if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
    return llvm_type_str(btype_to_ir(elem_type)) + "**";
}

static bool is_pointer_like_btype(BType type) {
    if (type == BType::ARR ||
        is_array_type(type)) {
        return true;
    }

    switch (type) {
        case BType::VOID:
        case BType::FUNC:
        case BType::PTR:
        case BType::STR:

        case BType::I8_PTR:
        case BType::I16_PTR:
        case BType::I32_PTR:
        case BType::I64_PTR:

        case BType::U8_PTR:
        case BType::U16_PTR:
        case BType::U32_PTR:
        case BType::U64_PTR:

        case BType::F32_PTR:
        case BType::F64_PTR:
        case BType::STR_PTR:
            return true;

        default:
            return false;
    }
}

static bool is_null_expression(const Expr* expr) {
    return dynamic_cast<const NullExpr*>(expr) != nullptr;
}

static std::string llvm_function_pointer_type(
    IRType return_type,
    const std::vector<IRType>& argument_types
) {
    if (return_type == IRType::UNKNOWN ||
        return_type == IRType::STRUCT ||
        return_type == IRType::ARR) {
        return "";
    }

    std::string result = llvm_ir_type_name(return_type) + " (";
    for (size_t i = 0; i < argument_types.size(); ++i) {
        if (argument_types[i] == IRType::UNKNOWN ||
            argument_types[i] == IRType::STRUCT ||
            argument_types[i] == IRType::ARR ||
            argument_types[i] == IRType::VOID) {
            return "";
        }
        if (i != 0) result += ", ";
        result += llvm_ir_type_name(argument_types[i]);
    }
    result += ")*";
    return result;
}

static bool function_signature_from_expression(
    LLVMEmitter& emitter,
    const Expr* expr,
    IRType& return_type,
    std::vector<IRType>& argument_types
) {
    if (!expr) return false;

    const Expr* candidate = expr;
    if (auto* reference = dynamic_cast<const RefExpr*>(candidate)) {
        candidate = reference->operand.get();
    }

    if (auto* anonymous = dynamic_cast<const AnonymousFnExpr*>(candidate)) {
        return_type = btype_to_ir(anonymous->return_type);
        argument_types.clear();
        for (const ParamDecl& parameter : anonymous->params) {
            argument_types.push_back(btype_to_ir(parameter.type));
        }
        return !llvm_function_pointer_type(return_type, argument_types).empty();
    }

    auto* variable = dynamic_cast<const VariableExpr*>(candidate);
    if (!variable) return false;

    auto local = emitter.vars.find(variable->name);
    if (local != emitter.vars.end()) {
        if (!local->second.is_function_pointer ||
            !local->second.function_signature_known) {
            return false;
        }
        return_type = local->second.function_return_type;
        argument_types = local->second.function_argument_types;
        return true;
    }

    
    if (emitter.global_vars.count(variable->name)) return false;

    auto function = emitter.func_types.find(variable->name);
    auto arguments = emitter.func_arg_types.find(variable->name);
    if (function == emitter.func_types.end() ||
        arguments == emitter.func_arg_types.end()) {
        return false;
    }

    return_type = function->second;
    argument_types = arguments->second;
    return !llvm_function_pointer_type(return_type, argument_types).empty();
}

static bool remember_function_pointer_signature(
    LLVMEmitter& emitter,
    LLVMVar& variable,
    const Expr* initializer,
    bool report_error = true
) {
    IRType return_type = IRType::UNKNOWN;
    std::vector<IRType> argument_types;
    if (!function_signature_from_expression(
            emitter, initializer, return_type, argument_types)) {
        return false;
    }

    if (variable.function_signature_known &&
        (variable.function_return_type != return_type ||
         variable.function_argument_types != argument_types)) {
        if (report_error) {
            gerror("Cannot assign a function with a different signature to '" +
                   variable.name + "' :/\n");
        }
        return false;
    }

    variable.is_function_pointer = true;
    variable.function_signature_known = true;
    variable.function_return_type = return_type;
    variable.function_argument_types = std::move(argument_types);
    return true;
}

static std::string pointer_pointee_struct_name(
    LLVMEmitter& emitter,
    const Expr* expr
);

static BType pointer_pointee_type_from_expression(
    LLVMEmitter& emitter,
    const Expr* expr
) {
    if (!expr) return BType::UNKNOWN;

    if (auto* reference = dynamic_cast<const RefExpr*>(expr)) {
        return emitter.get_expr_type(reference->operand.get());
    }

    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto local = emitter.vars.find(variable->name);
        if (local != emitter.vars.end() &&
            local->second.elem_type != BType::UNKNOWN) {
            return local->second.elem_type;
        }
    }

    if (!pointer_pointee_struct_name(emitter, expr).empty()) {
        return BType::STRUCT;
    }

    BType pointer_type = emitter.get_expr_type(expr);
    if (pointer_type == BType::UNKNOWN) pointer_type = expr->btype;
    return get_pointer_base_type(pointer_type);
}

static std::string pointer_pointee_struct_name(
    LLVMEmitter& emitter,
    const Expr* expr
) {
    if (!expr) return "";

    if (auto* reference = dynamic_cast<const RefExpr*>(expr)) {
        return emitter.get_expr_struct_name(reference->operand.get());
    }

    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto local = emitter.vars.find(variable->name);
        if (local != emitter.vars.end() &&
            (local->second.elem_type == BType::STRUCT ||
             local->second.elem_type == BType::TUPLE)) {
            return local->second.struct_name;
        }
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        std::string owner_name =
            emitter.get_expr_struct_name(member->object.get());
        if (owner_name.empty()) {
            owner_name = pointer_pointee_struct_name(
                emitter, member->object.get());
        }

        auto owner = emitter.structs.find(owner_name);
        if (owner != emitter.structs.end()) {
            auto field = owner->second.field_indices.find(member->member);
            if (field != owner->second.field_indices.end()) {
                const size_t index = field->second;
                if (index < owner->second.field_types.size() &&
                    index < owner->second.field_annotations.size() &&
                    is_pointer_like_btype(
                        owner->second.field_types[index]) &&
                    emitter.structs.count(
                        owner->second.field_annotations[index])) {
                    return owner->second.field_annotations[index];
                }
            }
        }
    }

    
    
    
    BType expression_type = emitter.get_expr_type(expr);
    if (expression_type == BType::UNKNOWN) expression_type = expr->btype;
    if (is_pointer_like_btype(expression_type)) {
        const std::string name = emitter.get_expr_struct_name(expr);
        if (!name.empty() && emitter.structs.count(name)) return name;
    }

    return "";
}

static std::string struct_array_element_name(
    LLVMEmitter& emitter,
    const Expr* array_expression
) {
    if (!array_expression) return "";

    if (auto* variable = dynamic_cast<const VariableExpr*>(array_expression)) {
        auto local = emitter.vars.find(variable->name);
        if (local != emitter.vars.end() &&
            local->second.type == IRType::ARR &&
            (local->second.elem_type == BType::STRUCT ||
             local->second.elem_type == BType::TUPLE)) {
            return local->second.struct_name;
        }
    }

    auto* member = dynamic_cast<const MemberExpr*>(array_expression);
    if (!member) return "";

    std::string owner_name =
        emitter.get_expr_struct_name(member->object.get());
    if (owner_name.empty()) {
        owner_name = pointer_pointee_struct_name(
            emitter, member->object.get());
    }
    auto owner = emitter.structs.find(owner_name);
    if (owner == emitter.structs.end()) return "";

    auto field = owner->second.field_indices.find(member->member);
    if (field == owner->second.field_indices.end()) return "";
    const size_t index = field->second;
    if (index >= owner->second.field_types.size() ||
        index >= owner->second.field_annotations.size() ||
        !is_array_type(owner->second.field_types[index])) {
        return "";
    }

    const std::string& element_name =
        owner->second.field_annotations[index];
    return emitter.structs.count(element_name) ? element_name : "";
}

static bool struct_array_field_is_inline(
    LLVMEmitter& emitter,
    const MemberExpr* member
) {
    if (!member) return false;

    std::string owner_name =
        emitter.get_expr_struct_name(member->object.get());
    if (owner_name.empty()) {
        owner_name = pointer_pointee_struct_name(
            emitter, member->object.get());
    }

    auto owner = emitter.structs.find(owner_name);
    if (owner == emitter.structs.end()) return false;
    auto field = owner->second.field_indices.find(member->member);
    if (field == owner->second.field_indices.end()) return false;

    const size_t index = field->second;
    return index < owner->second.field_inline_struct_arrays.size() &&
           owner->second.field_inline_struct_arrays[index];
}

static bool is_inline_struct_array_expression(
    LLVMEmitter& emitter,
    const Expr* expression
) {
    if (auto* variable = dynamic_cast<const VariableExpr*>(expression)) {
        auto local = emitter.vars.find(variable->name);
        return local != emitter.vars.end() &&
               local->second.type == IRType::ARR &&
               (local->second.elem_type == BType::STRUCT ||
                local->second.elem_type == BType::TUPLE) &&
               local->second.inline_struct_array;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expression)) {
        return struct_array_field_is_inline(emitter, member);
    }

    return false;
}

static std::string emitted_pointer_type(
    LLVMEmitter& emitter,
    const Expr* expr
) {
    if (!expr || is_null_expression(expr)) return "i8*";

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        std::string callee;
        if (emitter.resolve_call_target(call, callee, false)) {
            auto return_type = emitter.func_types.find(callee);
            if (return_type != emitter.func_types.end() &&
                is_ir_pointer(return_type->second) &&
                return_type->second != IRType::STRUCT) {
                return llvm_ir_type_name(return_type->second);
            }
        }
    }

    if (auto* reference = dynamic_cast<const RefExpr*>(expr)) {
        if (auto* variable =
                dynamic_cast<const VariableExpr*>(reference->operand.get())) {
            auto local = emitter.vars.find(variable->name);
            if (local != emitter.vars.end()) {
                const LLVMVar& value = local->second;
                if (value.type == IRType::STRUCT) {
                    
                    return "i64*";
                }
                if (value.type == IRType::ARR) {
                if ((value.elem_type == BType::STRUCT ||
                     value.elem_type == BType::TUPLE) &&
                    !value.struct_name.empty()) {
                    return emitter.get_struct_type_str(
                        value.struct_name) +
                        (value.inline_struct_array ? "**" : "***");
                    }
                    BType element_type = value.elem_type;
                    if (element_type == BType::UNKNOWN) {
                        element_type = BType::INT;
                    }
                    return get_array_ptr_ptr_type(element_type);
                }
                return llvm_ptr_type_str(value.type);
            }

            auto global = emitter.global_vars.find(variable->name);
            if (global != emitter.global_vars.end()) {
                return llvm_ptr_type_str(global->second);
            }
        }

        if (auto* index =
                dynamic_cast<const IndexExpr*>(reference->operand.get())) {
            if (!struct_array_element_name(
                    emitter, index->object.get()).empty()) {
                return "i64*";
            }
        }

        if (auto* member =
                dynamic_cast<const MemberExpr*>(reference->operand.get())) {
            const std::string element_struct =
                struct_array_element_name(emitter, member);
            if (!element_struct.empty()) {
                return emitter.get_struct_type_str(element_struct) +
                    (struct_array_field_is_inline(emitter, member)
                        ? "**"
                        : "***");
            }

            const std::string member_struct =
                emitter.get_expr_struct_name(member);
            if (!member_struct.empty()) {
                return emitter.get_struct_type_str(member_struct) + "*";
            }

            BType member_type = emitter.get_expr_type(member);
            if (member_type != BType::UNKNOWN) {
                return emitter.get_llvm_type(member_type) + "*";
            }
        }

        BType reference_type = emitter.get_expr_type(reference);
        if (reference_type != BType::UNKNOWN) {
            return emitter.get_llvm_type(reference_type);
        }
        return "";
    }

    const std::string struct_name = emitter.get_expr_struct_name(expr);
    if (!struct_name.empty()) {
        return emitter.get_struct_type_str(struct_name) + "*";
    }

    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto local = emitter.vars.find(variable->name);
        if (local != emitter.vars.end()) {
            const LLVMVar& value = local->second;
            if (value.type == IRType::ARR) {
                if ((value.elem_type == BType::STRUCT ||
                     value.elem_type == BType::TUPLE) &&
                    !value.struct_name.empty()) {
                    return emitter.get_struct_type_str(
                        value.struct_name) +
                        (value.inline_struct_array ? "*" : "**");
                }
                BType element_type = value.elem_type;
                if (element_type == BType::UNKNOWN) {
                    element_type = BType::INT;
                }
                return get_array_ptr_type(element_type);
            }
            return llvm_type_str(value.type);
        }

        auto global = emitter.global_vars.find(variable->name);
        if (global != emitter.global_vars.end()) {
            return llvm_ir_type_name(global->second);
        }
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        const std::string element_struct =
            struct_array_element_name(emitter, member);
        if (!element_struct.empty()) {
            return emitter.get_struct_type_str(element_struct) +
                (struct_array_field_is_inline(emitter, member)
                    ? "*"
                    : "**");
        }
    }

    BType value_type = emitter.get_expr_type(expr);
    if (value_type == BType::UNKNOWN) value_type = expr->btype;
    if (is_pointer_like_btype(value_type)) {
        return emitter.get_llvm_type(value_type);
    }
    return "";
}

static bool is_unsigned_integer_type(BType type) {
    return type == BType::BOOL ||
           type == BType::U8 || type == BType::U16 ||
           type == BType::U32 || type == BType::U64 ||
           type == BType::USIZE || type == BType::HEX;
}

static bool normalize_integer_to_i64(
    LLVMEmitter& emitter,
    const Expr* expression,
    std::string& value,
    const std::string& context,
    const std::string& pad = "  "
) {
    BType source_type = emitter.get_expr_type(expression);
    if (source_type == BType::UNKNOWN && expression) {
        source_type = expression->btype;
    }

    IRType source_ir = btype_to_ir(source_type);
    if (source_ir == IRType::UNKNOWN) source_ir = IRType::I64;
    if (!is_ir_integer(source_ir)) {
        gerror(context + " must be an integer :/\n");
        return false;
    }
    if (source_ir == IRType::I64) return true;

    if (!coerce_ir_value(
            emitter, value, source_ir, IRType::I64, pad,
            is_unsigned_integer_type(source_type))) {
        gerror("Cannot normalize " + context + " :/\n");
        return false;
    }
    return true;
}

static bool is_float_type(BType type) {
    return type == BType::F32 || type == BType::F64;
}

static BType pointer_type_for_value_type(BType value_type) {
    switch (value_type) {
        case BType::INT:
        case BType::I64: return BType::I64_PTR;
        case BType::I8: return BType::I8_PTR;
        case BType::I16: return BType::I16_PTR;
        case BType::I32: return BType::I32_PTR;
        case BType::U8: return BType::U8_PTR;
        case BType::U16: return BType::U16_PTR;
        case BType::U32: return BType::U32_PTR;
        case BType::U64: return BType::U64_PTR;
        case BType::HEX: return BType::U64_PTR;
        case BType::F32: return BType::F32_PTR;
        case BType::F64: return BType::F64_PTR;
        default: return BType::PTR;
    }
}

static std::string cast_pointer_to_pointee(
    LLVMEmitter& emitter,
    std::string pointer,
    BType pointer_type,
    BType pointee_type,
    const std::string& pad = "  "
) {
    IRType pointer_ir_type = btype_to_ir(pointer_type);
    if (!is_ir_pointer(pointer_ir_type)) pointer_ir_type = IRType::I64_PTR;

    const IRType pointee_ir_type = btype_to_ir(pointee_type);
    const std::string source_pointer_type =
        llvm_ir_type_name(pointer_ir_type);
    const std::string target_pointer_type =
        llvm_ptr_type_str(pointee_ir_type);

    if (source_pointer_type == target_pointer_type) return pointer;

    std::string converted = emitter.next_ssa();
    emitter.body << pad << converted << " = bitcast "
                 << source_pointer_type << " " << pointer
                 << " to " << target_pointer_type << "\n";
    return converted;
}

static bool is_pointer_like_irtype(IRType type) {
    return is_ir_pointer(type);
}

static std::string llvm_value_type_for_btype(BType type) {
    if (type == BType::PTR) {
        return llvm_type_str(btype_to_ir(type));
    }

    if (type == BType::ARR) {
        return "i8*";
    }

    if (is_array_type(type)) {
        BType elem_type =
            get_array_elem_type(type);

        if (elem_type == BType::UNKNOWN) {
            return "i8*";
        }

        return get_array_ptr_type(elem_type);
    }

    return llvm_type_str(
        btype_to_ir(type)
    );
}

static std::string llvm_storage_type_for_btype(BType type) {
    return llvm_value_type_for_btype(type) + "*";
}

static IRType volatile_value_ir_type(BType type) {
    
    
    if (type == BType::PTR || type == BType::ARR) return IRType::I8_PTR;
    return btype_to_ir(type);
}

static bool is_supported_volatile_type(BType type) {
    const IRType ir_type = volatile_value_ir_type(type);
    return ir_type != IRType::UNKNOWN &&
           ir_type != IRType::VOID &&
           ir_type != IRType::ARR &&
           ir_type != IRType::STRUCT;
}

static bool is_supported_atomic_type(BType type) {
    switch (type) {
        case BType::INT:
        case BType::I8:
        case BType::I16:
        case BType::I32:
        case BType::I64:
        case BType::U8:
        case BType::U16:
        case BType::U32:
        case BType::U64:
        case BType::ISIZE:
        case BType::USIZE:
        case BType::HEX:
            return true;
        default:
            return false;
    }
}

static std::string emit_typed_address(
    LLVMEmitter& emitter,
    const Expr* address_expr,
    BType value_type,
    bool& valid,
    const std::string& operation_name
) {
    valid = false;
    if (!address_expr) {
        gerror(operation_name + " operation requires an address :/\n");
        return "null";
    }

    const std::string address = emitter.emit_expression(address_expr);
    const std::string target_pointer_type =
        llvm_value_type_for_btype(value_type) + "*";

    
    
    if (auto* reference = dynamic_cast<const RefExpr*>(address_expr)) {
        const BType referenced_type = emitter.get_expr_type(reference->operand.get());
        if (referenced_type == BType::UNKNOWN ||
            referenced_type == BType::STRUCT ||
            is_array_type(referenced_type)) {
            gerror("Cannot use this reference as an " + operation_name +
                   " address :/\n");
            return "null";
        }

        const std::string source_pointer_type =
            llvm_storage_type_for_btype(referenced_type);
        if (source_pointer_type == target_pointer_type) {
            valid = true;
            return address;
        }

        std::string cast = emitter.next_ssa();
        emitter.body << "  " << cast << " = bitcast "
                     << source_pointer_type << " " << address
                     << " to " << target_pointer_type << "\n";
        valid = true;
        return cast;
    }

    BType address_type = emitter.get_expr_type(address_expr);
    if (address_type == BType::UNKNOWN) address_type = address_expr->btype;
    const IRType address_ir_type = volatile_value_ir_type(address_type);

    if (is_ir_integer(address_ir_type)) {
        std::string cast = emitter.next_ssa();
        emitter.body << "  " << cast << " = inttoptr "
                     << llvm_ir_type_name(address_ir_type) << " " << address
                     << " to " << target_pointer_type << "\n";
        valid = true;
        return cast;
    }

    if (!is_pointer_like_btype(address_type) &&
        !is_ir_pointer(address_ir_type)) {
        gerror(operation_name +
               " address must be a pointer, reference, usize, or isize :/\n");
        return "null";
    }

    const std::string source_pointer_type =
        llvm_value_type_for_btype(address_type);
    if (source_pointer_type == target_pointer_type) {
        valid = true;
        return address;
    }

    std::string cast = emitter.next_ssa();
    emitter.body << "  " << cast << " = bitcast "
                 << source_pointer_type << " " << address
                 << " to " << target_pointer_type << "\n";
    valid = true;
    return cast;
}

static bool pointer_types_compatible(
    BType from,
    BType to
) {
    if (!is_pointer_like_btype(from) ||
        !is_pointer_like_btype(to)) {
        return false;
    }

    if (from == BType::PTR ||
        to == BType::PTR ||
        from == BType::ARR ||
        to == BType::ARR) {
        return true;
    }

    if (is_array_type(from) &&
        is_array_type(to)) {
        BType from_element =
            get_array_elem_type(from);

        BType to_element =
            get_array_elem_type(to);

        return from_element == BType::UNKNOWN ||
               to_element == BType::UNKNOWN ||
               from_element == to_element;
    }

    if (is_array_type(from)) {
        BType elem =
            get_array_elem_type(from);

        if (elem == BType::UNKNOWN) {
            return true;
        }

        return (
            elem == BType::I8 &&
            (
                to == BType::I8_PTR ||
                to == BType::STR
            )
        ) || (
            elem == BType::I16 &&
            to == BType::I16_PTR
        ) || (
            elem == BType::I32 &&
            to == BType::I32_PTR
        ) || (
            elem == BType::I64 &&
            to == BType::I64_PTR
        ) || (
            elem == BType::F32 &&
            to == BType::F32_PTR
        ) || (
            elem == BType::F64 &&
            to == BType::F64_PTR
        );
    }

    if (is_array_type(to)) {
        return pointer_types_compatible(
            to,
            from
        );
    }

    return from == to ||
           (
               from == BType::STR &&
               to == BType::I8_PTR
           ) ||
           (
               from == BType::I8_PTR &&
               to == BType::STR
           );
}

static std::string cast_pointer_value(
    LLVMEmitter& emitter,
    const std::string& value,
    BType from,
    BType to
) {
    if (from == to || from == BType::UNKNOWN || to == BType::UNKNOWN) {
        return value;
    }

    if (!pointer_types_compatible(from, to)) {
        return value;
    }

    const std::string from_type = llvm_value_type_for_btype(from);
    const std::string to_type = llvm_value_type_for_btype(to);

    if (from_type == to_type) {
        return value;
    }

    std::string converted = emitter.next_ssa();
    emitter.body << "  " << converted
                 << " = bitcast " << from_type << " " << value
                 << " to " << to_type << "\n";
    return converted;
}

static std::unique_ptr<CallExpr> make_operator_call(
    const std::string& op,
    const Expr* receiver,
    const Expr* operand = nullptr) {
    if (!receiver) return nullptr;

    auto call = std::make_unique<CallExpr>();
    call->callee = operator_method_name(op, operand ? 1 : 0);
    call->is_method_call = true;
    call->args.push_back(clone_expression(*receiver));
    if (operand) call->args.push_back(clone_expression(*operand));
    return call;
}

static void collect_return_expressions(const Stmt* stmt, std::vector<const Expr*>& returns) {
    if (!stmt) return;
    if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        if (ret->value) returns.push_back(ret->value.get());
    } else if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& child : block->statements) {
            collect_return_expressions(child.get(), returns);
        }
    } else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        collect_return_expressions(ifs->then_branch.get(), returns);
        collect_return_expressions(ifs->else_branch.get(), returns);
    } else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        for (const auto& entry : match->cases) {
            collect_return_expressions(entry.second.get(), returns);
        }
        collect_return_expressions(match->default_case.get(), returns);
    } else if (auto* fors = dynamic_cast<const ForStmt*>(stmt)) {
        collect_return_expressions(fors->body.get(), returns);
    } else if (auto* fws = dynamic_cast<const ForWhileStmt*>(stmt)) {
        collect_return_expressions(fws->body.get(), returns);
    }
}


std::string LLVMEmitter::get_struct_type_str(const std::string& struct_name) {
    return "%" + struct_name;
}

static TypeRef type_ref_or_legacy(const TypeRef& type_ref, BType type, const std::string& struct_name = "") {
    if (type_ref.base != BType::UNKNOWN || !type_ref.name.empty() ||
        !type_ref.type_args.empty() || type_ref.is_pointer || type_ref.is_array) {
        return type_ref;
    }

    TypeRef result;
    result.base = type;
    result.name = struct_name;
    return result;
}

static std::string get_function_struct_return_name(
    LLVMEmitter& emitter,
    const FnDecl& fn
) {
    if (!is_aggregate_type(fn.return_type) &&
        !is_aggregate_type(fn.return_type_ref.base)) {
        return "";
    }

    if (fn.return_type == BType::TUPLE ||
        fn.return_type_ref.base == BType::TUPLE) {
        return emitter.resolve_tuple_type(fn.return_type_ref);
    }

    std::string result =
        emitter.resolve_struct_type(fn.return_type_ref);

    if (result.empty()) {
        result = fn.return_type_annotation;
    }

    if (result.empty() &&
        fn.return_type_ref.base == BType::STRUCT) {
        result = fn.return_type_ref.name;
    }

    return result;
}

static std::unordered_map<const LLVMEmitter*,std::unordered_map<std::string, std::string>> g_struct_return_types;

static std::unordered_map<const LLVMEmitter*,std::string> g_current_struct_return_type;

static void remember_struct_return_type(
    LLVMEmitter& emitter,
    const FnDecl& fn
) {
    if (!is_aggregate_type(fn.return_type) &&
        !is_aggregate_type(fn.return_type_ref.base)) {
        return;
    }

    std::string struct_name =
        get_function_struct_return_name(emitter, fn);

    if (!struct_name.empty()) {
        g_struct_return_types[&emitter][fn.name] =
            struct_name;
    }
}

static void remember_parameter_passing_modes(
    LLVMEmitter& emitter,
    const FnDecl& fn
) {
    std::vector<bool> by_value;
    std::vector<std::string> struct_names;
    by_value.reserve(fn.params.size());
    struct_names.reserve(fn.params.size());

    for (const ParamDecl& parameter : fn.params) {
        const bool copies_struct =
            is_aggregate_type(parameter.type) &&
            parameter.type_ref.pass_by_value;
        by_value.push_back(copies_struct);

        std::string struct_name;
        if (is_aggregate_type(parameter.type)) {
            const TypeRef type_ref = type_ref_or_legacy(
                parameter.type_ref, parameter.type, parameter.struct_name);
            struct_name = parameter.type == BType::TUPLE
                ? emitter.resolve_tuple_type(type_ref)
                : emitter.resolve_struct_type(type_ref);
            if (struct_name.empty()) {
                struct_name = !parameter.struct_name.empty()
                    ? parameter.struct_name
                    : parameter.type_annotation;
                if (!struct_name.empty() && struct_name.back() == '!') {
                    struct_name.pop_back();
                }
            }
        }
        struct_names.push_back(std::move(struct_name));
    }

    emitter.func_param_by_value[fn.name] = std::move(by_value);
    emitter.func_param_struct_names[fn.name] = std::move(struct_names);
}

static bool has_unresolved_type_param(const TypeRef& type_ref) {
    if (type_ref.base == BType::UNKNOWN && !type_ref.name.empty()) return true;
    for (const auto& arg : type_ref.type_args) {
        if (has_unresolved_type_param(arg)) return true;
    }
    return false;
}

static TypeRef substitute_type_ref(
    const TypeRef& type_ref,
    const std::unordered_map<std::string, TypeRef>& substitutions) {
    if (type_ref.base == BType::UNKNOWN && !type_ref.name.empty()) {
        auto it = substitutions.find(type_ref.name);
        if (it != substitutions.end()) {
            TypeRef result = it->second;
            result.is_pointer = result.is_pointer || type_ref.is_pointer;
            result.is_array = result.is_array || type_ref.is_array;
            result.pass_by_value =
                result.pass_by_value || type_ref.pass_by_value;
            return result;
        }
    }

    TypeRef result = type_ref;
    result.type_args.clear();
    for (const auto& arg : type_ref.type_args) {
        result.type_args.push_back(substitute_type_ref(arg, substitutions));
    }
    return result;
}

std::string LLVMEmitter::resolve_struct_type(const TypeRef& type_ref) {
    if (type_ref.base != BType::STRUCT || type_ref.name.empty() ||
        has_unresolved_type_param(type_ref)) {
        return "";
    }

    if (type_ref.type_args.empty()) return type_ref.name;

    std::string instance_name = mangle_template_struct_name(type_ref.name, type_ref.type_args);
    if (structs.count(instance_name)) return instance_name;

    auto template_it = struct_templates.find(type_ref.name);
    if (template_it == struct_templates.end()) {
        std::cerr << "Unknown template struct: " << type_ref.name << "\n";
        return "";
    }

    const StructDecl& templ = *template_it->second;
    if (templ.type_params.size() != type_ref.type_args.size()) {
        std::cerr << "Template struct '" << type_ref.name << "' expects "
                  << templ.type_params.size() << " type arguments, got "
                  << type_ref.type_args.size() << "\n";
        return "";
    }

    std::unordered_map<std::string, TypeRef> substitutions;
    for (size_t i = 0; i < templ.type_params.size(); ++i) {
        substitutions[templ.type_params[i]] = type_ref.type_args[i];
    }

    LLVMStructInfo info;
    info.name = instance_name;
    info.template_name = type_ref.name;
    info.template_args = type_ref.type_args;
    for (const auto& field : templ.fields) {
        TypeRef field_type_ref = substitute_type_ref(
            type_ref_or_legacy(field.type_ref, field.type, field.struct_name), substitutions);
        BType field_type = type_ref_to_btype(field_type_ref);

        info.field_names.push_back(field.name);
        info.field_types.push_back(field_type);
        info.field_indices[field.name] = info.field_names.size() - 1;

        if (field_type_ref.base == BType::STRUCT ||
            field_type_ref.base == BType::TUPLE) {
            TypeRef aggregate_ref = field_type_ref;
            aggregate_ref.is_array = false;
            std::string field_struct_name =
                field_type_ref.base == BType::TUPLE
                    ? resolve_tuple_type(aggregate_ref)
                    : resolve_struct_type(aggregate_ref);
            if (field_struct_name.empty()) field_struct_name = field_type_ref.name;
            info.field_annotations.push_back(field_struct_name);
            info.field_inline_struct_arrays.push_back(
                field_type_ref.is_array &&
                inline_struct_array_types.count(field_struct_name));
        } else {
            info.field_annotations.push_back(type_ref_to_string(field_type_ref));
            info.field_inline_struct_arrays.push_back(false);
        }
    }

    structs[instance_name] = std::move(info);
    return instance_name;
}

std::string LLVMEmitter::resolve_tuple_type(const TypeRef& type_ref) {
    if (type_ref.base != BType::TUPLE || type_ref.type_args.empty() ||
        type_ref.is_pointer || type_ref.is_array) {
        return "";
    }

    const std::string tuple_name = "__ferra_tuple__" +
        mangle_type_ref(type_ref);
    if (structs.count(tuple_name)) return tuple_name;

    LLVMStructInfo info;
    info.name = tuple_name;
    info.is_tuple = true;
    info.tuple_element_types = type_ref.type_args;

    for (size_t i = 0; i < type_ref.type_args.size(); ++i) {
        const TypeRef& element = type_ref.type_args[i];
        const BType element_type = type_ref_to_btype(element);
        if (element_type == BType::UNKNOWN) {
            gerror("Cannot infer a concrete type for tuple element " +
                   std::to_string(i) + " :/\n");
            return "";
        }

        info.field_types.push_back(element_type);
        info.field_names.push_back(std::to_string(i));
        info.field_indices[info.field_names.back()] = i;
        info.field_inline_struct_arrays.push_back(false);

        if (element.base == BType::STRUCT && !element.is_pointer &&
            !element.is_array) {
            std::string nested = resolve_struct_type(element);
            info.field_annotations.push_back(
                nested.empty() ? element.name : nested);
        } else if (element.base == BType::TUPLE && !element.is_pointer &&
                   !element.is_array) {
            std::string nested = resolve_tuple_type(element);
            if (nested.empty()) return "";
            info.field_annotations.push_back(nested);
        } else {
            info.field_annotations.push_back(type_ref_to_string(element));
        }
    }

    structs[tuple_name] = std::move(info);
    return tuple_name;
}

static TypeRef tuple_type_ref_from_expr(LLVMEmitter& emitter, const Expr* expr) {
    if (!expr) return {};

    if (auto expected = emitter.expected_tuple_types.find(expr);
        expected != emitter.expected_tuple_types.end()) {
        return expected->second;
    }

    if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        TypeRef result;
        result.base = BType::TUPLE;
        for (const auto& element : tuple->elements) {
            TypeRef element_type = tuple_type_ref_from_expr(emitter, element.get());
            if (element_type.base == BType::UNKNOWN && element_type.name.empty()) {
                element_type.base = emitter.get_expr_type(element.get());
            }
            result.type_args.push_back(std::move(element_type));
        }
        return result;
    }

    if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        return literal->type_ref;
    }

    if (auto* variable = dynamic_cast<const VariableExpr*>(expr)) {
        auto value = emitter.vars.find(variable->name);
        if (value != emitter.vars.end()) {
            TypeRef result;
            result.base = value->second.source_type == BType::UNKNOWN
                ? ir_to_btype(value->second.type)
                : value->second.source_type;
            if (result.base == BType::STRUCT) result.name = value->second.struct_name;
            if (result.base == BType::TUPLE) {
                auto tuple = emitter.structs.find(value->second.struct_name);
                if (tuple != emitter.structs.end()) {
                    result.type_args = tuple->second.tuple_element_types;
                }
            }
            return result;
        }
    }

    const BType base = emitter.get_expr_type(expr);
    TypeRef result;
    result.base = base;
    if (base == BType::STRUCT || base == BType::TUPLE) {
        const std::string name = emitter.get_expr_struct_name(expr);
        auto aggregate = emitter.structs.find(name);
        if (base == BType::STRUCT) result.name = name;
        if (base == BType::TUPLE && aggregate != emitter.structs.end()) {
            result.type_args = aggregate->second.tuple_element_types;
        }
    }
    return result;
}

static void collect_struct_uses_from_expr(const Expr* expr, LLVMEmitter& emitter) {
    if (!expr) return;

    if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        emitter.resolve_struct_type(literal->type_ref);
        for (const auto& field : literal->fields) {
            collect_struct_uses_from_expr(field.value.get(), emitter);
        }
    } else if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        for (const auto& element : tuple->elements) {
            collect_struct_uses_from_expr(element.get(), emitter);
        }
    } else if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        for (const auto& arg : call->args) {
            collect_struct_uses_from_expr(arg.get(), emitter);
        }
    } else if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        collect_struct_uses_from_expr(binary->left.get(), emitter);
        collect_struct_uses_from_expr(binary->right.get(), emitter);
    } else if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        collect_struct_uses_from_expr(unary->operand.get(), emitter);
    } else if (auto* ternary = dynamic_cast<const TernaryExpr*>(expr)) {
        collect_struct_uses_from_expr(ternary->cond.get(), emitter);
        collect_struct_uses_from_expr(ternary->then_expr.get(), emitter);
        collect_struct_uses_from_expr(ternary->else_expr.get(), emitter);
    } else if (auto* array = dynamic_cast<const ArrayExpr*>(expr)) {
        for (const auto& element : array->elements) {
            collect_struct_uses_from_expr(element.get(), emitter);
        }
    } else if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        collect_struct_uses_from_expr(index->object.get(), emitter);
        collect_struct_uses_from_expr(index->index.get(), emitter);
    } else if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        collect_struct_uses_from_expr(member->object.get(), emitter);
    } else if (auto* size = dynamic_cast<const SizeofExpr*>(expr)) {
        collect_struct_uses_from_expr(size->expr.get(), emitter);
    } else if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        collect_struct_uses_from_expr(ref->operand.get(), emitter);
    } else if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        collect_struct_uses_from_expr(deref->operand.get(), emitter);
    } else if (auto* cast = dynamic_cast<const AsExpr*>(expr)) {
        collect_struct_uses_from_expr(cast->operand.get(), emitter);
    }
}

static void collect_struct_uses_from_stmt(const Stmt* stmt, LLVMEmitter& emitter) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& child : block->statements) {
            collect_struct_uses_from_stmt(child.get(), emitter);
        }
    } else if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt)) {
        const std::string declared_struct = emitter.resolve_struct_type(
            type_ref_or_legacy(var->type_ref, var->type, var->struct_name));
        if (!var->array_size && var->initializer &&
            var->type_ref.base == BType::STRUCT &&
            var->type_ref.is_array &&
            !var->type_ref.is_pointer &&
            !declared_struct.empty()) {
            emitter.inline_struct_array_types.insert(declared_struct);
        }
        collect_struct_uses_from_expr(var->initializer.get(), emitter);
        for (const auto& argument : var->constructor_args) {
            collect_struct_uses_from_expr(argument.get(), emitter);
        }
        collect_struct_uses_from_expr(var->array_size.get(), emitter);
    } else if (auto* destructure = dynamic_cast<const TupleDestructureStmt*>(stmt)) {
        collect_struct_uses_from_expr(destructure->initializer.get(), emitter);
    } else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        collect_struct_uses_from_expr(ifs->condition.get(), emitter);
        collect_struct_uses_from_stmt(ifs->then_branch.get(), emitter);
        collect_struct_uses_from_stmt(ifs->else_branch.get(), emitter);
    } else if (auto* fors = dynamic_cast<const ForStmt*>(stmt)) {
        collect_struct_uses_from_expr(fors->bound.get(), emitter);
        collect_struct_uses_from_stmt(fors->body.get(), emitter);
    } else if (auto* fws = dynamic_cast<const ForWhileStmt*>(stmt)) {
        collect_struct_uses_from_expr(fws->condition.get(), emitter);
        collect_struct_uses_from_stmt(fws->body.get(), emitter);
    } else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        collect_struct_uses_from_expr(match->value.get(), emitter);
        for (const auto& entry : match->cases) {
            collect_struct_uses_from_expr(entry.first.get(), emitter);
            collect_struct_uses_from_stmt(entry.second.get(), emitter);
        }
        collect_struct_uses_from_stmt(match->default_case.get(), emitter);
    } else if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        collect_struct_uses_from_expr(ret->value.get(), emitter);
    } else if (auto* drop_now = dynamic_cast<const DropNowStmt*>(stmt)) {
        collect_struct_uses_from_expr(drop_now->value.get(), emitter);
    } else if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        collect_struct_uses_from_expr(assign->value.get(), emitter);
    } else if (auto* assign = dynamic_cast<const ArrayAssignStmt*>(stmt)) {
        collect_struct_uses_from_expr(assign->index.get(), emitter);
        collect_struct_uses_from_expr(assign->value.get(), emitter);
    } else if (auto* assign = dynamic_cast<const MemberAssignStmt*>(stmt)) {
        collect_struct_uses_from_expr(assign->lhs.get(), emitter);
        collect_struct_uses_from_expr(assign->value.get(), emitter);
    } else if (auto* assign = dynamic_cast<const DerefAssignStmt*>(stmt)) {
        collect_struct_uses_from_expr(assign->pointer.get(), emitter);
        collect_struct_uses_from_expr(assign->value.get(), emitter);
    } else if (auto* expression = dynamic_cast<const ExprStmt*>(stmt)) {
        collect_struct_uses_from_expr(expression->expression.get(), emitter);
    }
}

void LLVMEmitter::collect_structs(const Program& prog) {
    for (const auto& stmt : prog.statements) {
        if (auto* sd = dynamic_cast<const StructDecl*>(stmt.get())) {
            if (!sd->type_params.empty()) {
                struct_templates[sd->name] = sd;
            }
        }
    }

    for (const auto& stmt : prog.statements) {
        auto* sd = dynamic_cast<const StructDecl*>(stmt.get());
        if (!sd || !sd->type_params.empty()) continue;

        LLVMStructInfo info;
        info.name = sd->name;
        info.template_name = sd->name;
        info.is_opaque = sd->is_extern;
        for (const auto& field : sd->fields) {
            TypeRef field_type_ref = type_ref_or_legacy(field.type_ref, field.type, field.struct_name);
            BType field_type = type_ref_to_btype(field_type_ref);

            info.field_types.push_back(field_type);
            info.field_names.push_back(field.name);
            info.field_indices[field.name] = info.field_names.size() - 1;

            if (field_type_ref.base == BType::STRUCT ||
                field_type_ref.base == BType::TUPLE) {
                TypeRef aggregate_ref = field_type_ref;
                aggregate_ref.is_array = false;
                std::string field_struct_name =
                    field_type_ref.base == BType::TUPLE
                        ? resolve_tuple_type(aggregate_ref)
                        : resolve_struct_type(aggregate_ref);
                if (field_struct_name.empty()) field_struct_name = field_type_ref.name;
                info.field_annotations.push_back(field_struct_name);
                info.field_inline_struct_arrays.push_back(false);
            } else {
                info.field_annotations.push_back(type_ref_to_string(field_type_ref));
                info.field_inline_struct_arrays.push_back(false);
            }
        }
        auto existing = structs.find(sd->name);
        if (sd->is_extern && existing != structs.end() &&
            !existing->second.is_opaque) {
            continue;
        }
        structs[sd->name] = std::move(info);
    }

    for (const auto& fn : prog.functions) {
        for (const auto& param : fn->params) {
            const TypeRef type_ref = type_ref_or_legacy(
                param.type_ref, param.type, param.struct_name);
            if (!has_unresolved_type_param(type_ref)) {
                resolve_struct_type(type_ref);
                resolve_tuple_type(type_ref);
            }
        }
        if (!has_unresolved_type_param(fn->return_type_ref)) {
            resolve_struct_type(fn->return_type_ref);
            resolve_tuple_type(fn->return_type_ref);
        }
        collect_struct_uses_from_stmt(fn->body.get(), *this);
    }
    for (const auto& stmt : prog.statements) {
        collect_struct_uses_from_stmt(stmt.get(), *this);
    }

    for (auto& [unused_name, info] : structs) {
        info.field_inline_struct_arrays.resize(
            info.field_types.size(), false);
        for (size_t i = 0; i < info.field_types.size(); ++i) {
            if (is_array_type(info.field_types[i]) &&
                i < info.field_annotations.size() &&
                inline_struct_array_types.count(
                    info.field_annotations[i])) {
                info.field_inline_struct_arrays[i] = true;
            }
        }
    }
}

void LLVMEmitter::emit_struct_defs() {
    bool changed = true;

    while (changed) {
        changed = false;

        for (const auto& [name, info] : structs) {
            if (
                struct_defs.str().find(
                    "%" + name + " = type"
                ) != std::string::npos
            ) {
                continue;
            }

            if (info.is_opaque) {
                struct_defs << "%" << name << " = type opaque\n";
                changed = true;
                continue;
            }

            bool has_unresolved_struct_field = false;

            for (size_t i = 0; i < info.field_types.size(); i++) {
                const bool direct_struct = is_aggregate_type(info.field_types[i]);
                if (!direct_struct) {
                    continue;
                }

                const std::string& dep_name =
                    info.field_annotations[i];

                if (
                    struct_defs.str().find(
                        "%" + dep_name + " = type"
                    ) == std::string::npos
                ) {
                    has_unresolved_struct_field = true;
                    break;
                }
            }

            if (has_unresolved_struct_field) {
                continue;
            }

            struct_defs << "%" << name << " = type { ";

            for (size_t i = 0; i < info.field_types.size(); i++) {
                if (i > 0) {
                    struct_defs << ", ";
                }

                BType field_type = info.field_types[i];

                if (is_aggregate_type(field_type)) {
                    if (
                        i < info.field_annotations.size() &&
                        !info.field_annotations[i].empty()
                    ) {
                        struct_defs
                            << "%"
                            << info.field_annotations[i];
                    } else {
                        struct_defs << "i64";
                    }
                } else if (is_array_type(field_type) &&
                           i < info.field_annotations.size() &&
                           structs.count(info.field_annotations[i])) {
                    const bool inline_elements =
                        i < info.field_inline_struct_arrays.size() &&
                        info.field_inline_struct_arrays[i];
                    struct_defs << "%" << info.field_annotations[i]
                                << (inline_elements ? "*" : "**");
                } else {
                    struct_defs << get_llvm_type(field_type);
                }
            }

            struct_defs << " }\n";
            changed = true;
        }
    }
}

void LLVMEmitter::emit_string_literal(const std::string& s) {
    int len = s.length() + 1; 
    std::string escaped;
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if (c >= 0x20 && c <= 0x7e && c != '"' && c != '\\') {
            escaped += static_cast<char>(c);
        } else {
            escaped += '\\';
            escaped += hex[(c >> 4) & 0x0f];
            escaped += hex[c & 0x0f];
        }
    }
    escaped += "\\00";
    
    std::string name = next_str_name();
    globals << name << " = private constant [" << len << " x i8] c\"" << escaped << "\"\n";
}

std::string LLVMEmitter::emit_array_literal(const ArrayExpr* array, BType element_type) {
    if (!array) return "0";
    if (element_type == BType::UNKNOWN) element_type = BType::INT;

    const size_t element_count = array->elements.size();
    const std::string element_llvm_type = get_llvm_type(element_type);

    std::string array_ptr = next_ssa();
    body << "  " << array_ptr << " = alloca [" << element_count << " x "
         << element_llvm_type << "]\n";

    for (size_t i = 0; i < element_count; ++i) {
        std::string element_value = emit_expression(array->elements[i].get());
        BType source_btype = get_expr_type(array->elements[i].get());
        if (source_btype == BType::UNKNOWN) {
            source_btype = array->elements[i]->btype;
        }
        IRType source_ir_type = btype_to_ir(source_btype);
        IRType target_ir_type = btype_to_ir(element_type);
        if (source_ir_type == IRType::UNKNOWN) source_ir_type = target_ir_type;
        if (source_ir_type != target_ir_type &&
            !coerce_ir_value(*this, element_value, source_ir_type,
                             target_ir_type, "  ",
                             is_unsigned_integer_type(source_btype))) {
            gerror("Cannot initialize array element with an incompatible value :/\n");
            element_value = "0";
        }
        std::string element_ptr = next_ssa();
        body << "  " << element_ptr << " = getelementptr inbounds [" << element_count
             << " x " << element_llvm_type << "], [" << element_count << " x "
             << element_llvm_type << "]* " << array_ptr << ", i32 0, i32 " << i << "\n";
        body << "  store " << element_llvm_type << " " << element_value << ", "
             << element_llvm_type << "* " << element_ptr << "\n";
    }

    std::string first_element_ptr = next_ssa();
    body << "  " << first_element_ptr << " = getelementptr inbounds [" << element_count
         << " x " << element_llvm_type << "], [" << element_count << " x "
         << element_llvm_type << "]* " << array_ptr << ", i32 0, i32 0\n";
    return first_element_ptr;
}

static void scan_expr_for_templates(const Expr* expr, LLVMEmitter& emitter);

static bool function_uses_native_void_abi(const FnDecl& fn) {
    return fn.is_drop ||
           (fn.is_extern && fn.return_type == BType::VOID) ||
           (fn.is_method && !fn.method_owner.empty() &&
            fn.method_name == fn.method_owner);
}

static BType inferred_expression_type(LLVMEmitter& emitter, const Expr* expr) {
    if (!expr) return BType::UNKNOWN;

    if (auto* array = dynamic_cast<const ArrayExpr*>(expr)) {
        BType element_type = BType::UNKNOWN;
        for (const auto& element : array->elements) {
            BType current = inferred_expression_type(emitter, element.get());
            if (current == BType::UNKNOWN) continue;
            if (element_type == BType::UNKNOWN) {
                element_type = current;
            } else if (element_type != current) {
                if (element_type == BType::F64 || current == BType::F64) {
                    element_type = BType::F64;
                } else if (element_type == BType::F32 || current == BType::F32) {
                    element_type = BType::F32;
                } else if (btype_to_ir(element_type) != btype_to_ir(current)) {
                    return BType::ARR;
                }
            }
        }
        return element_type == BType::UNKNOWN
            ? BType::ARR
            : array_type_for(element_type);
    }

    BType result = emitter.get_expr_type(expr);
    return result == BType::UNKNOWN ? expr->btype : result;
}

static void bind_inferred_value(
    LLVMEmitter& emitter,
    const std::string& name,
    BType type,
    const std::string& struct_name = ""
) {
    if (type == BType::UNKNOWN) return;

    LLVMVar value{name, "%" + name + "_inferred", btype_to_ir(type),
                  BType::UNKNOWN, 0};
    value.source_type = type;

    if (is_struct_type(type)) {
        value.type = IRType::STRUCT;
        value.struct_name = struct_name;
        value.struct_pointer_slot = true;
    } else if (is_array_type(type)) {
        value.type = IRType::ARR;
        value.elem_type = get_array_elem_type(type);
    } else if (type == BType::PTR || is_pointer_type(type)) {
        value.elem_type = get_pointer_base_type(type);
    }

    emitter.vars[name] = std::move(value);
}

static void set_inferred_type(
    ParamDecl& parameter,
    BType type,
    const std::string& struct_name = ""
) {
    parameter.type = type;
    parameter.type_ref = TypeRef{};
    parameter.type_ref.base = type;
    parameter.struct_name.clear();
    if (type == BType::STRUCT) {
        parameter.type_ref.name = struct_name;
        parameter.struct_name = struct_name;
        parameter.type_annotation = struct_name;
    } else {
        parameter.type_annotation = type_name(type);
    }
}

static void set_inferred_return_type(
    FnDecl& function,
    BType type,
    const std::string& struct_name = ""
) {
    function.return_type = type;
    function.return_type_ref = TypeRef{};
    function.return_type_ref.base = type;
    if (type == BType::STRUCT) {
        function.return_type_ref.name = struct_name;
        function.return_type_annotation = struct_name;
    } else {
        function.return_type_annotation = type_name(type);
    }
}

static bool merge_inferred_type(BType& known, BType candidate) {
    if (candidate == BType::UNKNOWN) return true;
    if (known == BType::UNKNOWN) {
        known = candidate;
        return true;
    }
    if (known == candidate || btype_to_ir(known) == btype_to_ir(candidate)) {
        return true;
    }
    if (known == BType::F64 || candidate == BType::F64) {
        known = BType::F64;
        return true;
    }
    if (known == BType::F32 || candidate == BType::F32) {
        known = BType::F32;
        return true;
    }
    return false;
}

static bool supports_signature_inference(const FnDecl& function) {
    return !function.is_extern && function.type_params.empty() &&
           !function.is_operator && !function.is_drop &&
           !(function.is_method &&
             function.method_name == function.method_owner);
}

static void refresh_inferred_function_tables(
    LLVMEmitter& emitter,
    const Program& program
) {
    for (const auto& function : program.functions) {
        if (!function->type_params.empty()) continue;

        emitter.func_types[function->name] =
            function_uses_native_void_abi(*function)
                ? IRType::VOID
                : btype_to_ir(function->return_type);
        emitter.func_return_btypes[function->name] = function->return_type;

        std::vector<IRType> argument_types;
        argument_types.reserve(function->params.size());
        for (const ParamDecl& parameter : function->params) {
            argument_types.push_back(btype_to_ir(parameter.type));
        }
        emitter.func_arg_types[function->name] = std::move(argument_types);
    }
}

static void infer_call_argument_types(
    const Expr* expr,
    LLVMEmitter& emitter,
    const std::unordered_map<std::string, FnDecl*>& functions,
    bool& changed
);

static void infer_call_argument_types_from_stmt(
    const Stmt* stmt,
    LLVMEmitter& emitter,
    const std::unordered_map<std::string, FnDecl*>& functions,
    bool& changed
) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        const auto values_before = emitter.vars;
        for (const auto& child : block->statements) {
            infer_call_argument_types_from_stmt(
                child.get(), emitter, functions, changed);
        }
        if (!block->is_declaration_group) emitter.vars = values_before;
    } else if (auto* variable = dynamic_cast<const VarDeclStmt*>(stmt)) {
        infer_call_argument_types(variable->initializer.get(), emitter, functions, changed);
        for (const auto& argument : variable->constructor_args) {
            infer_call_argument_types(argument.get(), emitter, functions, changed);
        }
        BType type = variable->type;
        if (type == BType::UNKNOWN) {
            type = inferred_expression_type(emitter, variable->initializer.get());
        }
        std::string struct_name = variable->struct_name;
        if (type == BType::STRUCT && struct_name.empty()) {
            struct_name = emitter.get_expr_struct_name(variable->initializer.get());
        }
        bind_inferred_value(emitter, variable->name, type, struct_name);
    } else if (auto* branch = dynamic_cast<const IfStmt*>(stmt)) {
        infer_call_argument_types(branch->condition.get(), emitter, functions, changed);
        infer_call_argument_types_from_stmt(branch->then_branch.get(), emitter, functions, changed);
        infer_call_argument_types_from_stmt(branch->else_branch.get(), emitter, functions, changed);
    } else if (auto* loop = dynamic_cast<const ForStmt*>(stmt)) {
        infer_call_argument_types(loop->bound.get(), emitter, functions, changed);
        const auto values_before = emitter.vars;
        bind_inferred_value(emitter, loop->var_name, loop->var_type);
        infer_call_argument_types_from_stmt(loop->body.get(), emitter, functions, changed);
        emitter.vars = values_before;
    } else if (auto* loop = dynamic_cast<const ForWhileStmt*>(stmt)) {
        infer_call_argument_types(loop->condition.get(), emitter, functions, changed);
        infer_call_argument_types_from_stmt(loop->body.get(), emitter, functions, changed);
    } else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        infer_call_argument_types(match->value.get(), emitter, functions, changed);
        for (const auto& entry : match->cases) {
            infer_call_argument_types(entry.first.get(), emitter, functions, changed);
            infer_call_argument_types_from_stmt(entry.second.get(), emitter, functions, changed);
        }
        infer_call_argument_types_from_stmt(match->default_case.get(), emitter, functions, changed);
    } else if (auto* result = dynamic_cast<const ReturnStmt*>(stmt)) {
        infer_call_argument_types(result->value.get(), emitter, functions, changed);
    } else if (auto* drop = dynamic_cast<const DropNowStmt*>(stmt)) {
        infer_call_argument_types(drop->value.get(), emitter, functions, changed);
    } else if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        infer_call_argument_types(assign->value.get(), emitter, functions, changed);
    } else if (auto* assign = dynamic_cast<const ArrayAssignStmt*>(stmt)) {
        infer_call_argument_types(assign->index.get(), emitter, functions, changed);
        infer_call_argument_types(assign->value.get(), emitter, functions, changed);
    } else if (auto* assign = dynamic_cast<const MemberAssignStmt*>(stmt)) {
        infer_call_argument_types(assign->lhs.get(), emitter, functions, changed);
        infer_call_argument_types(assign->value.get(), emitter, functions, changed);
    } else if (auto* assign = dynamic_cast<const DerefAssignStmt*>(stmt)) {
        infer_call_argument_types(assign->pointer.get(), emitter, functions, changed);
        infer_call_argument_types(assign->value.get(), emitter, functions, changed);
    } else if (auto* expression = dynamic_cast<const ExprStmt*>(stmt)) {
        infer_call_argument_types(expression->expression.get(), emitter, functions, changed);
    }
}

static void infer_call_argument_types(
    const Expr* expr,
    LLVMEmitter& emitter,
    const std::unordered_map<std::string, FnDecl*>& functions,
    bool& changed
) {
    if (!expr) return;

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        for (const auto& argument : call->args) {
            infer_call_argument_types(argument.get(), emitter, functions, changed);
        }

        std::string callee = call->callee;
        if (call->is_method_call &&
            !emitter.resolve_call_target(call, callee, false)) {
            return;
        }
        auto target = functions.find(callee);
        if (target == functions.end()) return;

        FnDecl& function = *target->second;
        const size_t parameter_count = std::min(
            call->args.size(), function.params.size());
        for (size_t index = 0; index < parameter_count; ++index) {
            ParamDecl& parameter = function.params[index];
            if (parameter.type != BType::UNKNOWN) continue;

            BType inferred = inferred_expression_type(
                emitter, call->args[index].get());
            if (inferred == BType::UNKNOWN) continue;

            BType merged = parameter.type;
            if (!merge_inferred_type(merged, inferred)) {
                gerror("Cannot infer one type for parameter '" + parameter.name +
                       "' in function '" + function.name + "' :/\n");
                continue;
            }
            if (merged == parameter.type) continue;

            std::string struct_name;
            if (merged == BType::STRUCT) {
                struct_name = emitter.get_expr_struct_name(call->args[index].get());
                if (struct_name.empty()) continue;
            }
            set_inferred_type(parameter, merged, struct_name);
            changed = true;
        }
        return;
    }

    if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        infer_call_argument_types(binary->left.get(), emitter, functions, changed);
        infer_call_argument_types(binary->right.get(), emitter, functions, changed);
    } else if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        infer_call_argument_types(unary->operand.get(), emitter, functions, changed);
    } else if (auto* ternary = dynamic_cast<const TernaryExpr*>(expr)) {
        infer_call_argument_types(ternary->cond.get(), emitter, functions, changed);
        infer_call_argument_types(ternary->then_expr.get(), emitter, functions, changed);
        infer_call_argument_types(ternary->else_expr.get(), emitter, functions, changed);
    } else if (auto* array = dynamic_cast<const ArrayExpr*>(expr)) {
        for (const auto& element : array->elements) {
            infer_call_argument_types(element.get(), emitter, functions, changed);
        }
    } else if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        for (const auto& field : literal->fields) {
            infer_call_argument_types(field.value.get(), emitter, functions, changed);
        }
    } else if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        infer_call_argument_types(index->object.get(), emitter, functions, changed);
        infer_call_argument_types(index->index.get(), emitter, functions, changed);
    } else if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        infer_call_argument_types(member->object.get(), emitter, functions, changed);
    } else if (auto* size = dynamic_cast<const SizeofExpr*>(expr)) {
        infer_call_argument_types(size->expr.get(), emitter, functions, changed);
    } else if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        infer_call_argument_types(ref->operand.get(), emitter, functions, changed);
    } else if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        infer_call_argument_types(deref->operand.get(), emitter, functions, changed);
    } else if (auto* cast = dynamic_cast<const AsExpr*>(expr)) {
        infer_call_argument_types(cast->operand.get(), emitter, functions, changed);
    }
}

static void collect_inferred_return_types(
    const Stmt* stmt,
    LLVMEmitter& emitter,
    BType& inferred,
    std::string& inferred_struct_name,
    bool& has_return,
    bool& conflict
) {
    if (!stmt) return;

    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        const auto values_before = emitter.vars;
        for (const auto& child : block->statements) {
            collect_inferred_return_types(child.get(), emitter, inferred,
                                          inferred_struct_name, has_return, conflict);
        }
        if (!block->is_declaration_group) emitter.vars = values_before;
    } else if (auto* variable = dynamic_cast<const VarDeclStmt*>(stmt)) {
        BType type = variable->type;
        if (type == BType::UNKNOWN) {
            type = inferred_expression_type(emitter, variable->initializer.get());
        }
        std::string struct_name = variable->struct_name;
        if (type == BType::STRUCT && struct_name.empty()) {
            struct_name = emitter.get_expr_struct_name(variable->initializer.get());
        }
        bind_inferred_value(emitter, variable->name, type, struct_name);
    } else if (auto* result = dynamic_cast<const ReturnStmt*>(stmt)) {
        has_return = true;
        BType current = inferred_expression_type(emitter, result->value.get());
        if (!merge_inferred_type(inferred, current)) {
            conflict = true;
            return;
        }
        if (inferred == BType::STRUCT && inferred_struct_name.empty()) {
            inferred_struct_name = emitter.get_expr_struct_name(result->value.get());
        }
    } else if (auto* branch = dynamic_cast<const IfStmt*>(stmt)) {
        collect_inferred_return_types(branch->then_branch.get(), emitter, inferred,
                                      inferred_struct_name, has_return, conflict);
        collect_inferred_return_types(branch->else_branch.get(), emitter, inferred,
                                      inferred_struct_name, has_return, conflict);
    } else if (auto* loop = dynamic_cast<const ForStmt*>(stmt)) {
        const auto values_before = emitter.vars;
        bind_inferred_value(emitter, loop->var_name, loop->var_type);
        collect_inferred_return_types(loop->body.get(), emitter, inferred,
                                      inferred_struct_name, has_return, conflict);
        emitter.vars = values_before;
    } else if (auto* loop = dynamic_cast<const ForWhileStmt*>(stmt)) {
        collect_inferred_return_types(loop->body.get(), emitter, inferred,
                                      inferred_struct_name, has_return, conflict);
    } else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        for (const auto& entry : match->cases) {
            collect_inferred_return_types(entry.second.get(), emitter, inferred,
                                          inferred_struct_name, has_return, conflict);
        }
        collect_inferred_return_types(match->default_case.get(), emitter, inferred,
                                      inferred_struct_name, has_return, conflict);
    }
}

static void infer_unannotated_function_signatures(
    Program& program,
    LLVMEmitter& emitter
) {
    std::unordered_map<std::string, FnDecl*> functions;
    for (const auto& function : program.functions) {
        if (supports_signature_inference(*function)) {
            functions[function->name] = function.get();
        }
    }

    
    
    for (size_t pass = 0; pass < 16; ++pass) {
        refresh_inferred_function_tables(emitter, program);
        bool changed = false;

        for (const auto& function : program.functions) {
            if (!function->type_params.empty()) continue;
            emitter.vars.clear();
            for (const ParamDecl& parameter : function->params) {
                bind_inferred_value(emitter, parameter.name, parameter.type,
                                    parameter.struct_name);
            }
            current_function_name = function->name;
            infer_call_argument_types_from_stmt(
                function->body.get(), emitter, functions, changed);
        }
        emitter.vars.clear();
        for (const auto& statement : program.statements) {
            infer_call_argument_types_from_stmt(
                statement.get(), emitter, functions, changed);
        }

        refresh_inferred_function_tables(emitter, program);
        for (const auto& function : program.functions) {
            if (!supports_signature_inference(*function) ||
                function->return_type != BType::UNKNOWN) {
                continue;
            }

            emitter.vars.clear();
            for (const ParamDecl& parameter : function->params) {
                bind_inferred_value(emitter, parameter.name, parameter.type,
                                    parameter.struct_name);
            }
            current_function_name = function->name;

            BType inferred = BType::UNKNOWN;
            std::string struct_name;
            bool has_return = false;
            bool conflict = false;
            collect_inferred_return_types(function->body.get(), emitter, inferred,
                                          struct_name, has_return, conflict);
            if (conflict) {
                gerror("Cannot infer one return type for function '" +
                       function->name + "' :/\n");
                continue;
            }
            if (!has_return) {
                set_inferred_return_type(*function, BType::VOID);
                changed = true;
            } else if (inferred != BType::UNKNOWN &&
                       (inferred != BType::STRUCT || !struct_name.empty())) {
                set_inferred_return_type(*function, inferred, struct_name);
                changed = true;
            }
        }

        if (!changed) break;
    }

    refresh_inferred_function_tables(emitter, program);
    for (const auto& function : program.functions) {
        if (!supports_signature_inference(*function)) continue;
        for (const ParamDecl& parameter : function->params) {
            if (parameter.type == BType::UNKNOWN) {
                gerror("Cannot infer type for parameter '" + parameter.name +
                       "' in function '" + function->name +
                       "'; add ': type' :/\n");
            }
        }
        if (function->return_type == BType::UNKNOWN) {
            gerror("Cannot infer return type for function '" + function->name +
                   "'; add ': type' :/\n");
        }
    }
    emitter.vars.clear();
}

static void scan_and_instantiate_templates(const Stmt* stmt, LLVMEmitter& emitter) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& s : block->statements) {
            scan_and_instantiate_templates(s.get(), emitter);
        }
    }
    else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        bool condition = false;
        if (evaluate_compile_time_condition(ifs->condition.get(), condition)) {
            scan_and_instantiate_templates(
                condition ? ifs->then_branch.get() : ifs->else_branch.get(), emitter);
        } else {
            scan_and_instantiate_templates(ifs->then_branch.get(), emitter);
            scan_and_instantiate_templates(ifs->else_branch.get(), emitter);
        }
    }
    else if (auto* fors = dynamic_cast<const ForStmt*>(stmt)) {
        scan_and_instantiate_templates(fors->body.get(), emitter);
    }
    else if (auto* fws = dynamic_cast<const ForWhileStmt*>(stmt)) {
        scan_and_instantiate_templates(fws->body.get(), emitter);
    }
    else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        for (const auto& c : match->cases) {
            scan_and_instantiate_templates(c.second.get(), emitter);
        }
        scan_and_instantiate_templates(match->default_case.get(), emitter);
    }
    else if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        if (ret->value) {
            scan_expr_for_templates(ret->value.get(), emitter);
        }
    }
    else if (auto* drop_now = dynamic_cast<const DropNowStmt*>(stmt)) {
        scan_expr_for_templates(drop_now->value.get(), emitter);
    }
    else if (auto* destructure = dynamic_cast<const TupleDestructureStmt*>(stmt)) {
        scan_expr_for_templates(destructure->initializer.get(), emitter);
    }
    else if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt)) {
        if (var->initializer) {
            scan_expr_for_templates(var->initializer.get(), emitter);
        }
        for (const auto& argument : var->constructor_args) {
            scan_expr_for_templates(argument.get(), emitter);
        }
    }
    else if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        if (assign->value) {
            scan_expr_for_templates(assign->value.get(), emitter);
        }
    }
    else if (auto* assign = dynamic_cast<const ArrayAssignStmt*>(stmt)) {
        scan_expr_for_templates(assign->index.get(), emitter);
        scan_expr_for_templates(assign->value.get(), emitter);
    }
    else if (auto* assign = dynamic_cast<const MemberAssignStmt*>(stmt)) {
        scan_expr_for_templates(assign->lhs.get(), emitter);
        scan_expr_for_templates(assign->value.get(), emitter);
    }
    else if (auto* assign = dynamic_cast<const DerefAssignStmt*>(stmt)) {
        scan_expr_for_templates(assign->pointer.get(), emitter);
        scan_expr_for_templates(assign->value.get(), emitter);
    }
    else if (auto* exprs = dynamic_cast<const ExprStmt*>(stmt)) {
        scan_expr_for_templates(exprs->expression.get(), emitter);
    }
}

static void scan_expr_for_templates(const Expr* expr, LLVMEmitter& emitter) {
    if (!expr) return;
    
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        
        for (const auto& arg : call->args) {
            scan_expr_for_templates(arg.get(), emitter);
        }
        
        
        if (emitter.template_registry.is_template(call->callee)) {
            std::vector<BType> type_args;
            
            
            if (!call->template_args.empty()) {
                type_args = call->template_args;
            } else {
                
                std::vector<BType> arg_types;
                for (const auto& arg : call->args) {
                    arg_types.push_back(emitter.get_expr_type(arg.get()));
                }
                
                
                if (!emitter.template_registry.infer_type_args(call->callee, arg_types, type_args)) {
                    return;  
                }
            }
            
            
            FnDecl* inst = emitter.template_registry.instantiate(call->callee, type_args);
            if (inst) {
                
                emitter.func_types[inst->name] =
                    function_uses_native_void_abi(*inst)
                        ? IRType::VOID
                        : btype_to_ir(inst->return_type);
                emitter.func_return_btypes[inst->name] = inst->return_type;
                remember_struct_return_type(emitter, *inst);
                remember_parameter_passing_modes(emitter, *inst);
                std::vector<IRType> arg_ir_types;
                for (const auto& p : inst->params) {
                    arg_ir_types.push_back(btype_to_ir(p.type));
                }
                emitter.func_arg_types[inst->name] = arg_ir_types;
            }
        }
    }
    else if (auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        scan_expr_for_templates(bin->left.get(), emitter);
        scan_expr_for_templates(bin->right.get(), emitter);
    }
    else if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        scan_expr_for_templates(unary->operand.get(), emitter);
    }
    else if (auto* tern = dynamic_cast<const TernaryExpr*>(expr)) {
        scan_expr_for_templates(tern->cond.get(), emitter);
        scan_expr_for_templates(tern->then_expr.get(), emitter);
        scan_expr_for_templates(tern->else_expr.get(), emitter);
    }
    else if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        scan_expr_for_templates(idx->object.get(), emitter);
        scan_expr_for_templates(idx->index.get(), emitter);
    }
    else if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        scan_expr_for_templates(member->object.get(), emitter);
    }
    else if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        scan_expr_for_templates(ref->operand.get(), emitter);
    }
    else if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        scan_expr_for_templates(deref->operand.get(), emitter);
    }
    else if (auto* arr = dynamic_cast<const ArrayExpr*>(expr)) {
        for (const auto& elem : arr->elements) {
            scan_expr_for_templates(elem.get(), emitter);
        }
    }
    else if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        emitter.resolve_struct_type(literal->type_ref);
        for (const auto& field : literal->fields) {
            scan_expr_for_templates(field.value.get(), emitter);
        }
    }
    else if (auto* as_expr = dynamic_cast<const AsExpr*>(expr)) {
        scan_expr_for_templates(as_expr->operand.get(), emitter);
    }
}

static IRType extern_ir_type(const TypeRef& type_ref, BType type) {
    if (type_ref.base == BType::STRUCT) {
        if (type_ref.is_array) return IRType::ARR;
        if (type_ref.is_pointer) return IRType::I64_PTR;
        return IRType::STRUCT;
    }
    if (type == BType::VOID) return IRType::VOID;
    if (type == BType::PTR) return IRType::I8_PTR;
    return btype_to_ir(type);
}

static bool extern_struct_passed_indirect(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    const std::string& fallback = ""
);

static bool extern_indirect_struct_uses_byval() {
#if defined(__aarch64__) && !defined(_WIN32)
    
    
    
    return false;
#else
    return true;
#endif
}

static std::string extern_llvm_type(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    BType type,
    const std::string& struct_name = "",
    bool as_parameter = false
) {
    if (type_ref.base == BType::STRUCT) {
        std::string resolved = emitter.resolve_struct_type(type_ref);
        if (resolved.empty()) resolved = struct_name;
        if (resolved.empty()) resolved = type_ref.name;
        const std::string value_type = emitter.get_struct_type_str(resolved);
        if (type_ref.is_array) return value_type + "**";
        if (type_ref.is_pointer) return value_type + "*";
        if (as_parameter && extern_struct_passed_indirect(
                emitter, type_ref, struct_name)) {
            if (!extern_indirect_struct_uses_byval()) {
                return value_type + "*";
            }
            return value_type + "* byval(" + value_type + ") align 8";
        }
        return value_type;
    }
    if (type == BType::VOID) return "void";
    if (type == BType::PTR) return "i8*";
    if (is_array_type(type)) {
        BType element = get_array_elem_type(type);
        if (element == BType::UNKNOWN) return "i8*";
        return get_array_ptr_type(element);
    }
    return emitter.get_llvm_type(type);
}

static std::string extern_struct_name(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    const std::string& fallback = ""
) {
    if (type_ref.base != BType::STRUCT) return "";
    std::string result = emitter.resolve_struct_type(type_ref);
    if (result.empty()) result = fallback;
    if (result.empty()) result = type_ref.name;
    return result;
}

static bool extern_opaque_struct_by_value(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    const std::string& fallback = ""
) {
    if (type_ref.base != BType::STRUCT ||
        type_ref.is_pointer || type_ref.is_array) {
        return false;
    }
    const std::string name =
        extern_struct_name(emitter, type_ref, fallback);
    auto found = emitter.structs.find(name);
    return found != emitter.structs.end() && found->second.is_opaque;
}

struct ExternAbiLayout {
    size_t size = 0;
    size_t alignment = 1;
    bool valid = false;
};

static size_t extern_align_up(size_t value, size_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) / alignment * alignment;
}

static ExternAbiLayout extern_struct_layout(
    LLVMEmitter& emitter,
    const std::string& struct_name,
    std::unordered_set<std::string>& active
) {
    auto found = emitter.structs.find(struct_name);
    if (found == emitter.structs.end() || found->second.is_opaque ||
        !active.insert(struct_name).second) {
        return {};
    }

    const LLVMStructInfo& info = found->second;
    size_t offset = 0;
    size_t aggregate_alignment = 1;
    for (size_t index = 0; index < info.field_types.size(); ++index) {
        const BType field_type = info.field_types[index];
        size_t field_size = 0;
        size_t field_alignment = 1;

        if (field_type == BType::STRUCT) {
            if (index >= info.field_annotations.size()) {
                active.erase(struct_name);
                return {};
            }
            ExternAbiLayout nested = extern_struct_layout(
                emitter, info.field_annotations[index], active);
            if (!nested.valid) {
                active.erase(struct_name);
                return {};
            }
            field_size = nested.size;
            field_alignment = nested.alignment;
        } else {
            const int raw_size = getTypeSize(field_type);
            if (raw_size <= 0) {
                active.erase(struct_name);
                return {};
            }
            field_size = static_cast<size_t>(raw_size);
            field_alignment = std::min<size_t>(field_size, sizeof(void*));
        }

        offset = extern_align_up(offset, field_alignment);
        offset += field_size;
        aggregate_alignment = std::max(
            aggregate_alignment, field_alignment);
    }

    active.erase(struct_name);
    return {
        extern_align_up(offset, aggregate_alignment),
        aggregate_alignment,
        true
    };
}

static bool extern_struct_passed_indirect(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    const std::string& fallback
) {
    if (type_ref.base != BType::STRUCT ||
        type_ref.is_pointer || type_ref.is_array) {
        return false;
    }
    std::unordered_set<std::string> active;
    const ExternAbiLayout layout = extern_struct_layout(
        emitter, extern_struct_name(emitter, type_ref, fallback), active);
    if (!layout.valid) return false;

#if defined(_WIN32)
    
    return layout.size != 1 && layout.size != 2 &&
           layout.size != 4 && layout.size != 8;
#else
    
    
    return sizeof(void*) == 8 && layout.size > 16;
#endif
}

enum class ExternEightbyteClass {
    NONE,
    INTEGER,
    SSE
};

struct ExternAbiPiece {
    std::string llvm_type;
    size_t offset = 0;
};

struct ExternStructAbi {
    ExternAbiLayout layout;
    bool parameter_indirect = false;
    bool return_indirect = false;
    std::string direct_parameter_type;
    std::vector<ExternAbiPiece> pieces;
};

#if defined(__aarch64__) && !defined(_WIN32)
static bool collect_aarch64_hfa_pieces(
    LLVMEmitter& emitter,
    const std::string& struct_name,
    size_t base_offset,
    BType& element_type,
    std::vector<ExternAbiPiece>& pieces,
    std::unordered_set<std::string>& active
) {
    auto found = emitter.structs.find(struct_name);
    if (found == emitter.structs.end() || found->second.is_opaque ||
        !active.insert(struct_name).second) {
        return false;
    }

    const LLVMStructInfo& info = found->second;
    size_t offset = 0;
    for (size_t index = 0; index < info.field_types.size(); ++index) {
        const BType field_type = info.field_types[index];
        size_t field_size = 0;
        size_t field_alignment = 1;

        if (field_type == BType::STRUCT) {
            if (index >= info.field_annotations.size()) {
                active.erase(struct_name);
                return false;
            }
            std::unordered_set<std::string> layout_active = active;
            const ExternAbiLayout nested = extern_struct_layout(
                emitter, info.field_annotations[index], layout_active);
            if (!nested.valid) {
                active.erase(struct_name);
                return false;
            }
            field_size = nested.size;
            field_alignment = nested.alignment;
        } else if (field_type == BType::F32 || field_type == BType::F64) {
            field_size = static_cast<size_t>(getTypeSize(field_type));
            field_alignment = field_size;
        } else {
            active.erase(struct_name);
            return false;
        }

        offset = extern_align_up(offset, field_alignment);
        if (field_type == BType::STRUCT) {
            if (!collect_aarch64_hfa_pieces(
                    emitter,
                    info.field_annotations[index],
                    base_offset + offset,
                    element_type,
                    pieces,
                    active)) {
                active.erase(struct_name);
                return false;
            }
        } else {
            if (element_type == BType::UNKNOWN) {
                element_type = field_type;
            } else if (element_type != field_type) {
                active.erase(struct_name);
                return false;
            }
            pieces.push_back({
                field_type == BType::F32 ? "float" : "double",
                base_offset + offset
            });
            if (pieces.size() > 4) {
                active.erase(struct_name);
                return false;
            }
        }
        offset += field_size;
    }

    active.erase(struct_name);
    return !pieces.empty();
}
#endif

static ExternEightbyteClass merge_extern_abi_class(
    ExternEightbyteClass left,
    ExternEightbyteClass right
) {
    if (left == ExternEightbyteClass::INTEGER ||
        right == ExternEightbyteClass::INTEGER) {
        return ExternEightbyteClass::INTEGER;
    }
    if (left == ExternEightbyteClass::SSE ||
        right == ExternEightbyteClass::SSE) {
        return ExternEightbyteClass::SSE;
    }
    return ExternEightbyteClass::NONE;
}

static bool classify_extern_struct_fields(
    LLVMEmitter& emitter,
    const std::string& struct_name,
    size_t base_offset,
    std::vector<ExternEightbyteClass>& classes,
    std::unordered_set<std::string>& active
) {
    auto found = emitter.structs.find(struct_name);
    if (found == emitter.structs.end() || found->second.is_opaque ||
        !active.insert(struct_name).second) {
        return false;
    }

    const LLVMStructInfo& info = found->second;
    size_t offset = 0;
    for (size_t index = 0; index < info.field_types.size(); ++index) {
        const BType field_type = info.field_types[index];
        size_t field_size = 0;
        size_t field_alignment = 1;

        if (field_type == BType::STRUCT) {
            if (index >= info.field_annotations.size()) {
                active.erase(struct_name);
                return false;
            }
            std::unordered_set<std::string> layout_active = active;
            ExternAbiLayout nested = extern_struct_layout(
                emitter, info.field_annotations[index], layout_active);
            if (!nested.valid) {
                active.erase(struct_name);
                return false;
            }
            field_size = nested.size;
            field_alignment = nested.alignment;
        } else {
            const int raw_size = getTypeSize(field_type);
            if (raw_size <= 0) {
                active.erase(struct_name);
                return false;
            }
            field_size = static_cast<size_t>(raw_size);
            field_alignment = std::min<size_t>(field_size, sizeof(void*));
        }

        offset = extern_align_up(offset, field_alignment);
        const size_t absolute_offset = base_offset + offset;

        if (field_type == BType::STRUCT) {
            if (!classify_extern_struct_fields(
                    emitter, info.field_annotations[index], absolute_offset,
                    classes, active)) {
                active.erase(struct_name);
                return false;
            }
        } else {
            const ExternEightbyteClass field_class =
                (field_type == BType::F32 || field_type == BType::F64)
                    ? ExternEightbyteClass::SSE
                    : ExternEightbyteClass::INTEGER;
            const size_t first = absolute_offset / 8;
            const size_t last =
                (absolute_offset + field_size - 1) / 8;
            for (size_t part = first;
                 part <= last && part < classes.size(); ++part) {
                classes[part] = merge_extern_abi_class(
                    classes[part], field_class);
            }
        }

        offset += field_size;
    }

    active.erase(struct_name);
    return true;
}

static ExternStructAbi get_extern_struct_abi(
    LLVMEmitter& emitter,
    const TypeRef& type_ref,
    const std::string& fallback = ""
) {
    ExternStructAbi result;
    const std::string name =
        extern_struct_name(emitter, type_ref, fallback);
    std::unordered_set<std::string> layout_active;
    result.layout = extern_struct_layout(
        emitter, name, layout_active);
    if (!result.layout.valid) return result;

    result.parameter_indirect = extern_struct_passed_indirect(
        emitter, type_ref, fallback);
    result.return_indirect = result.parameter_indirect;

#if defined(__ANDROID__) && defined(__arm__) && !defined(__aarch64__)
    
    
    
    result.return_indirect = true;
    result.parameter_indirect = false;
    const size_t lane_size = result.layout.alignment >= 8 ? 8 : 4;
    const size_t lane_count =
        (result.layout.size + lane_size - 1) / lane_size;
    result.direct_parameter_type = "[" + std::to_string(lane_count) +
        " x i" + std::to_string(lane_size * 8) + "]";
    return result;
#endif

    if (result.parameter_indirect && result.return_indirect) return result;

#if defined(__aarch64__) && !defined(_WIN32)
    
    
    
    
    
    BType hfa_element_type = BType::UNKNOWN;
    std::unordered_set<std::string> hfa_active;
    if (collect_aarch64_hfa_pieces(
            emitter,
            name,
            0,
            hfa_element_type,
            result.pieces,
            hfa_active)) {
        return result;
    }
    result.pieces.clear();
    for (size_t offset = 0; offset < result.layout.size; offset += 8) {
        const size_t bytes = std::min<size_t>(8, result.layout.size - offset);
        result.pieces.push_back({"i" + std::to_string(bytes * 8), offset});
    }
    return result;
#endif

    const size_t piece_count = (result.layout.size + 7) / 8;
    std::vector<ExternEightbyteClass> classes(
        piece_count, ExternEightbyteClass::NONE);
    std::unordered_set<std::string> classify_active;
    if (!classify_extern_struct_fields(
            emitter, name, 0, classes, classify_active)) {
        result.layout.valid = false;
        return result;
    }

    for (size_t index = 0; index < piece_count; ++index) {
        const size_t offset = index * 8;
        const size_t bytes = std::min<size_t>(
            8, result.layout.size - offset);
        std::string llvm_type;
#if defined(_WIN32)
        llvm_type = "i" + std::to_string(bytes * 8);
#else
        if (classes[index] == ExternEightbyteClass::SSE) {
            llvm_type = bytes <= 4 ? "float" : "double";
        } else {
            llvm_type = "i" + std::to_string(bytes * 8);
        }
#endif
        result.pieces.push_back({llvm_type, offset});
    }
    return result;
}

static std::string extern_coerced_return_type(
    const ExternStructAbi& abi
) {
    if (abi.pieces.empty()) return "void";
    if (abi.pieces.size() == 1) return abi.pieces.front().llvm_type;

    std::string result = "{ ";
    for (size_t index = 0; index < abi.pieces.size(); ++index) {
        if (index != 0) result += ", ";
        result += abi.pieces[index].llvm_type;
    }
    result += " }";
    return result;
}

static bool ensure_drop_function(
    LLVMEmitter& emitter,
    const std::string& concrete_name
) {
    if (emitter.drop_functions.count(concrete_name)) return true;

    auto struct_it = emitter.structs.find(concrete_name);
    if (struct_it == emitter.structs.end()) return false;
    const LLVMStructInfo& info = struct_it->second;
    if (info.template_name.empty() || info.template_args.empty()) return false;

    const std::string drop_template_name = mangle_method_name(
        info.template_name, "__drop__");
    if (!emitter.template_registry.is_template(drop_template_name)) {
        return false;
    }

    for (const TypeRef& type_argument : info.template_args) {
        if (type_ref_to_btype(type_argument) == BType::UNKNOWN) return false;
    }

    FnDecl* instantiated = emitter.template_registry.instantiate(
        drop_template_name, info.template_args);
    if (!instantiated) return false;

    instantiated->is_drop = true;
    instantiated->is_method = true;
    instantiated->method_owner = concrete_name;
    instantiated->method_name = "__drop__";

    if (!instantiated->params.empty()) {
        ParamDecl& receiver = instantiated->params[0];
        receiver.type = BType::STRUCT;
        receiver.type_ref = TypeRef{};
        receiver.type_ref.base = BType::STRUCT;
        receiver.type_ref.name = concrete_name;
        receiver.struct_name = concrete_name;
        receiver.type_annotation = concrete_name;
    }

    emitter.func_types[instantiated->name] = IRType::VOID;
    emitter.func_return_btypes[instantiated->name] = BType::VOID;
    std::vector<IRType> argument_types;
    for (const ParamDecl& parameter : instantiated->params) {
        argument_types.push_back(btype_to_ir(parameter.type));
    }
    emitter.func_arg_types[instantiated->name] =
        std::move(argument_types);
    remember_parameter_passing_modes(emitter, *instantiated);
    emitter.drop_functions[concrete_name] = instantiated->name;
    return true;
}


std::string generate_llvm_ir(Program& prog) {
    LLVMEmitter emitter;
    
    emitter.out << "declare i32 @printf(i8*, ...)\n";
    emitter.out << "declare i32 @strcmp(i8*, i8*)\n";
    emitter.globals << "@fmt_str = private constant [4 x i8] c\"%s\\0A\\00\"\n";
    emitter.globals << "@fmt_ptr = private constant [4 x i8] c\"%p\\0A\\00\"\n";
    emitter.globals << "@fmt_num = private constant [6 x i8] c\"%lld\\0A\\00\"\n";
    emitter.globals << "@fmt_unum = private constant [6 x i8] c\"%llu\\0A\\00\"\n";
    emitter.globals << "@fmt_hex = private constant [8 x i8] c\"0x%llX\\0A\\00\"\n";
    emitter.globals << "@fmt_f64 = private constant [5 x i8] c\"%lf\\0A\\00\"\n";
    emitter.globals << "@fmt_str_raw = private constant [3 x i8] c\"%s\\00\"\n";
    emitter.globals << "@fmt_ptr_raw = private constant [3 x i8] c\"%p\\00\"\n";
    emitter.globals << "@fmt_num_raw = private constant [5 x i8] c\"%lld\\00\"\n";
    emitter.globals << "@fmt_unum_raw = private constant [5 x i8] c\"%llu\\00\"\n";
    emitter.globals << "@fmt_hex_raw = private constant [7 x i8] c\"0x%llX\\00\"\n";
    emitter.globals << "@fmt_f64_raw = private constant [4 x i8] c\"%lf\\00\"\n\n";
    emitter.globals << "@.null_str = private constant [7 x i8] c\"(null)\\00\"\n";
    emitter.globals << "@.null_str_raw = private constant [7 x i8] c\"(null)\\00\"\n";
    emitter.globals << "@fmt_newline = private constant [2 x i8] c\"\\0A\\00\"\n\n";

    emitter.global_vars["_args"] = IRType::ARR;
    emitter.global_btypes["_args"] = BType::STR_ARR;
    emitter.global_consts.insert("_args");
    emitter.globals << "@_args = global i8** null\n";
    emitter.global_vars["_argc"] = IRType::I64;
    emitter.global_btypes["_argc"] = BType::I64;
    emitter.global_consts.insert("_argc");
    emitter.globals << "@_argc = global i64 0\n";
    
    emitter.collect_structs(prog);
    emitter.emit_struct_defs();
    
    emitter.out << "declare i8* @malloc(i64)\n";
    emitter.out << "declare void @free(i8*)\n";
    
    emitter.func_types["malloc"] = IRType::I8_PTR;
    emitter.func_return_btypes["malloc"] = BType::PTR;
    emitter.func_arg_types["malloc"] = {IRType::I64};
    emitter.func_types["free"] = IRType::VOID;
    emitter.func_return_btypes["free"] = BType::VOID;
    emitter.func_arg_types["free"] = {IRType::I8_PTR};
    emitter.func_types["strcmp"] = IRType::I32;
    emitter.func_return_btypes["strcmp"] = BType::I32;
    emitter.func_arg_types["strcmp"] = {IRType::I8_PTR, IRType::I8_PTR};

    infer_unannotated_function_signatures(prog, emitter);

    std::unordered_set<std::string> defined_function_names;
    for (const auto& fn : prog.functions) {
        if (!fn->is_extern) defined_function_names.insert(fn->name);
    }

    for (const auto& fn : prog.functions) {
        if (!fn->type_params.empty()) {
            emitter.template_registry.register_template(*fn);
            continue;
        }

        if (fn->is_extern &&
            !defined_function_names.count(fn->name) &&
            !emitter.extern_functions.count(fn->name)) {
            emitter.extern_functions[fn->name] = fn.get();
        }

        if (fn->is_operator &&
            fn->return_type == BType::UNKNOWN) {
            emitter.infer_operator_return_type(*fn);
        }

        emitter.func_types[fn->name] = function_uses_native_void_abi(*fn)
            ? IRType::VOID
            : (fn->is_extern
                ? extern_ir_type(fn->return_type_ref, fn->return_type)
                : btype_to_ir(fn->return_type));
        emitter.func_return_btypes[fn->name] = fn->return_type;
        if (fn->is_variadic) {
            emitter.variadic_functions.insert(fn->name);
        }
        remember_struct_return_type(emitter, *fn);
        remember_parameter_passing_modes(emitter, *fn);

        std::vector<IRType> arg_types;

        for (const auto& param : fn->params) {
            arg_types.push_back(fn->is_extern
                ? extern_ir_type(param.type_ref, param.type)
                : btype_to_ir(param.type));
        }

        emitter.func_arg_types[fn->name] =
            std::move(arg_types);

        if (fn->is_drop && !fn->params.empty()) {
            std::string concrete_name =
                emitter.resolve_struct_type(
                    fn->params[0].type_ref
                );

            if (concrete_name.empty()) {
                concrete_name =
                    fn->params[0].struct_name;
            }

            emitter.drop_functions[concrete_name] =
                fn->name;
        }
    }
    
    for (const auto& fn : prog.functions) {
        if (fn->type_params.empty()) {
            
            scan_and_instantiate_templates(fn->body.get(), emitter);
        }
    }
    
    for (const auto& stmt : prog.statements) {
        scan_and_instantiate_templates(stmt.get(), emitter);
    }
    
    std::vector<const LLHStmt*> active_llh_statements;
    for (const auto& stmt : prog.statements) {
        collect_active_llh(stmt.get(), active_llh_statements);
    }
    for (const auto& fn : prog.functions) {
        collect_active_llh(fn->body.get(), active_llh_statements);
    }

    std::unordered_set<std::string> emitted_llh;
    std::unordered_map<std::string, std::string> emitted_llh_symbols;
    auto remember_predeclared_function = [&emitted_llh_symbols](
        const std::string& name,
        IRType return_type,
        const std::vector<IRType>& argument_types,
        bool variadic = false
    ) {
        std::string signature = std::to_string(static_cast<int>(return_type)) + "(";
        for (IRType argument_type : argument_types) {
            signature += std::to_string(static_cast<int>(argument_type)) + ",";
        }
        if (variadic) signature += "...";
        signature += ")";
        emitted_llh_symbols["function:" + name] = std::move(signature);
    };
    remember_predeclared_function("printf", IRType::I32, {IRType::I8_PTR}, true);
    remember_predeclared_function(
        "strcmp", IRType::I32, {IRType::I8_PTR, IRType::I8_PTR});
    remember_predeclared_function("malloc", IRType::I8_PTR, {IRType::I64});
    remember_predeclared_function("free", IRType::VOID, {IRType::I8_PTR});

    for (const LLHStmt* llh : active_llh_statements) {
        const std::string original = trim_copy(llh->llvm_code);
        const std::string normalized =
            normalize_opaque_ptr_tokens(original);
        if (!emitted_llh.insert(normalized).second) continue;
        const size_t declare_pos = normalized.find("declare");

        if (declare_pos != std::string::npos) {
            const size_t at_pos = normalized.find('@', declare_pos);
            const size_t open_pos = normalized.find('(', at_pos);
            const size_t close_pos = normalized.rfind(')');

            if (at_pos != std::string::npos && open_pos != std::string::npos &&
                close_pos != std::string::npos && close_pos >= open_pos) {
                const std::string function_name = trim_copy(
                    normalized.substr(at_pos + 1, open_pos - at_pos - 1));
                const std::string return_text = trim_copy(
                    normalized.substr(declare_pos + 7, at_pos - (declare_pos + 7)));

                IRType return_type = parse_ll_ir_type(return_text);
                if (return_type == IRType::UNKNOWN) {
                    gerror("Unknown LLVM return type in __llh: '" + return_text + "' :/\n");
                    return_type = IRType::I64;
                }

                std::vector<IRType> argument_types;
                const std::string args_text =
                    normalized.substr(open_pos + 1, close_pos - open_pos - 1);
                for (const std::string& argument : split_ll_args(args_text)) {
                    IRType argument_type = parse_ll_ir_type(argument);
                    if (argument_type == IRType::UNKNOWN) {
                        gerror("Unknown LLVM argument type in __llh: '" + argument + "' :/\n");
                        argument_type = IRType::I64;
                    }
                    argument_types.push_back(argument_type);
                }

                std::string signature =
                    std::to_string(static_cast<int>(return_type)) + "(";
                for (IRType argument_type : argument_types) {
                    signature += std::to_string(
                        static_cast<int>(argument_type)) + ",";
                }
                if (args_text.find("...") != std::string::npos) {
                    signature += "...";
                    emitter.variadic_functions.insert(function_name);
                    std::vector<std::string> fixed_types;
                    fixed_types.reserve(argument_types.size());
                    for (IRType type : argument_types) {
                        fixed_types.push_back(llvm_ir_type_name(type));
                    }
                    emitter.extern_variadic_fixed_types[function_name] =
                        std::move(fixed_types);
                }
                signature += ")";

                const std::string symbol_key = "function:" + function_name;
                auto previous = emitted_llh_symbols.find(symbol_key);
                if (previous != emitted_llh_symbols.end()) {
                    if (previous->second != signature) {
                        gerror("Conflicting __llh declarations for function '" +
                               function_name + "' :/\n");
                    }
                    continue;
                }
                emitted_llh_symbols[symbol_key] = signature;

                emitter.func_types[function_name] = return_type;
                emitter.func_return_btypes[function_name] =
                    ir_to_btype(return_type);
                emitter.func_arg_types[function_name] = argument_types;
            }
        } else {
            const size_t at_pos = normalized.find('@');
            const size_t global_pos = normalized.find("global");
            if (at_pos != std::string::npos && global_pos != std::string::npos) {
                size_t name_end = normalized.find_first_of(" =\t\r\n", at_pos + 1);
                if (name_end == std::string::npos) name_end = normalized.size();
                const std::string variable_name =
                    normalized.substr(at_pos + 1, name_end - at_pos - 1);

                std::string type_text = trim_copy(normalized.substr(global_pos + 6));
                const size_t type_end = type_text.find_first_of(" ,\t\r\n");
                if (type_end != std::string::npos) type_text = type_text.substr(0, type_end);

                IRType variable_type = parse_ll_ir_type(type_text);
                if (variable_type != IRType::UNKNOWN) {
                    const std::string symbol_key = "global:" + variable_name;
                    const std::string signature = std::to_string(
                        static_cast<int>(variable_type));
                    auto previous = emitted_llh_symbols.find(symbol_key);
                    if (previous != emitted_llh_symbols.end()) {
                        if (previous->second != signature) {
                            gerror("Conflicting __llh declarations for global '" +
                                   variable_name + "' :/\n");
                        }
                        continue;
                    }
                    emitted_llh_symbols[symbol_key] = signature;

                    emitter.global_vars[variable_name] = variable_type;
                    emitter.global_btypes[variable_name] =
                        variable_type == IRType::PTR ? BType::PTR : ir_to_btype(variable_type);
                }
            }
        }

        emitter.globals << normalized << "\n";
    }

    std::unordered_set<std::string> emitted_extern_functions;
    for (const auto& fn : prog.functions) {
        if (!fn->is_extern || !fn->type_params.empty()) continue;
        if (!emitted_extern_functions.insert(fn->name).second) continue;

        bool has_definition = false;
        for (const auto& candidate : prog.functions) {
            if (!candidate->is_extern && candidate->name == fn->name) {
                has_definition = true;
                break;
            }
        }
        if (has_definition) continue;

        bool valid_abi = true;
        if (extern_opaque_struct_by_value(
                emitter, fn->return_type_ref,
                fn->return_type_annotation)) {
            gerror("Extern function '" + fn->name +
                   "' cannot return opaque struct '" +
                   extern_struct_name(
                       emitter, fn->return_type_ref,
                       fn->return_type_annotation) +
                   "' by value; use a pointer :/\n");
            valid_abi = false;
        }
        for (const ParamDecl& parameter : fn->params) {
            if (!extern_opaque_struct_by_value(
                    emitter, parameter.type_ref,
                    parameter.struct_name)) {
                continue;
            }
            gerror("Extern function '" + fn->name +
                   "' cannot accept opaque struct '" +
                   extern_struct_name(
                       emitter, parameter.type_ref,
                       parameter.struct_name) +
                   "' by value; use a pointer :/\n");
            valid_abi = false;
        }
        if (!valid_abi) continue;

        const std::string symbol = "@" + fn->name + "(";
        if (emitter.out.str().find(symbol) != std::string::npos ||
            emitter.globals.str().find(symbol) != std::string::npos) {
            continue;
        }

        std::string return_llvm_type;
        std::vector<std::string> llvm_parameters;

        const bool returns_struct_value =
            fn->return_type_ref.base == BType::STRUCT &&
            !fn->return_type_ref.is_pointer &&
            !fn->return_type_ref.is_array;
        if (returns_struct_value) {
            const std::string return_struct = extern_struct_name(
                emitter, fn->return_type_ref,
                fn->return_type_annotation);
            const std::string value_type =
                emitter.get_struct_type_str(return_struct);
            const ExternStructAbi abi = get_extern_struct_abi(
                emitter, fn->return_type_ref,
                fn->return_type_annotation);
            if (abi.return_indirect) {
                return_llvm_type = "void";
                llvm_parameters.push_back(
                    value_type + "* sret(" + value_type + ") align " +
                    std::to_string(abi.layout.alignment));
            } else {
                return_llvm_type = extern_coerced_return_type(abi);
            }
        } else {
            return_llvm_type = extern_llvm_type(
                emitter, fn->return_type_ref, fn->return_type);
        }

        for (const ParamDecl& parameter : fn->params) {
            const bool accepts_struct_value =
                parameter.type_ref.base == BType::STRUCT &&
                !parameter.type_ref.is_pointer &&
                !parameter.type_ref.is_array;
            if (accepts_struct_value) {
                const ExternStructAbi abi = get_extern_struct_abi(
                    emitter, parameter.type_ref,
                    parameter.struct_name);
                if (abi.parameter_indirect) {
                    llvm_parameters.push_back(extern_llvm_type(
                        emitter, parameter.type_ref, parameter.type,
                        parameter.struct_name, true));
                } else if (!abi.direct_parameter_type.empty()) {
                    llvm_parameters.push_back(abi.direct_parameter_type);
                } else {
                    for (const ExternAbiPiece& piece : abi.pieces) {
                        llvm_parameters.push_back(piece.llvm_type);
                    }
                }
            } else {
                llvm_parameters.push_back(extern_llvm_type(
                    emitter, parameter.type_ref, parameter.type,
                    parameter.struct_name, true));
            }
        }

        emitter.out << "declare " << return_llvm_type
                    << " @" << fn->name << "(";
        for (size_t i = 0; i < llvm_parameters.size(); ++i) {
            if (i != 0) emitter.out << ", ";
            emitter.out << llvm_parameters[i];
        }
        if (fn->is_variadic) {
            emitter.variadic_functions.insert(fn->name);
            if (!llvm_parameters.empty()) emitter.out << ", ";
            emitter.out << "...";

            std::vector<std::string> fixed_types;
            fixed_types.reserve(llvm_parameters.size());
            for (const std::string& parameter : llvm_parameters) {
                const size_t attribute = parameter.find(' ');
                fixed_types.push_back(parameter.substr(0, attribute));
            }
            emitter.extern_variadic_fixed_types[fn->name] =
                std::move(fixed_types);
        }
        emitter.out << ")\n";
    }

    bool has_runtime_global_initialization = false;

    for (const auto& stmt : prog.statements) {
        auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get());
        if (!var) {
            if (is_runtime_top_level_statement(stmt.get())) {
                has_runtime_global_initialization = true;
            }
            continue;
        }

        BType var_type = var->type;
        if (var_type == BType::UNKNOWN && var->initializer) {
            var_type = emitter.get_expr_type(var->initializer.get());
            if (var_type == BType::UNKNOWN) var_type = var->initializer->btype;
        }
        if (var_type == BType::UNKNOWN) var_type = BType::INT;

        if (is_aggregate_type(var_type)) {
            const TypeRef type_ref = type_ref_or_legacy(
                var->type_ref, var_type, var->struct_name);
            std::string struct_name = var_type == BType::TUPLE
                ? emitter.resolve_tuple_type(type_ref)
                : emitter.resolve_struct_type(type_ref);
            if (struct_name.empty()) struct_name = var->struct_name;
            if (var_type != BType::TUPLE && struct_name.empty()) {
                struct_name = var->type_annotation;
            }
            if (struct_name.empty() && var->initializer) {
                struct_name = emitter.get_expr_struct_name(var->initializer.get());
            }

            if (struct_name.empty() || !emitter.structs.count(struct_name)) {
                gerror("Cannot resolve global aggregate type for '" + var->name + "' :/\n");
                continue;
            }
            if (emitter.structs.at(struct_name).is_opaque) {
                gerror("Cannot instantiate opaque extern struct '" +
                       struct_name + "' by value; use a pointer :/\n");
                continue;
            }

            emitter.globals << "@" << var->name << " = global %" << struct_name
                            << " zeroinitializer\n";
            emitter.global_vars[var->name] = IRType::STRUCT;
            emitter.global_btypes[var->name] = var_type;
            if (var->is_const) emitter.global_consts.insert(var->name);
            g_global_struct_types[&emitter][var->name] = struct_name;
            if (var->initializer || var->has_constructor_call) {
                has_runtime_global_initialization = true;
            }
            continue;
        }

        std::string initializer;
        if (!var->initializer) {
            initializer = is_pointer_like_btype(var_type) ? "null" : "0";
        } else if (auto* number = dynamic_cast<const NumberExpr*>(var->initializer.get())) {
            initializer = !number->literal.empty()
                ? number->literal
                : (number->is_float
                    ? std::to_string(number->value)
                    : std::to_string(static_cast<long long>(number->value)));
        } else if (auto* boolean = dynamic_cast<const BoolExpr*>(var->initializer.get())) {
            initializer = boolean->value ? "true" : "false";
        } else if (is_null_expression(var->initializer.get()) &&
                   is_pointer_like_btype(var_type)) {
            initializer = "null";
        } else {
            initializer = is_pointer_like_btype(var_type) ? "null" : "0";
            has_runtime_global_initialization = true;
        }

        const bool immutable_static_initializer =
            var->is_const &&
            (dynamic_cast<const NumberExpr*>(var->initializer.get()) != nullptr ||
             dynamic_cast<const BoolExpr*>(var->initializer.get()) != nullptr ||
             (is_null_expression(var->initializer.get()) &&
              is_pointer_like_btype(var_type)));

        emitter.globals << "@" << var->name << " = "
                        << (immutable_static_initializer ? "constant " : "global ")
                        << emitter.get_llvm_type(var_type) << " " << initializer << "\n";
        emitter.global_vars[var->name] = btype_to_ir(var_type);
        emitter.global_btypes[var->name] = var_type;
        if (var->is_const) emitter.global_consts.insert(var->name);
    }

    g_has_global_init[&emitter] = has_runtime_global_initialization;

    std::vector<std::string> concrete_struct_names;

    for (const auto& [name, info] : emitter.structs) {
        if (!info.template_args.empty()) {
            concrete_struct_names.push_back(name);
        }
    }

    for (const std::string& concrete_name :
        concrete_struct_names) {
        auto struct_it =
            emitter.structs.find(concrete_name);

        if (struct_it == emitter.structs.end()) {
            continue;
        }

        const LLVMStructInfo info = struct_it->second;

        if (info.template_name.empty()) {
            continue;
        }

        const std::string drop_template_name =
            mangle_method_name(
                info.template_name,
                "__drop__"
            );

        if (!emitter.template_registry.is_template(
                drop_template_name)) {
            continue;
        }

        std::vector<TypeRef> type_args;
        bool valid = true;

        for (const TypeRef& type_arg_ref :
            info.template_args) {
            if (type_ref_to_btype(type_arg_ref) == BType::UNKNOWN) {
                valid = false;
                break;
            }

            type_args.push_back(type_arg_ref);
        }

        if (!valid) {
            continue;
        }

        FnDecl* instantiated =
            emitter.template_registry.instantiate(
                drop_template_name,
                type_args
            );

        if (!instantiated) {
            continue;
        }

        instantiated->is_drop = true;
        instantiated->is_method = true;
        instantiated->method_owner = concrete_name;
        instantiated->method_name = "__drop__";

        /*
        * Примусово конкретизуємо this.
        * Це гарантує сигнатуру:
        *
        *   void @drop(%Vec_i64* %this)
        */
        if (!instantiated->params.empty()) {
            ParamDecl& this_param =
                instantiated->params[0];

            this_param.type = BType::STRUCT;

            this_param.type_ref = TypeRef{};
            this_param.type_ref.base = BType::STRUCT;
            this_param.type_ref.name = concrete_name;

            this_param.struct_name = concrete_name;
            this_param.type_annotation = concrete_name;
        }

        emitter.func_types[instantiated->name] =
            IRType::VOID;
        emitter.func_return_btypes[instantiated->name] = BType::VOID;

        std::vector<IRType> argument_types;

        for (const ParamDecl& param :
            instantiated->params) {
            argument_types.push_back(
                btype_to_ir(param.type)
            );
        }

        emitter.func_arg_types[instantiated->name] =
            std::move(argument_types);
        remember_parameter_passing_modes(emitter, *instantiated);

        emitter.drop_functions[concrete_name] =
            instantiated->name;
    }

    if (has_runtime_global_initialization) {
        const std::string saved_function_name = current_function_name;
        current_function_name = "__llbm_init_globals";
        emitter.body << "define internal void @__llbm_init_globals() {\nentry:\n";

        for (const auto& stmt : prog.statements) {
            if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
                BType target_type = emitter.global_btypes[var->name];
                if (var->has_constructor_call) {
                    const std::string target_struct =
                        g_global_struct_types[&emitter][var->name];
                    emitter.emit_struct_constructor(*var, target_struct);
                    continue;
                }

                if (!var->initializer) continue;

                if (is_aggregate_type(target_type)) {
                    const std::string target_struct =
                        g_global_struct_types[&emitter][var->name];
                    if (var->type_ref.base == BType::TUPLE &&
                        !var->type_ref.type_args.empty()) {
                        emitter.expected_tuple_types[var->initializer.get()] =
                            var->type_ref;
                    }
                    const std::string source_struct =
                        emitter.get_expr_struct_name(var->initializer.get());

                    if (source_struct.empty() || source_struct != target_struct) {
                        gerror("Cannot initialize global aggregate '" + var->name +
                               "' with incompatible type :/\n");
                        continue;
                    }

                    std::string source_ptr =
                        emitter.emit_expression(var->initializer.get());
                    std::string loaded = emitter.next_ssa();
                    emitter.body << "  " << loaded << " = load %" << target_struct
                                 << ", %" << target_struct << "* " << source_ptr << "\n";
                    emitter.body << "  store %" << target_struct << " " << loaded
                                 << ", %" << target_struct << "* @" << var->name << "\n";
                    continue;
                }

                const bool already_static =
                    dynamic_cast<const NumberExpr*>(var->initializer.get()) != nullptr ||
                    dynamic_cast<const BoolExpr*>(var->initializer.get()) != nullptr ||
                    (is_null_expression(var->initializer.get()) &&
                     is_pointer_like_btype(var->type));
                if (already_static) continue;

                BType source_type = emitter.get_expr_type(var->initializer.get());
                if (source_type == BType::UNKNOWN) source_type = var->initializer->btype;

                std::string value = emitter.emit_expression(var->initializer.get());
                IRType source_ir = btype_to_ir(source_type);
                IRType target_ir = btype_to_ir(target_type);
                if (!coerce_ir_value(emitter, value, source_ir, target_ir, "  ")) {
                    gerror("Cannot initialize global '" + var->name +
                           "' with incompatible type :/\n");
                    continue;
                }

                const std::string target_name = llvm_ir_type_name(target_ir);
                emitter.body << "  store " << target_name << " " << value
                             << ", " << target_name << "* @" << var->name << "\n";
                continue;
            }

            if (is_runtime_top_level_statement(stmt.get())) {
                emitter.emit_statement(stmt.get());
            }
        }

        emitter.body << "  ret void\n}\n\n";
        current_function_name = saved_function_name;
    }

    for (const auto& fn : prog.functions) {
        if (fn->is_extern || !fn->type_params.empty()) continue;
        emitter.emit_function(*fn);
    }
    
    while (true) {
        auto instantiations = emitter.template_registry.take_instantiations();
        if (instantiations.empty()) break;
        for (const auto& fn : instantiations) {
            emitter.emit_function(*fn);
        }
    }
    
    emitter.emit_struct_defs();

    emitter.globals << "@.type_str_str = private constant [4 x i8] c\"str\\00\"\n";
    emitter.globals << "@.type_str_int = private constant [4 x i8] c\"int\\00\"\n";
    emitter.globals << "@.type_str_f64 = private constant [4 x i8] c\"f64\\00\"\n";
    emitter.globals << "@.type_str_bol = private constant [4 x i8] c\"bol\\00\"\n";
    emitter.globals << "@.type_str_arr = private constant [4 x i8] c\"arr\\00\"\n";
    emitter.globals << "@.type_str_obj = private constant [4 x i8] c\"obj\\00\"\n";
    emitter.globals << "@.type_str_fn = private constant [5 x i8] c\"func\\00\"\n";
    emitter.globals << "@.type_str_nul = private constant [4 x i8] c\"nul\\00\"\n";
    emitter.globals << "@.type_str_ptr = private constant [4 x i8] c\"ptr\\00\"\n";
    emitter.globals << "@.type_str_i8 = private constant [3 x i8] c\"i8\\00\"\n";
    emitter.globals << "@.type_str_i16 = private constant [4 x i8] c\"i16\\00\"\n";
    emitter.globals << "@.type_str_i32 = private constant [4 x i8] c\"i32\\00\"\n";
    emitter.globals << "@.type_str_i64 = private constant [4 x i8] c\"i64\\00\"\n";
    emitter.globals << "@.type_str_u8 = private constant [3 x i8] c\"u8\\00\"\n";
    emitter.globals << "@.type_str_u16 = private constant [4 x i8] c\"u16\\00\"\n";
    emitter.globals << "@.type_str_u32 = private constant [4 x i8] c\"u32\\00\"\n";
    emitter.globals << "@.type_str_u64 = private constant [4 x i8] c\"u64\\00\"\n";
    emitter.globals << "@.type_str_f32 = private constant [4 x i8] c\"f32\\00\"\n";
    emitter.globals << "@.type_str_isize = private constant [6 x i8] c\"isize\\00\"\n";
    emitter.globals << "@.type_str_usize = private constant [6 x i8] c\"usize\\00\"\n";
    emitter.globals << "@.type_str_hex = private constant [4 x i8] c\"hex\\00\"\n";
    emitter.globals << "@.type_str_tup = private constant [4 x i8] c\"tup\\00\"\n";

    std::string result;
    result += emitter.struct_defs.str();
    result += emitter.out.str();
    result += emitter.globals.str();
    result += emitter.anonymous_functions.str();
    result += emitter.body.str();

    g_global_struct_types.erase(&emitter);
    g_has_global_init.erase(&emitter);
    g_struct_return_types.erase(&emitter);
    g_current_struct_return_type.erase(&emitter);

    return result;
}

void LLVMEmitter::emit_program(const Program& prog) {}
static bool statement_falls_through(const Stmt* stmt);

void LLVMEmitter::emit_function(const FnDecl& fn) {
    vars.clear();

    current_fn_return_type = fn.return_type;
    current_function_name = fn.name;
    g_current_struct_return_type[this].clear();

    std::string ret_type;
    const bool native_void = function_uses_native_void_abi(fn);

    if (native_void) {
        ret_type = "void";
    } else if (is_aggregate_type(fn.return_type)) {
        std::string struct_name =
            get_function_struct_return_name(*this, fn);

        if (struct_name.empty()) {
            gerror(
                "Cannot resolve aggregate return type for '" +
                fn.name + "' :/\n"
            );
            struct_name = "struct";
        }

        g_current_struct_return_type[this] = struct_name;
        g_struct_return_types[this][fn.name] = struct_name;
        ret_type = get_struct_type_str(struct_name) + "*";
    } else {
        ret_type = get_llvm_type(fn.return_type);
    }
    
    body << "define ";
    if (fn.name != "main") body << "internal ";
    body << ret_type << " @" << fn.name << "(";

    if (fn.name == "main") {
        body << "i32 %__ferra_argc, i8** %__ferra_argv";
    }
    
    for (size_t i = 0; i < fn.params.size(); i++) {
        if (i > 0 || fn.name == "main") body << ", ";
        
        BType param_type = fn.params[i].type;
        const std::string incoming = "%__arg_" + fn.params[i].name;
        if (param_type == BType::PTR) {
            body << get_llvm_type(param_type) << " " << incoming;
        } else if (is_array_type(param_type)) {
            if (fn.params[i].type_ref.base == BType::STRUCT &&
                fn.params[i].type_ref.is_array) {
                std::string struct_name = resolve_struct_type(
                    fn.params[i].type_ref);
                if (struct_name.empty()) {
                    struct_name = !fn.params[i].struct_name.empty()
                        ? fn.params[i].struct_name
                        : fn.params[i].type_annotation;
                }
                const bool inline_elements =
                    inline_struct_array_types.count(struct_name);
                body << get_struct_type_str(struct_name)
                        << (inline_elements ? "* " : "** ")
                        << incoming;
                continue;
            }
            BType elem_type = get_array_elem_type(param_type);
            if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
            body << get_array_ptr_type(elem_type) << " " << incoming;
        } else if (is_aggregate_type(param_type)) {
            const TypeRef type_ref = type_ref_or_legacy(
                fn.params[i].type_ref, param_type, fn.params[i].struct_name);
            std::string struct_name = param_type == BType::TUPLE
                ? resolve_tuple_type(type_ref)
                : resolve_struct_type(type_ref);
            if (struct_name.empty()) {
                struct_name = !fn.params[i].struct_name.empty()
                    ? fn.params[i].struct_name
                    : fn.params[i].type_annotation;
            }
            body << get_struct_type_str(struct_name)
                    << ((param_type == BType::STRUCT &&
                        fn.params[i].type_ref.pass_by_value) ? " " : "* ")
                    << incoming;
        } else {
            body << get_llvm_type(param_type) << " " << incoming;
        }
    }
    
    body << ")";
    if (fn.force_inline) body << " alwaysinline";
    if (fn.force_noinline) body << " noinline";
    body << " {\nentry:\n";

    if (fn.name == "main") {
        std::string has_user_arguments = next_ssa();
        body << "  " << has_user_arguments
                << " = icmp sgt i32 %__ferra_argc, 1\n";
        std::string raw_argument_count = next_ssa();
        body << "  " << raw_argument_count
                << " = sub i32 %__ferra_argc, 1\n";
        std::string argument_count_i32 = next_ssa();
        body << "  " << argument_count_i32 << " = select i1 "
                << has_user_arguments << ", i32 " << raw_argument_count
                << ", i32 0\n";
        std::string argument_count = next_ssa();
        body << "  " << argument_count << " = zext i32 "
                << argument_count_i32 << " to i64\n";
        body << "  store i64 " << argument_count << ", i64* @_argc\n";
        std::string first_argument = next_ssa();
        body << "  " << first_argument
                << " = getelementptr inbounds i8*, i8** %__ferra_argv, i64 1\n";
        body << "  store i8** " << first_argument << ", i8*** @_args\n";
    }
    
    for (const auto& param : fn.params) {
        std::string ptr = next_local_alloca(param.name);
        const std::string incoming = "%__arg_" + param.name;
        BType param_type = param.type;
        BType elem_type = BType::UNKNOWN;
        
        if (is_aggregate_type(param_type)) {
            const TypeRef type_ref = type_ref_or_legacy(
                param.type_ref, param_type, param.struct_name);
            std::string struct_name = param_type == BType::TUPLE
                ? resolve_tuple_type(type_ref)
                : resolve_struct_type(type_ref);
            if (struct_name.empty()) {
                struct_name = !param.struct_name.empty() ? param.struct_name : param.type_annotation;
            }
            std::string struct_type = get_struct_type_str(struct_name);
            if (param_type == BType::STRUCT && param.type_ref.pass_by_value) {
                body << "  " << ptr << " = alloca " << struct_type << "\n";
                body << "  store " << struct_type << " " << incoming
                    << ", " << struct_type << "* " << ptr << "\n";
                vars[param.name] = {
                    param.name, ptr, IRType::STRUCT,
                    BType::UNKNOWN, 0, struct_name, false
                };
            } else {
                body << "  " << ptr << " = alloca " << struct_type << "*\n";
                body << "  store " << struct_type << "* " << incoming
                    << ", " << struct_type << "** " << ptr << "\n";
                vars[param.name] = {
                    param.name, ptr, IRType::STRUCT,
                    BType::UNKNOWN, 0, struct_name, true
                };
            }
            vars[param.name].used = false;
        }
        else if (param_type == BType::PTR) {
            body << "  " << ptr << " = alloca "
                    << get_llvm_type(param_type) << "\n";
            body << "  store " << get_llvm_type(param_type) << " "
                    << incoming << ", " << get_llvm_type(param_type)
                    << "* " << ptr << "\n";

            BType pointee_type = BType::UNKNOWN;
            std::string pointee_struct_name;
            if (param.type_ref.base == BType::STRUCT &&
                param.type_ref.is_pointer) {
                pointee_type = BType::STRUCT;
                pointee_struct_name = resolve_struct_type(param.type_ref);
                if (pointee_struct_name.empty()) {
                    pointee_struct_name = !param.struct_name.empty()
                        ? param.struct_name
                        : param.type_annotation;
                }
            }

            vars[param.name] = {
                param.name,
                ptr,
                btype_to_ir(param_type),
                pointee_type,
                0,
                pointee_struct_name
            };
        } else if (is_array_type(param_type)) {
            if (param.type_ref.base == BType::STRUCT &&
                param.type_ref.is_array) {
                std::string struct_name = resolve_struct_type(
                    param.type_ref);
                if (struct_name.empty()) {
                    struct_name = !param.struct_name.empty()
                        ? param.struct_name
                        : param.type_annotation;
                }
                const bool inline_elements =
                    inline_struct_array_types.count(struct_name);
                const std::string element_type =
                    get_struct_type_str(struct_name) +
                    (inline_elements ? "" : "*");

                body << "  " << ptr << " = alloca "
                        << element_type << "*\n";
                body << "  store " << element_type << "* "
                        << incoming << ", " << element_type << "** "
                        << ptr << "\n";

                vars[param.name] = {
                    param.name, ptr, IRType::ARR,
                    BType::STRUCT, 0, struct_name
                };
                vars[param.name].inline_struct_array =
                    inline_elements;
            } else {
                elem_type = get_array_elem_type(param_type);
                if (elem_type == BType::UNKNOWN) elem_type = BType::INT;

                body << "  " << ptr
                        << " = alloca " << get_array_ptr_type(elem_type) << "\n";
                body << "  store " << get_array_ptr_type(elem_type)
                        << " " << incoming
                        << ", " << get_array_ptr_ptr_type(elem_type)
                        << " " << ptr << "\n";

                vars[param.name] = {
                    param.name,
                    ptr,
                    IRType::ARR,
                    elem_type,
                    0
                };
            }
        } else {
            body << "  " << ptr << " = alloca " << get_llvm_type(param_type) << "\n";
            body << "  store " << get_llvm_type(param_type) << " " << incoming
                    << ", " << get_llvm_type(param_type) << "* " << ptr << "\n";
            vars[param.name] = {param.name, ptr, btype_to_ir(param_type), BType::UNKNOWN, 0};
        }
        vars[param.name].source_type = param_type;
        if (param_type == BType::FUNC) {
            vars[param.name].is_function_pointer = true;
        }
        vars[param.name].used = false;
    }
    
    if (fn.name == "main") {
        auto init_it = g_has_global_init.find(this);
        if (init_it != g_has_global_init.end() && init_it->second) {
            body << "  call void @__llbm_init_globals()\n";
        }
    }

    if (fn.body) {
        emit_statement(fn.body.get());
    }

    bool has_return = false;
    if (fn.body) {
        if (auto* block = dynamic_cast<const BlockStmt*>(fn.body.get())) {
            if (!block->statements.empty()) {
                auto* last = block->statements.back().get();
                if (dynamic_cast<const ReturnStmt*>(last)) {
                    has_return = true;
                }
            }
        } else if (dynamic_cast<const ReturnStmt*>(fn.body.get())) {
            has_return = true;
        }
    }
    
    if (!has_return) {
        if (is_aggregate_type(fn.return_type) &&
            statement_falls_through(fn.body.get())) {
            gerror("Function '" + fn.name +
                    "' can reach its end without returning " +
                    (fn.return_type == BType::TUPLE ? "tuple '" : "struct '") +
                    g_current_struct_return_type[this] + "' :/\n");
        }
        
        if (native_void) {
            body << "  ret void\n}\n\n";
        } else if (fn.return_type == BType::VOID) {
            body << "  ret i8* null\n}\n\n";
        } else if (is_aggregate_type(fn.return_type)) {
            const std::string& struct_name =
                g_current_struct_return_type[this];
            body << "  ret "
                    << get_struct_type_str(struct_name)
                    << "* null\n}\n\n";
        } else if (is_pointer_like_btype(fn.return_type)) {
            body << "  ret " << ret_type << " null\n}\n\n";
        } else if (fn.return_type == BType::F64) {
            body << "  ret double 0.0\n}\n\n";
        } else if (fn.return_type == BType::F32) {
            body << "  ret float 0.0\n}\n\n";
        } else {
            body << "  ret " << ret_type << " 0\n}\n\n";
        }
    } else {
        body << "}\n\n";
    }
    // The function body restores its outer scope before returning here, so
    // `vars` now contains its parameters. Report parameters that were never
    // read anywhere in that body.
    for (const auto& [name, info] : vars) {
        if (!info.used && info.name != "this") {
            std::cout << "Unused var: " << name << std::endl;
        }
    }

}


static const VariableExpr* assignment_root_variable(const Expr* expr) {
    if (!expr) return nullptr;
    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) return var;
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return assignment_root_variable(member->object.get());
    }
    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return assignment_root_variable(index->object.get());
    }
    if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        return assignment_root_variable(deref->operand.get());
    }
    if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        return assignment_root_variable(ref->operand.get());
    }
    return nullptr;
}

static bool is_const_assignment_target(
    LLVMEmitter& emitter,
    const Expr* lhs,
    std::string* root_name = nullptr
) {
    const VariableExpr* root = assignment_root_variable(lhs);
    if (!root) return false;
    if (root_name) *root_name = root->name;

    auto local = emitter.vars.find(root->name);
    const bool is_const_local =
        local != emitter.vars.end() && local->second.is_const;
    const bool is_const_global =
        emitter.global_consts.count(root->name) != 0;
    return is_const_local || is_const_global;
}

static bool reject_const_assignment(LLVMEmitter& emitter, const Expr* lhs) {
    std::string root_name;
    if (!is_const_assignment_target(emitter, lhs, &root_name)) return false;
    gerror("Cannot modify constant '" + root_name + "' :/\n");
    return true;
}

static bool emit_compound_assignment(
    LLVMEmitter& emitter,
    const Expr* lhs,
    const std::string& lhs_ptr,
    BType lhs_type,
    const std::string& assignment_op,
    const Expr* rhs,
    const std::string& pad
) {
    if (!is_compound_assignment_operator(assignment_op) ||
        lhs_ptr == "0" || lhs_type == BType::UNKNOWN || !rhs) {
        return false;
    }

    const std::string struct_name =
        lhs ? emitter.get_expr_struct_name(lhs) : "";
    const bool is_struct = lhs_type == BType::STRUCT ||
                           !struct_name.empty();
    if (is_struct && struct_name.empty()) {
        gerror("Cannot resolve struct type for compound assignment :/\n");
        return false;
    }

    const std::string temp_name =
        emitter.next_label("__compound_lhs_");
    BType element_type = is_array_type(lhs_type)
        ? get_array_elem_type(lhs_type)
        : BType::UNKNOWN;
    emitter.vars[temp_name] = {
        temp_name,
        lhs_ptr,
        is_array_type(lhs_type) ? IRType::ARR : btype_to_ir(lhs_type),
        element_type,
        0,
        struct_name,
        false
    };

    auto binary = std::make_unique<BinaryExpr>();
    binary->op = is_struct
        ? assignment_op
        : compound_base_operator(assignment_op);
    auto temp_expr = std::make_unique<VariableExpr>();
    temp_expr->name = temp_name;
    temp_expr->btype = lhs_type;
    binary->left = std::move(temp_expr);
    binary->right = clone_expression(*rhs);

    std::string value = emitter.emit_expression(binary.get());
    BType value_type = emitter.get_expr_type(binary.get());
    emitter.vars.erase(temp_name);

    if (is_struct) {
        return true;
    }

    IRType source_type = btype_to_ir(value_type);
    IRType target_type = btype_to_ir(lhs_type);
    if (source_type == IRType::UNKNOWN) source_type = target_type;
    if (source_type != target_type &&
        !coerce_ir_value(emitter, value, source_type, target_type, pad)) {
        gerror("Cannot store compound assignment result :/\n");
        return false;
    }

    emitter.body << pad << "store " << emitter.get_llvm_type(lhs_type)
                 << " " << value << ", "
                 << emitter.get_llvm_type(lhs_type) << "* "
                 << lhs_ptr << "\n";
    return true;
}

bool LLVMEmitter::emit_struct_constructor(
    const VarDeclStmt& var,
    const std::string& struct_name
) {
    if (!var.has_constructor_call) return true;

    auto struct_it = structs.find(struct_name);
    if (struct_name.empty() || struct_it == structs.end()) {
        gerror("Cannot resolve constructor type for '" + var.name + "' :/\n");
        return false;
    }

    const LLVMStructInfo& info = struct_it->second;
    const std::string constructor_name = info.template_name.empty()
        ? struct_name
        : info.template_name;

    CallExpr call;
    call.callee = constructor_name;
    call.is_method_call = true;

    auto receiver = std::make_unique<VariableExpr>();
    receiver->name = var.name;
    receiver->btype = BType::STRUCT;
    call.args.push_back(std::move(receiver));

    for (const auto& argument : var.constructor_args) {
        call.args.push_back(clone_expression(*argument));
    }

    std::string callee_name;
    if (!resolve_call_target(&call, callee_name, true)) return false;
    emit_expression(&call);
    return true;
}

static bool statement_falls_through(const Stmt* stmt) {
    if (!stmt) return true;

    if (dynamic_cast<const ReturnStmt*>(stmt) ||
        dynamic_cast<const BreakStmt*>(stmt) ||
        dynamic_cast<const ContinueStmt*>(stmt)) {
        return false;
    }

    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& child : block->statements) {
            if (!statement_falls_through(child.get())) return false;
        }
        return true;
    }

    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        bool compile_time_condition = false;
        if (evaluate_compile_time_condition(
                ifs->condition.get(), compile_time_condition)) {
            return statement_falls_through(
                compile_time_condition
                    ? ifs->then_branch.get()
                    : ifs->else_branch.get());
        }

        if (!ifs->else_branch) return true;
        return statement_falls_through(ifs->then_branch.get()) ||
               statement_falls_through(ifs->else_branch.get());
    }

    if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        if (!match->default_case) return true;
        for (const auto& entry : match->cases) {
            if (statement_falls_through(entry.second.get())) return true;
        }
        return statement_falls_through(match->default_case.get());
    }

    
    
    return true;
}

void LLVMEmitter::emit_cleanup_from(
    size_t first_scope,
    const std::string& pad
) {
    if (first_scope >= cleanup_scopes.size()) return;

    for (size_t scope = cleanup_scopes.size(); scope-- > first_scope;) {
        const auto& local_vars = cleanup_scopes[scope];
        for (auto variable = local_vars.rbegin();
             variable != local_vars.rend();
             ++variable) {
            auto var_it = vars.find(*variable);
            if (var_it == vars.end()) continue;

            LLVMVar& info = var_it->second;
            if (info.type != IRType::STRUCT || info.struct_name.empty()) {
                continue;
            }

            ensure_drop_function(*this, info.struct_name);
            auto drop_it = drop_functions.find(info.struct_name);
            const bool has_drop = drop_it != drop_functions.end();
            if (!has_drop && !info.owns_struct_pointer) continue;

            const std::string struct_type =
                get_struct_type_str(info.struct_name);
            VariableExpr local;
            local.name = *variable;
            const std::string object_pointer = emit_lvalue(&local);

            if (!info.drop_enabled_alloca.empty()) {
                std::string enabled = next_ssa();
                const std::string drop_label = next_label("drop_enabled");
                const std::string continue_label =
                    next_label("drop_continue");
                body << pad << enabled << " = load i1, i1* "
                     << info.drop_enabled_alloca << "\n";
                body << pad << "br i1 " << enabled << ", label %"
                     << drop_label << ", label %" << continue_label << "\n";
                body << drop_label << ":\n";
                if (has_drop) {
                    body << pad << "call void @" << drop_it->second
                         << "(" << struct_type << "* " << object_pointer << ")\n";
                }
                if (info.owns_struct_pointer) {
                    std::string raw_pointer = next_ssa();
                    body << pad << raw_pointer << " = bitcast "
                         << struct_type << "* " << object_pointer
                         << " to i8*\n";
                    body << pad << "call void @free(i8* "
                         << raw_pointer << ")\n";
                }
                body << pad << "br label %" << continue_label << "\n";
                body << continue_label << ":\n";
                continue;
            }

            if (has_drop) {
                body << pad << "call void @" << drop_it->second
                     << "(" << struct_type << "* " << object_pointer << ")\n";
            }
            if (info.owns_struct_pointer) {
                std::string raw_pointer = next_ssa();
                body << pad << raw_pointer << " = bitcast "
                     << struct_type << "* " << object_pointer
                     << " to i8*\n";
                body << pad << "call void @free(i8* "
                     << raw_pointer << ")\n";
            }
        }
    }
}

void LLVMEmitter::emit_statement(const Stmt* stmt, int indent) {
    if (!stmt) return;
    
    std::string pad(indent, ' ');
    
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        if (block->is_declaration_group) {
            for (const auto& child : block->statements) {
                emit_statement(child.get(), indent);
            }
            return;
        }

        auto vars_before = vars;

        const size_t cleanup_scope = cleanup_scopes.size();
        cleanup_scopes.emplace_back();
        bool terminated = false;

        auto emit_local_drops = [&]() {
            emit_cleanup_from(cleanup_scope, pad);
        };

        for (const auto& child :
            block->statements) {
            const auto statement_vars_before = vars;

            emit_statement(child.get(), indent);

            for (const auto& [name, info] : vars) {
                const auto previous = statement_vars_before.find(name);
                if (previous == statement_vars_before.end() ||
                    previous->second.alloca != info.alloca) {
                    cleanup_scopes[cleanup_scope].push_back(name);

                    LLVMVar& local_info = vars[name];
                    if (local_info.type == IRType::STRUCT &&
                        !local_info.struct_name.empty()) {
                        ensure_drop_function(
                            *this, local_info.struct_name);
                    }
                    if (local_info.type == IRType::STRUCT &&
                        !local_info.struct_name.empty() &&
                        (drop_functions.count(local_info.struct_name) ||
                         local_info.owns_struct_pointer)) {
                        local_info.drop_enabled_alloca = next_ssa();
                        body << pad << local_info.drop_enabled_alloca
                             << " = alloca i1\n";
                        body << pad << "store i1 true, i1* "
                             << local_info.drop_enabled_alloca << "\n";
                    }
                }
            }

            if (!statement_falls_through(child.get())) {
                terminated = true;
                break;
            }
        }

        if (!terminated) {
            emit_local_drops();
        }

        // A block owns only the bindings it introduced. Outer bindings stay
        // visible here, so compare their unique allocas before reporting.
        for (const auto& [name, info] : vars) {
            const auto previous = vars_before.find(name);
            const bool declared_in_this_scope =
                previous == vars_before.end() ||
                previous->second.alloca != info.alloca;
            if (declared_in_this_scope && !info.used && name != "this") {
                std::cout << "Unused var: " << name << std::endl;
            }
        }

        // `vars` is copied for nested lexical scopes. Preserve reads of outer
        // bindings made inside this block before restoring its parent scope.
        for (auto& [name, previous] : vars_before) {
            const auto current = vars.find(name);
            if (current != vars.end() &&
                current->second.alloca == previous.alloca) {
                previous.used = previous.used || current->second.used;
            }
        }

        vars = vars_before;
        cleanup_scopes.pop_back();

        return;
    }
    else if (auto* nodrop = dynamic_cast<const NodropStmt*>(stmt)) {
        auto variable = vars.find(nodrop->name);
        if (variable == vars.end()) {
            gerror("Unknown local variable '" + nodrop->name +
                   "' in nodrop statement :/\n");
            return;
        }
        if (variable->second.type != IRType::STRUCT ||
            variable->second.struct_name.empty() ||
            variable->second.drop_enabled_alloca.empty()) {
            gerror("nodrop expects a local object with an automatic destructor; '" +
                   nodrop->name + "' is not droppable :/\n");
            return;
        }
        body << pad << "store i1 false, i1* "
             << variable->second.drop_enabled_alloca << "\n";
    }
    else if (auto* drop_now = dynamic_cast<const DropNowStmt*>(stmt)) {
        if (!drop_now->value) return;

        const std::string struct_name =
            get_expr_struct_name(drop_now->value.get());
        if (struct_name.empty()) {
            
            
            return;
        }

        ensure_drop_function(*this, struct_name);
        const auto drop = drop_functions.find(struct_name);
        const bool has_destructor = drop != drop_functions.end();
        const std::string struct_type = get_struct_type_str(struct_name);

        if (auto* index = dynamic_cast<const IndexExpr*>(
                drop_now->value.get());
            index && !struct_array_element_name(
                *this, index->object.get()).empty()) {
            std::string element_slot = emit_lvalue(index);
            if (element_slot == "0") return;

            if (is_inline_struct_array_expression(
                    *this, index->object.get())) {
                if (has_destructor) {
                    body << pad << "call void @" << drop->second << "("
                         << struct_type << "* " << element_slot << ")\n";
                }
                body << pad << "store " << struct_type
                     << " zeroinitializer, " << struct_type << "* "
                     << element_slot << "\n";
                return;
            }

            std::string object_pointer = next_ssa();
            body << pad << object_pointer << " = load " << struct_type
                 << "*, " << struct_type << "** " << element_slot << "\n";
            std::string is_present = next_ssa();
            body << pad << is_present << " = icmp ne " << struct_type
                 << "* " << object_pointer << ", null\n";
            const std::string destroy_label = next_label("dropnow_destroy");
            const std::string continue_label = next_label("dropnow_continue");
            body << pad << "br i1 " << is_present << ", label %"
                 << destroy_label << ", label %" << continue_label << "\n";
            body << destroy_label << ":\n";
            if (has_destructor) {
                body << pad << "call void @" << drop->second << "("
                     << struct_type << "* " << object_pointer << ")\n";
            }
            std::string raw_pointer = next_ssa();
            body << pad << raw_pointer << " = bitcast " << struct_type
                 << "* " << object_pointer << " to i8*\n";
            body << pad << "call void @free(i8* " << raw_pointer << ")\n";
            body << pad << "store " << struct_type << "* null, "
                 << struct_type << "** " << element_slot << "\n";
            body << pad << "br label %" << continue_label << "\n";
            body << continue_label << ":\n";
            return;
        }

        auto* variable = dynamic_cast<const VariableExpr*>(
            drop_now->value.get());
        if (!variable) {
            if (!has_destructor) return;
            gerror("dropnow expects a local object or a struct-array element :/\n");
            return;
        }
        auto local = vars.find(variable->name);
        if (local == vars.end() || local->second.type != IRType::STRUCT) {
            return;
        }

        LLVMVar& info = local->second;
        if (!has_destructor && !info.owns_struct_pointer) {
            
            return;
        }
        std::string object_pointer = emit_lvalue(drop_now->value.get());
        if (!info.drop_enabled_alloca.empty()) {
            std::string enabled = next_ssa();
            const std::string destroy_label = next_label("dropnow_destroy");
            const std::string continue_label = next_label("dropnow_continue");
            body << pad << enabled << " = load i1, i1* "
                 << info.drop_enabled_alloca << "\n";
            body << pad << "store i1 false, i1* "
                 << info.drop_enabled_alloca << "\n";
            body << pad << "br i1 " << enabled << ", label %"
                 << destroy_label << ", label %" << continue_label << "\n";
            body << destroy_label << ":\n";
            if (has_destructor) {
                body << pad << "call void @" << drop->second << "("
                     << struct_type << "* " << object_pointer << ")\n";
            }
            if (info.owns_struct_pointer) {
                std::string raw_pointer = next_ssa();
                body << pad << raw_pointer << " = bitcast " << struct_type
                     << "* " << object_pointer << " to i8*\n";
                body << pad << "call void @free(i8* " << raw_pointer << ")\n";
            }
            body << pad << "br label %" << continue_label << "\n";
            body << continue_label << ":\n";
            return;
        }

        if (has_destructor) {
            body << pad << "call void @" << drop->second << "("
                 << struct_type << "* " << object_pointer << ")\n";
        }
        if (info.owns_struct_pointer) {
            std::string raw_pointer = next_ssa();
            body << pad << raw_pointer << " = bitcast " << struct_type
                 << "* " << object_pointer << " to i8*\n";
            body << pad << "call void @free(i8* " << raw_pointer << ")\n";
        }
    }
    else if (auto* destructure = dynamic_cast<const TupleDestructureStmt*>(stmt)) {
        const std::string tuple_name =
            get_expr_struct_name(destructure->initializer.get());
        auto tuple = structs.find(tuple_name);
        if (tuple_name.empty() || tuple == structs.end() || !tuple->second.is_tuple) {
            gerror("Tuple destructuring requires a tuple value :/\n");
            return;
        }
        if (destructure->names.size() != tuple->second.field_types.size()) {
            gerror("Tuple destructuring expects " +
                   std::to_string(tuple->second.field_types.size()) +
                   " names, got " + std::to_string(destructure->names.size()) +
                   " :/\n");
            return;
        }

        const std::string tuple_type = get_struct_type_str(tuple_name);
        const std::string tuple_value =
            emit_expression(destructure->initializer.get());
        for (size_t i = 0; i < destructure->names.size(); ++i) {
            const std::string& name = destructure->names[i];
            if (name == "_") continue;
            if (vars.count(name)) {
                gerror("Variable '" + name + "' is already declared :/\n");
                continue;
            }
            const BType element_type = tuple->second.field_types[i];
            const std::string element_llvm = is_aggregate_type(element_type)
                ? get_struct_type_str(tuple->second.field_annotations[i])
                : get_llvm_type(element_type);
            std::string field_ptr = next_ssa();
            std::string local = next_local_alloca(name);
            std::string value = next_ssa();
            body << pad << field_ptr << " = getelementptr inbounds "
                 << tuple_type << ", " << tuple_type << "* " << tuple_value
                 << ", i32 0, i32 " << i << "\n";
            body << pad << local << " = alloca " << element_llvm << "\n";
            body << pad << value << " = load " << element_llvm << ", "
                 << element_llvm << "* " << field_ptr << "\n";
            body << pad << "store " << element_llvm << " " << value << ", "
                 << element_llvm << "* " << local << "\n";
            vars[name] = {name, local,
                          is_aggregate_type(element_type) ? IRType::STRUCT
                                                          : btype_to_ir(element_type),
                          BType::UNKNOWN, 0,
                          is_aggregate_type(element_type)
                              ? tuple->second.field_annotations[i] : ""};
            vars[name].source_type = element_type;
            vars[name].is_const = destructure->is_const;
        }
    }
    else if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt)) {    
        std::string init_val;
        IRType init_type = IRType::F64;
    
        BType aggregate_type = var->type;
        if (aggregate_type == BType::UNKNOWN && var->initializer) {
            aggregate_type = get_expr_type(var->initializer.get());
        }
        if (var->initializer && var->type_ref.base == BType::TUPLE &&
            !var->type_ref.type_args.empty()) {
            expected_tuple_types[var->initializer.get()] = var->type_ref;
        }
        std::string declared_struct_name;
        if (is_aggregate_type(aggregate_type)) {
            const TypeRef type_ref = type_ref_or_legacy(
                var->type_ref, aggregate_type, var->struct_name);
            declared_struct_name = aggregate_type == BType::TUPLE
                ? resolve_tuple_type(type_ref)
                : resolve_struct_type(type_ref);
            if (declared_struct_name.empty()) declared_struct_name = var->struct_name;
            if (aggregate_type != BType::TUPLE && declared_struct_name.empty()) {
                declared_struct_name = var->type_annotation;
            }
        }
        const std::string initializer_struct_name = var->initializer
            ? get_expr_struct_name(var->initializer.get())
            : "";
        const bool inferred_struct_value =
            (var->type == BType::UNKNOWN || aggregate_type == BType::TUPLE) &&
            !initializer_struct_name.empty();

        const bool declared_inline_struct_array =
            !var->array_size && var->initializer &&
            var->type_ref.base == BType::STRUCT &&
            var->type_ref.is_array &&
            !var->type_ref.is_pointer;

        if (!var->array_size &&
            (!declared_struct_name.empty() || inferred_struct_value)) {
            const std::string struct_name = !declared_struct_name.empty()
                ? declared_struct_name
                : initializer_struct_name;
            if (!structs.count(struct_name)) {
                gerror("Unknown struct type '" + struct_name + "' :/\n");
                return;
            }
            if (structs.at(struct_name).is_opaque) {
                gerror("Cannot instantiate opaque extern struct '" +
                       struct_name + "' by value; use a pointer :/\n");
                return;
            }

            std::string struct_type = get_struct_type_str(struct_name);
            std::string ptr = next_local_alloca(var->name);

            const auto* initializer_call = var->initializer
                ? dynamic_cast<const CallExpr*>(var->initializer.get())
                : nullptr;
            const bool raw_allocation = initializer_call &&
                !initializer_call->is_method_call &&
                initializer_call->callee == "malloc";
            if (raw_allocation) {
                std::string raw_pointer = emit_expression(var->initializer.get());
                std::string struct_pointer = next_ssa();
                body << pad << struct_pointer << " = bitcast i8* "
                     << raw_pointer << " to " << struct_type << "*\n";
                body << pad << "store " << struct_type << " zeroinitializer, "
                     << struct_type << "* " << struct_pointer << "\n";
                body << pad << ptr << " = alloca " << struct_type << "*\n";
                body << pad << "store " << struct_type << "* "
                     << struct_pointer << ", " << struct_type << "** "
                     << ptr << "\n";

                vars[var->name] = {
                    var->name, ptr, IRType::STRUCT, BType::UNKNOWN,
                    0, struct_name, true
                };
                vars[var->name].is_const = var->is_const;
                vars[var->name].source_type = aggregate_type;
                vars[var->name].used = false;
                return;
            }

            const bool struct_reference_initializer =
                initializer_call && !initializer_struct_name.empty();
            if (struct_reference_initializer) {
                if (initializer_struct_name != struct_name) {
                    gerror("Cannot initialize struct '" + struct_name +
                           "' with incompatible value :/\n");
                    return;
                }

                std::string source_pointer =
                    emit_expression(var->initializer.get());
                body << pad << ptr << " = alloca " << struct_type << "*\n";
                body << pad << "store " << struct_type << "* "
                     << source_pointer << ", " << struct_type << "** "
                     << ptr << "\n";

                vars[var->name] = {
                    var->name, ptr, IRType::STRUCT, BType::UNKNOWN,
                    0, struct_name, true
                };
                std::string initializer_callee;
                if (resolve_call_target(
                        initializer_call, initializer_callee, false) &&
                    !extern_functions.count(initializer_callee)) {
                    auto return_type = func_types.find(initializer_callee);
                    vars[var->name].owns_struct_pointer =
                        return_type != func_types.end() &&
                        return_type->second == IRType::STRUCT;
                }
                vars[var->name].is_const = var->is_const;
                vars[var->name].source_type = aggregate_type;
                vars[var->name].used = false;
                return;
            }

            body << pad << ptr << " = alloca " << struct_type << "\n";

            if (var->initializer) {
                if (initializer_struct_name.empty() ||
                    initializer_struct_name != struct_name) {
                    gerror("Cannot initialize struct '" + struct_name +
                           "' with incompatible value :/\n");
                    body << pad << "store " << struct_type << " zeroinitializer, "
                         << struct_type << "* " << ptr << "\n";
                } else {
                    std::string source_ptr = emit_expression(var->initializer.get());
                    std::string loaded = next_ssa();
                    body << pad << loaded << " = load " << struct_type << ", "
                         << struct_type << "* " << source_ptr << "\n";
                    body << pad << "store " << struct_type << " " << loaded << ", "
                         << struct_type << "* " << ptr << "\n";
                }
            } else {
                body << pad << "store " << struct_type << " zeroinitializer, "
                     << struct_type << "* " << ptr << "\n";
            }
            vars[var->name] = {var->name, ptr, IRType::STRUCT, BType::UNKNOWN, 0, struct_name};
            vars[var->name].is_const = var->is_const;
            vars[var->name].source_type = aggregate_type;
            vars[var->name].used = false;
            if (var->has_constructor_call) {
                emit_struct_constructor(*var, struct_name);
            }
            return;
        }
        
        if (declared_inline_struct_array) {
            std::string element_struct_name =
                resolve_struct_type(var->type_ref);
            if (element_struct_name.empty()) {
                element_struct_name = var->type_ref.name;
            }
            if (element_struct_name.empty() ||
                !structs.count(element_struct_name)) {
                gerror("Unknown struct array element type for '" +
                       var->name + "' :/\n");
                return;
            }
            if (structs.at(element_struct_name).is_opaque) {
                gerror("Cannot allocate an inline array of opaque extern struct '" +
                       element_struct_name + "' :/\n");
                return;
            }

            BType initializer_type = get_expr_type(var->initializer.get());
            if (initializer_type == BType::UNKNOWN) {
                initializer_type = var->initializer->btype;
            }
            if (!is_pointer_like_btype(initializer_type)) {
                gerror("Struct array initializer for '" + var->name +
                       "' must produce allocated storage or null :/\n");
                return;
            }

            std::string storage = emit_expression(var->initializer.get());
            std::string source_pointer_type =
                emitted_pointer_type(*this, var->initializer.get());
            if (source_pointer_type.empty()) {
                gerror("Struct array initializer for '" + var->name +
                       "' did not produce an address :/\n");
                return;
            }

            const std::string element_type =
                get_struct_type_str(element_struct_name);
            const std::string array_pointer_type = element_type + "*";
            std::string array_pointer = storage;
            if (is_null_expression(var->initializer.get())) {
                array_pointer = "null";
            } else if (source_pointer_type != array_pointer_type) {
                array_pointer = next_ssa();
                body << pad << array_pointer << " = bitcast "
                     << source_pointer_type << " " << storage << " to "
                     << array_pointer_type << "\n";
            }

            std::string slot = next_local_alloca(var->name);
            body << pad << slot << " = alloca " << array_pointer_type << "\n";
            body << pad << "store " << array_pointer_type << " "
                 << array_pointer << ", " << array_pointer_type << "* "
                 << slot << "\n";

            vars[var->name] = {
                var->name, slot, IRType::ARR, BType::STRUCT, 0
            };
            vars[var->name].struct_name = element_struct_name;
            vars[var->name].source_type = BType::ARR;
            vars[var->name].is_const = var->is_const;
            vars[var->name].inline_struct_array = true;
            vars[var->name].used = false;
            return;
        }
        else if (var->array_size) {
            std::string size_val = emit_expression(var->array_size.get());
            if (!normalize_integer_to_i64(
                    *this,
                    var->array_size.get(),
                    size_val,
                    "Array size",
                    pad)) {
                return;
            }
            
            
            BType elem_type = var->type;
            if (elem_type == BType::UNKNOWN) {
                elem_type = BType::INT;
            }
            IRType elem_ir_type = btype_to_ir(elem_type);

            std::string element_struct_name;
            if (is_aggregate_type(elem_type)) {
                element_struct_name = elem_type == BType::TUPLE
                    ? resolve_tuple_type(var->type_ref)
                    : resolve_struct_type(var->type_ref);
                if (element_struct_name.empty()) {
                    element_struct_name = var->struct_name;
                }
                if (element_struct_name.empty() ||
                    !structs.count(element_struct_name)) {
                    gerror("Unknown aggregate array element type for '" +
                           var->name + "' :/\n");
                    return;
                }
            }

            const std::string element_value_type =
                is_aggregate_type(elem_type)
                    ? get_struct_type_str(element_struct_name) + "*"
                    : llvm_type_str(elem_ir_type);

            int elem_size = is_aggregate_type(elem_type)
                ? static_cast<int>(sizeof(void*))
                : getTypeSize(elem_type);
            
            
            std::string arr_ptr = next_local_alloca(var->name);
            body << pad << arr_ptr << " = alloca "
                 << element_value_type << "*\n";
            
            
            std::string byte_size = next_ssa();
            body << pad << byte_size << " = mul i64 " << size_val << ", " << elem_size << "\n";
            
            
            std::string malloc_ptr = next_ssa();
            body << pad << malloc_ptr << " = call i8* @malloc(i64 " << byte_size << ")\n";
            
            
            std::string arr_elem_ptr = next_ssa();
            body << pad << arr_elem_ptr << " = bitcast i8* " << malloc_ptr
                 << " to " << element_value_type << "*\n";
            
            
            body << pad << "store " << element_value_type << "* "
                 << arr_elem_ptr << ", " << element_value_type << "** "
                 << arr_ptr << "\n";
            
            if (var->initializer) {
                if (auto* arr_init = dynamic_cast<const ArrayExpr*>(var->initializer.get())) {
                    
                    for (size_t i = 0; i < arr_init->elements.size(); i++) {
                        std::string elem_val = emit_expression(arr_init->elements[i].get());

                        if (is_aggregate_type(elem_type)) {
                            const std::string actual_struct =
                                get_expr_struct_name(arr_init->elements[i].get());
                            if (actual_struct != element_struct_name) {
                                gerror("Cannot initialize struct array element with an incompatible value :/\n");
                                continue;
                            }
                            std::string gep = next_ssa();
                            body << pad << gep << " = getelementptr inbounds "
                                 << element_value_type << ", "
                                 << element_value_type << "* " << arr_elem_ptr
                                 << ", i64 " << i << "\n";
                            body << pad << "store " << element_value_type << " "
                                 << elem_val << ", " << element_value_type
                                 << "* " << gep << "\n";
                            continue;
                        }

                        BType source_btype = get_expr_type(arr_init->elements[i].get());
                        if (source_btype == BType::UNKNOWN) {
                            source_btype = arr_init->elements[i]->btype;
                        }
                        IRType source_ir_type = btype_to_ir(source_btype);
                        if (source_ir_type == IRType::UNKNOWN) {
                            source_ir_type = elem_ir_type;
                        }
                        if (source_ir_type != elem_ir_type &&
                            !coerce_ir_value(*this, elem_val, source_ir_type,
                                             elem_ir_type, pad,
                                             is_unsigned_integer_type(source_btype))) {
                            gerror("Cannot initialize array element with an incompatible value :/\n");
                            continue;
                        }
                        std::string gep = next_ssa();
                        body << pad << gep << " = getelementptr inbounds " << llvm_type_str(elem_ir_type) << ", " << llvm_type_str(elem_ir_type) << "* " << arr_elem_ptr << ", i64 " << i << "\n";
                        body << pad << "store " << llvm_type_str(elem_ir_type) << " " << elem_val << ", " << llvm_type_str(elem_ir_type) << "* " << gep << "\n";
                    }
                }
            }
            
            
            int array_size = 0;
            if (auto* num = dynamic_cast<const NumberExpr*>(var->array_size.get())) {
                array_size = (int)num->value;
            }
            vars[var->name] = {var->name, arr_ptr, IRType::ARR, elem_type, array_size};
            vars[var->name].is_const = var->is_const;
            vars[var->name].struct_name = element_struct_name;
            vars[var->name].used = false;
        } else if (var->initializer) {
            BType var_type = var->type;
            if (var_type == BType::UNKNOWN) {
                var_type = get_expr_type(var->initializer.get());
                
                if (var_type == BType::UNKNOWN) {
                    var_type = var->initializer->btype;
                }
            }
            
            if (auto* array =
                    dynamic_cast<const ArrayExpr*>(var->initializer.get());
                array && is_array_type(var_type)) {
                init_val = emit_array_literal(array,
                                              get_array_elem_type(var_type));
            } else {
                init_val = emit_expression(var->initializer.get());
            }

            BType initializer_type = get_expr_type(var->initializer.get());
            if (initializer_type == BType::UNKNOWN &&
                is_null_expression(var->initializer.get())) {
                initializer_type = BType::PTR;
            }

            init_type = btype_to_ir(initializer_type);
            
            IRType var_ir_type = btype_to_ir(var_type);
            std::string val_to_store = init_val;
            
            if (is_pointer_like_btype(var_type) &&
                !initializer_struct_name.empty()) {
                const std::string source_pointer_type =
                    get_struct_type_str(initializer_struct_name) + "*";
                const std::string target_pointer_type =
                    llvm_ir_type_name(var_ir_type);
                if (source_pointer_type != target_pointer_type) {
                    std::string erased_pointer = next_ssa();
                    body << pad << erased_pointer << " = bitcast "
                         << source_pointer_type << " " << val_to_store
                         << " to " << target_pointer_type << "\n";
                    val_to_store = erased_pointer;
                }
                init_type = var_ir_type;
            }

            if (var_ir_type == IRType::UNKNOWN && init_type != IRType::UNKNOWN) {
                var_ir_type = init_type;
            } else if (init_type == IRType::UNKNOWN) {
                init_type = var_ir_type;
            } else if (init_type != var_ir_type &&
                       !coerce_ir_value(
                           *this, val_to_store, init_type, var_ir_type, pad)) {
                gerror("Cannot initialize '" + var->name +
                       "' with an incompatible value :/\n");
                val_to_store = is_pointer_like_btype(var_type) ? "null" : "0";
            }

            if (is_pointer_like_btype(var_type) &&
                val_to_store == "0" &&
                !is_null_expression(var->initializer.get())) {
                gerror("Pointer initializer for '" + var->name +
                       "' did not produce an address :/\n");
                val_to_store = "null";
            }
            
            std::string ptr = next_local_alloca(var->name);
            
            body << pad << ptr << " = alloca " << get_llvm_type(var_type) << "\n";
            
            body << pad << "store " << get_llvm_type(var_type) << " " << val_to_store
                 << ", " << get_llvm_type(var_type) << "* " << ptr << "\n";
            const bool declared_struct_pointer =
                var->type_ref.is_pointer &&
                var->type_ref.base == BType::STRUCT &&
                !var->type_ref.name.empty();
            BType elem_type = is_array_type(var_type)
                ? get_array_elem_type(var_type)
                : BType::UNKNOWN;
            if (var_type == BType::PTR || is_pointer_type(var_type)) {
                if (declared_struct_pointer) {
                    elem_type = BType::STRUCT;
                } else if (!initializer_struct_name.empty()) {
                    elem_type = BType::STRUCT;
                } else {
                    elem_type = pointer_pointee_type_from_expression(
                        *this, var->initializer.get());
                    if (elem_type == BType::UNKNOWN) {
                        elem_type = get_pointer_base_type(var_type);
                    }
                }
            }
            vars[var->name] = {
                var->name,
                ptr,
                is_array_type(var_type) ? IRType::ARR : var_ir_type,
                elem_type,
                0
            };
            vars[var->name].is_const = var->is_const;
            vars[var->name].source_type = var_type;
            vars[var->name].used = false;
            if (elem_type == BType::STRUCT) {
                if (declared_struct_pointer) {
                    vars[var->name].struct_name =
                        resolve_struct_type(var->type_ref);
                    if (vars[var->name].struct_name.empty()) {
                        vars[var->name].struct_name = var->type_ref.name;
                    }
                } else if (!initializer_struct_name.empty()) {
                    vars[var->name].struct_name = initializer_struct_name;
                } else {
                    vars[var->name].struct_name =
                        pointer_pointee_struct_name(*this,
                                                    var->initializer.get());
                }
            }
            if (var_type == BType::FUNC) {
                vars[var->name].is_function_pointer = true;
            }
            const bool has_function_signature =
                remember_function_pointer_signature(
                    *this, vars[var->name], var->initializer.get(), false);
            if (var_type == BType::FUNC &&
                !has_function_signature &&
                !is_null_expression(var->initializer.get())) {
                gerror("Initializer for function pointer '" + var->name +
                       "' is not a function :/\n");
            }
            vars[var->name].used = false;
        } else {
            BType var_type = var->type;
            if (var_type == BType::UNKNOWN) {
                var_type = BType::INT;
            }
            
            std::string ptr = next_local_alloca(var->name);
            const std::string default_value =
                is_pointer_like_btype(var_type) ? "null" : "0";

            const std::string value_type_str =
                llvm_value_type_for_btype(var_type);
            const std::string storage_type_str =
                llvm_storage_type_for_btype(var_type);

            body << pad << ptr
                 << " = alloca " << value_type_str << "\n";
            body << pad << "store " << value_type_str
                 << " " << default_value
                 << ", " << storage_type_str
                 << " " << ptr << "\n";

            BType elem_type = is_array_type(var_type)
                ? get_array_elem_type(var_type)
                : BType::UNKNOWN;
            if (var_type == BType::PTR || is_pointer_type(var_type)) {
                elem_type = get_pointer_base_type(var_type);
            }

            vars[var->name] = {
                var->name,
                ptr,
                is_array_type(var_type) ? IRType::ARR : btype_to_ir(var_type),
                elem_type,
                0
            };
            vars[var->name].is_const = var->is_const;
            vars[var->name].source_type = var_type;
            if (var_type == BType::FUNC) {
                vars[var->name].is_function_pointer = true;
            }
            vars[var->name].used = false;
        }
    }
    else if (auto* arr_assign = dynamic_cast<const ArrayAssignStmt*>(stmt)) {
        VariableExpr root;
        root.name = arr_assign->array_name;
        if (reject_const_assignment(*this, &root)) return;

        std::string arr_ptr;
        BType elem_type = BType::INT;  
        if (vars.count(arr_assign->array_name)) {
            LLVMVar& v = vars[arr_assign->array_name];
            elem_type = v.elem_type;
            if (elem_type == BType::UNKNOWN) {
                elem_type = BType::INT;
            }
            IRType elem_ir_type = btype_to_ir(elem_type);
            
            
            std::string ssa = next_ssa();
            body << pad << ssa << " = load " << llvm_type_str(elem_ir_type) << "*, " << llvm_type_str(elem_ir_type) << "** " << v.alloca << "\n";
            arr_ptr = ssa;
        }
        
        std::string index_val = emit_expression(arr_assign->index.get());
        if (!normalize_integer_to_i64(
                *this, arr_assign->index.get(), index_val, "Array index", pad)) {
            return;
        }
        
        IRType elem_ir_type = btype_to_ir(elem_type);
        std::string elem_ptr = next_ssa();
        body << pad << elem_ptr << " = getelementptr inbounds " << llvm_type_str(elem_ir_type) << ", " << llvm_type_str(elem_ir_type) << "* " << arr_ptr 
             << ", i64 " << index_val << "\n";

        if (is_compound_assignment_operator(arr_assign->assignment_op)) {
            emit_compound_assignment(
                *this, nullptr, elem_ptr, elem_type,
                arr_assign->assignment_op, arr_assign->value.get(), pad);
            return;
        }

        std::string val = emit_expression(arr_assign->value.get());
        
        body << pad << "store " << llvm_type_str(elem_ir_type) << " " << val << ", " << llvm_type_str(elem_ir_type) << "* " << elem_ptr << "\n";
    }
    else if (auto* member_assign = dynamic_cast<const MemberAssignStmt*>(stmt)) {
        if (reject_const_assignment(*this, member_assign->lhs.get())) return;

        std::string lhs_ptr = emit_lvalue(member_assign->lhs.get());
        BType lhs_type = get_expr_type(member_assign->lhs.get());

        if (lhs_ptr == "0" || lhs_type == BType::UNKNOWN) {
            gerror("Invalid assignment l-value :/\n");
            return;
        }

        if (auto* member =
                dynamic_cast<const MemberExpr*>(member_assign->lhs.get())) {
            const std::string element_struct =
                struct_array_element_name(*this, member);
            if (!element_struct.empty()) {
                if (is_compound_assignment_operator(
                        member_assign->assignment_op)) {
                    gerror("Compound assignment is invalid for struct-array fields :/\n");
                    return;
                }

                const bool inline_field =
                    struct_array_field_is_inline(*this, member);
                const std::string field_type =
                    get_struct_type_str(element_struct) +
                    (inline_field ? "*" : "**");
                std::string value;

                if (is_null_expression(member_assign->value.get())) {
                    value = "null";
                } else {
                    value = emit_expression(member_assign->value.get());
                    std::string source_type;

                    if (auto* source_var = dynamic_cast<const VariableExpr*>(
                            member_assign->value.get())) {
                        auto source = vars.find(source_var->name);
                        if (source != vars.end() &&
                            source->second.type == IRType::ARR) {
                            BType element_type = source->second.elem_type;
                            if (element_type == BType::UNKNOWN) {
                                element_type = BType::INT;
                            }
                            if (is_aggregate_type(element_type) &&
                                !source->second.struct_name.empty()) {
                                if (source->second.struct_name !=
                                    element_struct) {
                                    gerror("Struct-array field expects elements of type '" +
                                           element_struct + "' :/\n");
                                    return;
                                }
                                if (source->second.inline_struct_array !=
                                    inline_field) {
                                    gerror("Cannot mix contiguous and pointer-slot storage for struct-array field '" +
                                           member->member + "' :/\n");
                                    return;
                                }
                                source_type = get_struct_type_str(
                                    source->second.struct_name) +
                                    (source->second.inline_struct_array
                                        ? "*"
                                        : "**");
                            } else {
                                if (inline_field) {
                                    gerror("Inline struct-array field '" +
                                           member->member +
                                           "' expects contiguous '" +
                                           element_struct + "' storage :/\n");
                                    return;
                                }
                                source_type =
                                    llvm_type_str(btype_to_ir(element_type)) + "*";
                            }
                        }
                    }

                    if (source_type.empty()) {
                        gerror("Struct-array field expects allocated array storage or null :/\n");
                        return;
                    }

                    if (source_type != field_type) {
                        std::string cast = next_ssa();
                        body << pad << cast << " = bitcast " << source_type
                             << " " << value << " to " << field_type << "\n";
                        value = cast;
                    }
                }

                body << pad << "store " << field_type << " " << value
                     << ", " << field_type << "* " << lhs_ptr << "\n";
                return;
            }
        }

        if (auto* index = dynamic_cast<const IndexExpr*>(member_assign->lhs.get())) {
            const std::string element_struct =
                struct_array_element_name(*this, index->object.get());
            if (!element_struct.empty()) {
                if (is_compound_assignment_operator(
                        member_assign->assignment_op)) {
                    gerror("Compound assignment is invalid for struct-array elements :/\n");
                    return;
                }

                if (is_inline_struct_array_expression(
                        *this, index->object.get())) {
                    if (is_null_expression(member_assign->value.get())) {
                        gerror("Inline struct-array element cannot be null :/\n");
                        return;
                    }

                    const std::string actual_struct =
                        get_expr_struct_name(member_assign->value.get());
                    if (actual_struct != element_struct) {
                        gerror("Struct-array element expects '" +
                               element_struct + "' :/\n");
                        return;
                    }

                    const std::string struct_type =
                        get_struct_type_str(element_struct);
                    std::string source_pointer =
                        emit_expression(member_assign->value.get());
                    if (source_pointer == "0") return;

                    std::string aggregate = next_ssa();
                    body << pad << aggregate << " = load " << struct_type
                         << ", " << struct_type << "* " << source_pointer
                         << "\n";
                    body << pad << "store " << struct_type << " "
                         << aggregate << ", " << struct_type << "* "
                         << lhs_ptr << "\n";

                    
                    
                    
                    
                    if (auto* source_call = dynamic_cast<const CallExpr*>(
                            member_assign->value.get())) {
                        std::string callee;
                        if (resolve_call_target(
                                source_call, callee, false) &&
                            !extern_functions.count(callee) &&
                            func_types.count(callee) &&
                            func_types.at(callee) == IRType::STRUCT) {
                            std::string raw_pointer = next_ssa();
                            body << pad << raw_pointer << " = bitcast "
                                 << struct_type << "* " << source_pointer
                                 << " to i8*\n";
                            body << pad << "call void @free(i8* "
                                 << raw_pointer << ")\n";
                        }
                    }
                    return;
                }

                std::string value;
                if (is_null_expression(member_assign->value.get())) {
                    value = "null";
                } else {
                    const std::string actual_struct =
                        get_expr_struct_name(member_assign->value.get());
                    if (actual_struct != element_struct) {
                        gerror("Struct-array element expects '" +
                               element_struct + "' or null :/\n");
                        return;
                    }
                    value = emit_expression(member_assign->value.get());

                    
                    
                    
                    
                    
                    
                    
                    bool needs_heap_copy =
                        dynamic_cast<const StructLiteralExpr*>(
                            member_assign->value.get()) != nullptr ||
                        dynamic_cast<const MemberExpr*>(
                            member_assign->value.get()) != nullptr;

                    if (auto* source_var =
                            dynamic_cast<const VariableExpr*>(
                                member_assign->value.get())) {
                        auto source = vars.find(source_var->name);
                        needs_heap_copy =
                            source != vars.end() &&
                            source->second.type == IRType::STRUCT;
                    }

                    if (needs_heap_copy) {
                        const std::string struct_type =
                            get_struct_type_str(element_struct);
                        std::string size_pointer = next_ssa();
                        body << pad << size_pointer << " = getelementptr "
                             << struct_type << ", " << struct_type
                             << "* null, i32 1\n";

                        std::string byte_size = next_ssa();
                        body << pad << byte_size << " = ptrtoint "
                             << struct_type << "* " << size_pointer
                             << " to i64\n";

                        std::string raw_copy = next_ssa();
                        body << pad << raw_copy
                             << " = call i8* @malloc(i64 "
                             << byte_size << ")\n";

                        std::string heap_copy = next_ssa();
                        body << pad << heap_copy << " = bitcast i8* "
                             << raw_copy << " to " << struct_type << "*\n";

                        std::string aggregate = next_ssa();
                        body << pad << aggregate << " = load "
                             << struct_type << ", " << struct_type << "* "
                             << value << "\n";
                        body << pad << "store " << struct_type << " "
                             << aggregate << ", " << struct_type << "* "
                             << heap_copy << "\n";
                        value = heap_copy;
                    }
                }

                const std::string element_type =
                    get_struct_type_str(element_struct) + "*";
                body << pad << "store " << element_type << " " << value
                     << ", " << element_type << "* " << lhs_ptr << "\n";
                return;
            }
        }

        
        
        
        
        
        if (is_struct_type(lhs_type)) {
            if (is_compound_assignment_operator(
                    member_assign->assignment_op)) {
                gerror("Compound assignment is invalid for struct fields :/\n");
                return;
            }

            const std::string expected_struct =
                get_expr_struct_name(member_assign->lhs.get());
            const std::string actual_struct =
                get_expr_struct_name(member_assign->value.get());
            if (expected_struct.empty() || actual_struct != expected_struct) {
                gerror("Cannot assign incompatible struct value to field :/\n");
                return;
            }

            std::string source_pointer =
                emit_expression(member_assign->value.get());
            const std::string struct_type =
                get_struct_type_str(expected_struct);
            std::string aggregate = next_ssa();
            body << pad << aggregate << " = load " << struct_type
                 << ", " << struct_type << "* " << source_pointer << "\n";
            body << pad << "store " << struct_type << " " << aggregate
                 << ", " << struct_type << "* " << lhs_ptr << "\n";
            return;
        }

        if (is_compound_assignment_operator(member_assign->assignment_op)) {
            emit_compound_assignment(
                *this, member_assign->lhs.get(), lhs_ptr, lhs_type,
                member_assign->assignment_op, member_assign->value.get(), pad);
            return;
        }

        std::string rhs_val;
        if (auto* array = dynamic_cast<const ArrayExpr*>(member_assign->value.get());
            array && is_array_type(lhs_type)) {
            
            
            
            BType element_type = get_array_elem_type(lhs_type);
            if (element_type == BType::UNKNOWN) element_type = BType::INT;
            rhs_val = emit_array_literal(array, element_type);
        } else {
            rhs_val = emit_expression(member_assign->value.get());
            BType rhs_type = get_expr_type(member_assign->value.get());
            if (rhs_type == BType::UNKNOWN) rhs_type = member_assign->value->btype;

            IRType rhs_ir = btype_to_ir(rhs_type);
            IRType lhs_ir = btype_to_ir(lhs_type);
            if (rhs_ir != IRType::UNKNOWN && rhs_ir != lhs_ir &&
                !coerce_ir_value(*this, rhs_val, rhs_ir, lhs_ir, pad)) {
                gerror("Cannot assign incompatible value to struct field :/\n");
                return;
            }
        }
        body << pad << "store " << get_llvm_type(lhs_type) << " " << rhs_val
             << ", " << get_llvm_type(lhs_type) << "* " << lhs_ptr << "\n";
    }
    else if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        VariableExpr target;
        target.name = assign->name;
        if (reject_const_assignment(*this, &target)) return;

        if (is_compound_assignment_operator(assign->assignment_op)) {
            auto lhs = std::make_unique<VariableExpr>();
            lhs->name = assign->name;
            std::string lhs_ptr = emit_lvalue(lhs.get());
            BType lhs_type = get_expr_type(lhs.get());
            if (!emit_compound_assignment(
                    *this, lhs.get(), lhs_ptr, lhs_type,
                    assign->assignment_op, assign->value.get(), pad)) {
                gerror("Invalid compound assignment target '" +
                       assign->name + "' :/\n");
            }
            return;
        }

        std::string value = emit_expression(assign->value.get());
        BType source_btype = get_expr_type(assign->value.get());
        if (source_btype == BType::UNKNOWN) source_btype = assign->value->btype;
        IRType source_type = btype_to_ir(source_btype);

        if (vars.count(assign->name)) {
            LLVMVar& variable = vars[assign->name];
            if (variable.type == IRType::STRUCT) {
                if (auto* binary = dynamic_cast<const BinaryExpr*>(assign->value.get())) {
                    if (auto* left = dynamic_cast<const VariableExpr*>(binary->left.get())) {
                        if (left->name == assign->name) return;
                    }
                }
                gerror("General struct assignment is not implemented; use an in-place method or operator :/\n");
                return;
            }

            IRType assigned_return_type = IRType::UNKNOWN;
            std::vector<IRType> assigned_argument_types;
            const bool assigns_function = function_signature_from_expression(
                *this,
                assign->value.get(),
                assigned_return_type,
                assigned_argument_types
            );
            if (assigns_function) {
                if (!remember_function_pointer_signature(
                        *this, variable, assign->value.get(), true)) {
                    return;
                }
            } else if (variable.is_function_pointer &&
                       !is_null_expression(assign->value.get())) {
                gerror("Cannot assign a non-function value to function pointer '" +
                       assign->name + "' :/\n");
                return;
            }

            if ((variable.source_type == BType::PTR ||
                 is_pointer_type(variable.source_type)) &&
                !assigns_function &&
                !is_null_expression(assign->value.get())) {
                const BType pointee_type = pointer_pointee_type_from_expression(
                    *this, assign->value.get());
                if (pointee_type != BType::UNKNOWN) {
                    variable.elem_type = pointee_type;
                    if (pointee_type == BType::STRUCT) {
                        variable.struct_name = pointer_pointee_struct_name(
                            *this, assign->value.get());
                    } else {
                        variable.struct_name.clear();
                    }
                }
            }

            if (source_type == IRType::UNKNOWN) source_type = variable.type;
            if (source_type != variable.type &&
                !coerce_ir_value(*this, value, source_type, variable.type, pad)) {
                gerror("Cannot assign incompatible value to '" + assign->name + "' :/\n");
                return;
            }

            if (is_ir_pointer(variable.type) && value == "0" &&
                !is_null_expression(assign->value.get())) {
                gerror("Pointer assignment to '" + assign->name +
                       "' did not produce an address :/\n");
                value = "null";
            }

            const std::string type_name = llvm_ir_type_name(variable.type);
            body << pad << "store " << type_name << " " << value
                 << ", " << type_name << "* " << variable.alloca << "\n";
            return;
        }

        if (global_vars.count(assign->name)) {
            IRType target_type = global_vars[assign->name];
            if (target_type == IRType::STRUCT) {
                gerror("General global struct assignment is not implemented; assign its fields instead :/\n");
                return;
            }

            if (source_type == IRType::UNKNOWN) source_type = target_type;
            if (source_type != target_type &&
                !coerce_ir_value(*this, value, source_type, target_type, pad)) {
                gerror("Cannot assign incompatible value to global '" + assign->name + "' :/\n");
                return;
            }

            const std::string type_name = llvm_ir_type_name(target_type);
            body << pad << "store " << type_name << " " << value
                 << ", " << type_name << "* @" << assign->name << "\n";
        }
    }
    else if (auto* deref_assign = dynamic_cast<const DerefAssignStmt*>(stmt)) {
        if (reject_const_assignment(*this, deref_assign->pointer.get())) return;

        std::string ptr_val = emit_expression(deref_assign->pointer.get());
        BType pointer_type = get_expr_type(deref_assign->pointer.get());
        BType value_type = pointer_pointee_type_from_expression(
            *this, deref_assign->pointer.get());
        if (value_type == BType::UNKNOWN) {
            value_type = get_pointer_base_type(pointer_type);
        }
        if (value_type == BType::UNKNOWN) value_type = BType::INT;
        ptr_val = cast_pointer_to_pointee(
            *this, ptr_val, pointer_type, value_type, pad);

        if (is_compound_assignment_operator(deref_assign->assignment_op)) {
            emit_compound_assignment(
                *this, nullptr, ptr_val, value_type,
                deref_assign->assignment_op, deref_assign->value.get(), pad);
            return;
        }
        
        
        std::string val = emit_expression(deref_assign->value.get());
        BType source_btype = get_expr_type(deref_assign->value.get());
        if (source_btype == BType::UNKNOWN) {
            source_btype = deref_assign->value->btype;
        }
        IRType source_ir_type = btype_to_ir(source_btype);
        IRType value_ir_type = btype_to_ir(value_type);
        if (source_ir_type == IRType::UNKNOWN) source_ir_type = value_ir_type;
        if (source_ir_type != value_ir_type &&
            !coerce_ir_value(
                *this, val, source_ir_type, value_ir_type, pad)) {
            gerror("Cannot assign incompatible value through pointer :/\n");
            return;
        }

        body << pad << "store " << llvm_type_str(value_ir_type) << " " << val
             << ", " << llvm_ptr_type_str(value_ir_type) << " " << ptr_val << "\n";
    }
    else if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        if (ret->value) {
            if (current_fn_return_type == BType::TUPLE &&
                dynamic_cast<const TupleExpr*>(ret->value.get())) {
                
                
                auto expected = expected_tuple_types.find(ret->value.get());
                if (expected == expected_tuple_types.end()) {
                    expected_tuple_types.emplace(
                        ret->value.get(),
                        TypeRef{});
                    expected_tuple_types[ret->value.get()] = TypeRef{};
                }
                expected_tuple_types[ret->value.get()] = TypeRef{};
                expected_tuple_types[ret->value.get()].base = BType::TUPLE;
                const std::string& tuple_name =
                    g_current_struct_return_type[this];
                auto tuple = structs.find(tuple_name);
                if (tuple != structs.end()) {
                    expected_tuple_types[ret->value.get()].type_args =
                        tuple->second.tuple_element_types;
                }
            }
            std::string val = emit_expression(ret->value.get());

            const bool native_void =
                func_types.count(current_function_name) &&
                func_types.at(current_function_name) == IRType::VOID;
            if (native_void) {
                if (!is_null_expression(ret->value.get())) {
                    gerror("Native void function '" + current_function_name +
                           "' cannot return a value :/\n");
                    return;
                }
                emit_cleanup_from(0, pad);
                body << pad << "ret void\n";
                return;
            }

            if (is_aggregate_type(current_fn_return_type)) {
                const std::string& expected_struct =
                    g_current_struct_return_type[this];

                if (expected_struct.empty()) {
                    gerror("Unknown aggregate return type :/\n");
                    return;
                }

                std::string actual_struct =
                    get_expr_struct_name(ret->value.get());

                if (!actual_struct.empty() &&
                    actual_struct != expected_struct) {
                    gerror(
                        "Returned aggregate '" + actual_struct +
                        "' does not match '" + expected_struct +
                        "' :/\n"
                    );
                    return;
                }

                const std::string struct_type =
                    get_struct_type_str(expected_struct);
                std::string return_pointer = val;

                
                
                
                
                
                
                bool transfers_existing_wrapper = false;
                if (auto* returned_variable =
                        dynamic_cast<const VariableExpr*>(ret->value.get())) {
                    auto source = vars.find(returned_variable->name);
                    if (source != vars.end() &&
                        source->second.type == IRType::STRUCT) {
                        LLVMVar& source_info = source->second;
                        transfers_existing_wrapper =
                            source_info.struct_pointer_slot &&
                            source_info.owns_struct_pointer;

                        if (source_info.struct_pointer_slot &&
                            !source_info.owns_struct_pointer &&
                            drop_functions.count(expected_struct)) {
                            gerror("Cannot return borrowed struct '" +
                                   expected_struct +
                                   "' by value; return an owned local value :/\n");
                            return;
                        }

                        if (!source_info.drop_enabled_alloca.empty()) {
                            body << pad << "store i1 false, i1* "
                                 << source_info.drop_enabled_alloca << "\n";
                        }
                    }
                }

                if (!transfers_existing_wrapper) {
                    std::string size_pointer = next_ssa();
                    body << pad << size_pointer << " = getelementptr "
                         << struct_type << ", " << struct_type
                         << "* null, i32 1\n";
                    std::string byte_size = next_ssa();
                    body << pad << byte_size << " = ptrtoint "
                         << struct_type << "* " << size_pointer
                         << " to i64\n";
                    std::string raw_pointer = next_ssa();
                    body << pad << raw_pointer
                         << " = call i8* @malloc(i64 " << byte_size << ")\n";
                    return_pointer = next_ssa();
                    body << pad << return_pointer << " = bitcast i8* "
                         << raw_pointer << " to " << struct_type << "*\n";
                    std::string aggregate = next_ssa();
                    body << pad << aggregate << " = load " << struct_type
                         << ", " << struct_type << "* " << val << "\n";
                    body << pad << "store " << struct_type << " "
                         << aggregate << ", " << struct_type << "* "
                         << return_pointer << "\n";
                }

                emit_cleanup_from(0, pad);
                body << pad << "ret "
                     << struct_type << "* " << return_pointer << "\n";
                return;
            }

            BType target_type = current_fn_return_type;
            if (target_type == BType::UNKNOWN) target_type = BType::INT;

            BType value_type = get_expr_type(ret->value.get());
            if (is_null_expression(ret->value.get()) &&
                is_pointer_like_btype(target_type)) {
                value_type = target_type;
                val = "null";
            } else if (value_type == BType::UNKNOWN) {
                value_type = BType::INT;
            }

            IRType value_ir = btype_to_ir(value_type);
            IRType target_ir = btype_to_ir(target_type);

            const bool compatible_pointer_return =
                pointer_types_compatible(value_type, target_type);

            if (compatible_pointer_return) {
                val = cast_pointer_value(
                    *this,
                    val,
                    value_type,
                    target_type
                );
                value_ir = target_ir;
            }

            if (value_ir != target_ir &&
                !compatible_pointer_return) {
                if (!coerce_ir_value(*this, val, value_ir, target_ir, pad)) {
                    gerror(
                        "Return type mismatch in '" + current_function_name +
                        "'. Got: '" + llvm_value_type_for_btype(value_type) +
                        "', expected: '" + llvm_value_type_for_btype(target_type) +
                        "' :/\n");
                    return;
                }
            }
            
            emit_cleanup_from(0, pad);
            body << pad << "ret "
                 << llvm_value_type_for_btype(target_type)
                 << " " << val << "\n";
        } else {
            emit_cleanup_from(0, pad);
            const bool native_void =
                func_types.count(current_function_name) &&
                func_types.at(current_function_name) == IRType::VOID;
            if (native_void) {
                body << pad << "ret void\n";
            } else if (current_fn_return_type == BType::VOID) {
                body << pad << "ret i8* null\n";
            } else {
                gerror("Function '" + current_function_name +
                       "' must return a value :/\n");
            }
        }
    }
    else if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        bool compile_time_condition = false;
        if (evaluate_compile_time_condition(
                ifs->condition.get(), compile_time_condition)) {
            emit_statement(
                compile_time_condition
                    ? ifs->then_branch.get()
                    : ifs->else_branch.get(),
                indent);
            return;
        }

        std::string cond = emit_expression(ifs->condition.get());
        BType condition_type = get_expr_type(ifs->condition.get());
        if (condition_type == BType::UNKNOWN) {
            condition_type = ifs->condition->btype;
        }
        IRType condition_ir = btype_to_ir(condition_type);
        if (condition_ir == IRType::UNKNOWN) condition_ir = IRType::I1;
        if (condition_ir != IRType::I1 &&
            !coerce_ir_value(*this, cond, condition_ir, IRType::I1, pad)) {
            gerror("if condition is not convertible to bol :/\n");
            return;
        }
        
        std::string then_lbl = next_label("if_then");
        std::string else_lbl = next_label("if_else");
        std::string end_lbl = next_label("if_end");
        
        body << pad << "br i1 " << cond << ", label %" << then_lbl
             << ", label %" << else_lbl << "\n";
        
        body << then_lbl << ":\n";
        const bool then_falls_through =
            statement_falls_through(ifs->then_branch.get());
        emit_statement(ifs->then_branch.get(), indent);
        if (then_falls_through) {
            body << pad << "br label %" << end_lbl << "\n";
        }
        
        body << else_lbl << ":\n";
        const bool else_falls_through =
            !ifs->else_branch ||
            statement_falls_through(ifs->else_branch.get());
        if (ifs->else_branch) {
            emit_statement(ifs->else_branch.get(), indent);
        }
        if (else_falls_through) {
            body << pad << "br label %" << end_lbl << "\n";
        }
        
        if (then_falls_through || else_falls_through) {
            body << end_lbl << ":\n";
        }
    }
    else if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        std::string matched_value = emit_expression(match->value.get());
        BType matched_type = get_expr_type(match->value.get());
        IRType matched_ir = btype_to_ir(matched_type);
        if (matched_ir == IRType::UNKNOWN) matched_ir = IRType::I64;
        const std::string end_label = next_label("match_end");

        for (const auto& entry : match->cases) {
            const std::string case_label = next_label("match_case");
            const std::string next_label_name = next_label("match_next");
            std::string case_value = emit_expression(entry.first.get());
            BType case_type = get_expr_type(entry.first.get());
            IRType case_ir = btype_to_ir(case_type);
            if (case_ir == IRType::UNKNOWN) case_ir = matched_ir;
            if (case_ir != matched_ir &&
                !coerce_ir_value(*this, case_value, case_ir, matched_ir, pad,
                                 is_unsigned_integer_type(case_type))) {
                gerror("Match case has an incompatible type :/\n");
                return;
            }

            std::string comparison = next_ssa();
            if (is_ir_float(matched_ir)) {
                body << pad << comparison << " = fcmp oeq "
                     << llvm_ir_type_name(matched_ir) << " " << matched_value
                     << ", " << case_value << "\n";
            } else if (is_ir_pointer(matched_ir)) {
                body << pad << comparison << " = icmp eq "
                     << llvm_ir_type_name(matched_ir) << " " << matched_value
                     << ", " << case_value << "\n";
            } else {
                body << pad << comparison << " = icmp eq "
                     << llvm_ir_type_name(matched_ir) << " " << matched_value
                     << ", " << case_value << "\n";
            }
            body << pad << "br i1 " << comparison << ", label %" << case_label
                 << ", label %" << next_label_name << "\n";
            body << case_label << ":\n";
            const bool case_falls_through =
                statement_falls_through(entry.second.get());
            emit_statement(entry.second.get(), indent);
            if (case_falls_through) {
                body << pad << "br label %" << end_label << "\n";
            }
            body << next_label_name << ":\n";
        }

        const bool default_falls_through =
            !match->default_case ||
            statement_falls_through(match->default_case.get());
        if (match->default_case) {
            emit_statement(match->default_case.get(), indent);
        }
        if (default_falls_through) {
            body << pad << "br label %" << end_label << "\n";
        }
        if (statement_falls_through(match)) {
            body << end_label << ":\n";
        }
    }
    else if (auto* fors = dynamic_cast<const ForStmt*>(stmt)) {
        
        std::string bound = emit_expression(fors->bound.get());
        if (!normalize_integer_to_i64(
                *this, fors->bound.get(), bound, "Loop bound", pad)) {
            return;
        }
        
        
        
        std::string counter_ptr = next_ssa();
        body << pad << counter_ptr << " = alloca i64\n";
        body << pad << "store i64 0, i64* " << counter_ptr << "\n";
        vars[fors->var_name] = {fors->var_name, counter_ptr, IRType::I64, BType::UNKNOWN, 0};
        
        std::string cond_lbl = next_label("loopcond");
        std::string body_lbl = next_label("loopbody");
        std::string step_lbl = next_label("loopstep");
        std::string end_lbl = next_label("loopend");
        
        body << pad << "br label %" << cond_lbl << "\n";
        body << cond_lbl << ":\n";
        
        std::string cur = next_ssa();
        body << pad << cur << " = load i64, i64* " << counter_ptr << "\n";
        std::string cmp = next_ssa();
        body << pad << cmp << " = icmp ult i64 " << cur << ", " << bound << "\n";
        body << pad << "br i1 " << cmp << ", label %" << body_lbl
             << ", label %" << end_lbl << "\n";
        
        body << body_lbl << ":\n";
        loop_targets.push_back({
            end_lbl,
            step_lbl,
            cleanup_scopes.size()
        });
        const bool body_falls_through =
            statement_falls_through(fors->body.get());
        emit_statement(fors->body.get(), indent);
        loop_targets.pop_back();
        if (body_falls_through) {
            body << pad << "br label %" << step_lbl << "\n";
        }

        body << step_lbl << ":\n";
        std::string cur2 = next_ssa();
        body << pad << cur2 << " = load i64, i64* " << counter_ptr << "\n";
        std::string next = next_ssa();
        body << pad << next << " = add i64 " << cur2 << ", 1\n";
        body << pad << "store i64 " << next << ", i64* " << counter_ptr << "\n";
        body << pad << "br label %" << cond_lbl << "\n";
        
        body << end_lbl << ":\n";
    }
    else if (auto* fws = dynamic_cast<const ForWhileStmt*>(stmt)) {
        
        std::string cond_lbl = next_label("loopcond");
        std::string body_lbl = next_label("loopbody");
        std::string end_lbl = next_label("loopend");
        
        body << pad << "br label %" << cond_lbl << "\n";
        body << cond_lbl << ":\n";
        
        std::string cond = emit_expression(fws->condition.get());
        BType condition_type = get_expr_type(fws->condition.get());
        if (condition_type == BType::UNKNOWN) {
            condition_type = fws->condition->btype;
        }
        IRType condition_ir = btype_to_ir(condition_type);
        if (condition_ir == IRType::UNKNOWN) condition_ir = IRType::I1;
        if (condition_ir != IRType::I1 &&
            !coerce_ir_value(*this, cond, condition_ir, IRType::I1, pad)) {
            gerror("loop condition is not convertible to bol :/\n");
            return;
        }
        body << pad << "br i1 " << cond << ", label %" << body_lbl
             << ", label %" << end_lbl << "\n";
        
        body << body_lbl << ":\n";
        loop_targets.push_back({
            end_lbl,
            cond_lbl,
            cleanup_scopes.size()
        });
        const bool body_falls_through =
            statement_falls_through(fws->body.get());
        emit_statement(fws->body.get(), indent);
        loop_targets.pop_back();
        if (body_falls_through) {
            body << pad << "br label %" << cond_lbl << "\n";
        }
        
        body << end_lbl << ":\n";
    }
    else if (dynamic_cast<const BreakStmt*>(stmt)) {
        if (loop_targets.empty()) {
            gerror("'stop' can only be used inside a loop :/\n");
            return;
        }
        emit_cleanup_from(loop_targets.back().cleanup_depth, pad);
        body << pad << "br label %"
             << loop_targets.back().break_label << "\n";
    }
    else if (dynamic_cast<const ContinueStmt*>(stmt)) {
        if (loop_targets.empty()) {
            gerror("'pass' can only be used inside a loop :/\n");
            return;
        }
        emit_cleanup_from(loop_targets.back().cleanup_depth, pad);
        body << pad << "br label %"
             << loop_targets.back().continue_label << "\n";
    }
    else if (auto* plugin = dynamic_cast<const PluginStmt*>(stmt)) {
        std::string rel_path = plugin->path;
        std::string path = rel_path;
        
        if (!base_root.empty()) {
            path = (std::filesystem::path(base_root) / rel_path).lexically_normal().string();
        }
        
        emit_string_literal(path);
        std::string path_ptr = next_ssa();
        body << pad << path_ptr << " = getelementptr [" << path.length() + 1 << " x i8], ["
             << path.length() + 1 << " x i8]* @.str." << (str_counter - 1) << ", i32 0, i32 0\n";
        std::string handle = next_ssa();
        body << pad << handle << " = call i8* @dlopen(i8* " << path_ptr << ", i32 1)\n";
    }
    else if (auto* llh = dynamic_cast<const LLHStmt*>(stmt)) {
        (void)llh;
    }
    else if (auto* ll = dynamic_cast<const LLStmt*>(stmt)) {
        
        body << "  " << ll->llvm_code << "\n";
    }
    else if (auto* exprs = dynamic_cast<const ExprStmt*>(stmt)) {
        if (auto* call = dynamic_cast<const CallExpr*>(exprs->expression.get());
            call && (call->callee == "log" || call->callee == "logl")) {
            if (call->args.empty()) {
                if (call->callee == "log") {
                    std::string newline_ptr = next_ssa();
                    body << "  " << newline_ptr
                         << " = getelementptr [2 x i8], [2 x i8]* @fmt_newline, "
                         << "i32 0, i32 0\n";
                    body << "  call i32 (i8*, ...) @printf(i8* "
                         << newline_ptr << ")\n";
                }
                return;
            }

            const bool newline = call->callee == "log";
            const std::string str_format = newline ? "@fmt_str" : "@fmt_str_raw";
            const std::string ptr_format = newline ? "@fmt_ptr" : "@fmt_ptr_raw";
            const std::string num_format = newline ? "@fmt_num" : "@fmt_num_raw";
            const std::string unum_format = newline ? "@fmt_unum" : "@fmt_unum_raw";
            const std::string hex_format = newline ? "@fmt_hex" : "@fmt_hex_raw";
            const std::string f64_format = newline ? "@fmt_f64" : "@fmt_f64_raw";
            const int str_format_size = newline ? 4 : 3;
            const int ptr_format_size = newline ? 4 : 3;
            const int num_format_size = newline ? 6 : 5;
            const int hex_format_size = newline ? 8 : 7;
            const int f64_format_size = newline ? 5 : 4;
            const auto emit_format_pointer = [&](const std::string& format,
                                                 int size) {
                std::string result = next_ssa();
                body << "  " << result << " = getelementptr [" << size
                     << " x i8], [" << size << " x i8]* " << format
                     << ", i32 0, i32 0\n";
                return result;
            };

            const Expr* arg_expr = call->args[0].get();
            BType arg_type = get_expr_type(arg_expr);
            std::string arg = emit_expression(arg_expr);

            if (arg_type == BType::STRUCT) {
                const std::string struct_name = get_expr_struct_name(arg_expr);
                if (struct_name.empty()) {
                    gerror("Cannot resolve struct type passed to log() :/\n");
                    return;
                }
                std::string opaque_ptr = next_ssa();
                body << "  " << opaque_ptr << " = bitcast "
                     << get_struct_type_str(struct_name) << "* " << arg
                     << " to i8*\n";
                std::string fmt_ptr =
                    emit_format_pointer(ptr_format, ptr_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", i8* " << opaque_ptr << ")\n";
            } else if (arg_type == BType::STR || arg_type == BType::I8_PTR ||
                arg_type == BType::STR_PTR) {
                std::string is_null = next_ssa();
                body << "  " << is_null << " = icmp eq i8* " << arg
                     << ", null\n";
                const std::string null_name = newline
                    ? "@.null_str"
                    : "@.null_str_raw";
                const int null_size = 7;
                std::string null_value = next_ssa();
                body << "  " << null_value << " = getelementptr ["
                     << null_size << " x i8], [" << null_size << " x i8]* "
                     << null_name << ", i32 0, i32 0\n";
                std::string safe_arg = next_ssa();
                body << "  " << safe_arg << " = select i1 " << is_null
                     << ", i8* " << null_value << ", i8* " << arg << "\n";
                std::string fmt_ptr =
                    emit_format_pointer(str_format, str_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", i8* " << safe_arg << ")\n";
            } else if (arg_type == BType::VOID ||
                       arg_type == BType::PTR || arg_type == BType::FUNC ||
                       arg_type == BType::I16_PTR || arg_type == BType::I32_PTR ||
                       arg_type == BType::I64_PTR || arg_type == BType::U16_PTR ||
                       arg_type == BType::U32_PTR || arg_type == BType::U64_PTR ||
                       arg_type == BType::F32_PTR || arg_type == BType::F64_PTR) {
                std::string opaque_pointer = arg;
                const IRType pointer_type = btype_to_ir(arg_type);
                if (pointer_type != IRType::I8_PTR) {
                    opaque_pointer = next_ssa();
                    body << "  " << opaque_pointer << " = bitcast "
                         << llvm_ir_type_name(pointer_type) << " " << arg
                         << " to i8*\n";
                }
                std::string fmt_ptr =
                    emit_format_pointer(ptr_format, ptr_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", i8* " << opaque_pointer << ")\n";
            } else if (arg_type == BType::F32 || arg_type == BType::F64) {
                std::string printable = arg;
                if (arg_type == BType::F32) {
                    printable = next_ssa();
                    body << "  " << printable << " = fpext float " << arg
                         << " to double\n";
                }
                std::string fmt_ptr =
                    emit_format_pointer(f64_format, f64_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", double " << printable << ")\n";
            } else if (arg_type == BType::BOOL) {
                std::string converted = next_ssa();
                body << "  " << converted << " = zext i1 " << arg << " to i64\n";
                std::string fmt_ptr =
                    emit_format_pointer(num_format, num_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr << ", i64 " << converted << ")\n";
            } else if (arg_type == BType::HEX) {
                std::string fmt_ptr =
                    emit_format_pointer(hex_format, hex_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", i64 " << arg << ")\n";
            } else if (arg_type == BType::INT ||
                       arg_type == BType::I8 || arg_type == BType::I16 ||
                       arg_type == BType::I32 || arg_type == BType::I64 ||
                       arg_type == BType::U8 || arg_type == BType::U16 ||
                       arg_type == BType::U32 || arg_type == BType::U64 ||
                       arg_type == BType::ISIZE || arg_type == BType::USIZE) {
                IRType arg_ir_type = btype_to_ir(arg_type);
                std::string printable = arg;
                if (arg_ir_type == IRType::I8 ||
                    arg_ir_type == IRType::I16 ||
                    arg_ir_type == IRType::I32) {
                    const bool is_unsigned =
                        arg_type == BType::U8 || arg_type == BType::U16 ||
                        arg_type == BType::U32 || arg_type == BType::USIZE;
                    printable = next_ssa();
                    body << "  " << printable << " = "
                         << (is_unsigned ? "zext " : "sext ")
                         << llvm_ir_type_name(arg_ir_type) << " " << arg
                         << " to i64\n";
                }
                const bool is_unsigned =
                    arg_type == BType::U8 || arg_type == BType::U16 ||
                    arg_type == BType::U32 || arg_type == BType::U64 ||
                    arg_type == BType::USIZE;
                std::string fmt_ptr = emit_format_pointer(
                    is_unsigned ? unum_format : num_format,
                    num_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr
                     << ", i64 " << printable << ")\n";
            } else {
                std::string fmt_ptr =
                    emit_format_pointer(num_format, num_format_size);
                body << "  call i32 (i8*, ...) @printf(i8* " << fmt_ptr << ", i64 " << arg << ")\n";
            }
            return;
        }

        emit_expression(exprs->expression.get());
    }
}

static BType elem_to_array_type(BType elem_type) {
    return array_type_for(elem_type);
}

std::string LLVMEmitter::get_expr_struct_name(const Expr* expr) {
    if (!expr) return "";

    if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        std::string resolved = resolve_struct_type(literal->type_ref);
        return resolved.empty() ? literal->struct_name : resolved;
    }

    if (dynamic_cast<const TupleExpr*>(expr)) {
        return resolve_tuple_type(tuple_type_ref_from_expr(*this, expr));
    }

    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) {
        auto local_it = vars.find(var->name);
        if (local_it != vars.end() && local_it->second.type == IRType::STRUCT) {
            return local_it->second.struct_name;
        }

        auto emitter_it = g_global_struct_types.find(this);
        if (emitter_it != g_global_struct_types.end()) {
            auto global_it = emitter_it->second.find(var->name);
            if (global_it != emitter_it->second.end()) return global_it->second;
        }
        return "";
    }

    if (auto* dereference = dynamic_cast<const DerefExpr*>(expr)) {
        return pointer_pointee_struct_name(
            *this, dereference->operand.get());
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        std::string object_struct_name = get_expr_struct_name(member->object.get());
        auto struct_it = structs.find(object_struct_name);
        if (struct_it == structs.end()) return "";

        const LLVMStructInfo& info = struct_it->second;
        auto field_it = info.field_indices.find(member->member);
        if (field_it == info.field_indices.end()) return "";

        size_t field_idx = field_it->second;
        if (!is_aggregate_type(info.field_types[field_idx])) return "";
        return info.field_annotations[field_idx];
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        const std::string tuple_name =
            get_expr_struct_name(index->object.get());
        auto tuple = structs.find(tuple_name);
        if (tuple != structs.end() && tuple->second.is_tuple) {
            auto* number = dynamic_cast<const NumberExpr*>(index->index.get());
            if (!number || number->is_float || number->value < 0 ||
                static_cast<size_t>(number->value) >= tuple->second.field_types.size()) {
                return "";
            }
            const size_t element = static_cast<size_t>(number->value);
            return is_aggregate_type(tuple->second.field_types[element])
                ? tuple->second.field_annotations[element]
                : "";
        }

        const std::string native_element =
            struct_array_element_name(*this, index->object.get());
        if (!native_element.empty()) return native_element;

        if (!get_expr_struct_name(index->object.get()).empty()) {
            auto call = make_operator_call(
                "[]", index->object.get(), index->index.get());
            std::string callee;
            if (call && resolve_call_target(call.get(), callee, false)) {
                auto emitter_it = g_struct_return_types.find(this);
                if (emitter_it != g_struct_return_types.end()) {
                    auto result = emitter_it->second.find(callee);
                    if (result != emitter_it->second.end()) {
                        return result->second;
                    }
                }
            }
        }
        return "";
    }

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        std::string callee_name;
        if (!resolve_call_target(call, callee_name, false)) return "";

        auto emitter_it = g_struct_return_types.find(this);
        if (emitter_it == g_struct_return_types.end()) return "";
        auto return_it = emitter_it->second.find(callee_name);
        return return_it == emitter_it->second.end() ? "" : return_it->second;
    }

    return "";
}

BType LLVMEmitter::infer_operator_return_type(FnDecl& fn) {
    if (!fn.is_operator || fn.return_type != BType::UNKNOWN) {
        return fn.return_type;
    }

    auto saved_vars = vars;
    vars.clear();

    for (const auto& param : fn.params) {
        if (is_struct_type(param.type)) {
            std::string struct_name = resolve_struct_type(
                type_ref_or_legacy(param.type_ref, param.type, param.struct_name));
            if (struct_name.empty()) struct_name = param.struct_name;
            vars[param.name] = {
                param.name, "%" + param.name + "_type_only", IRType::STRUCT,
                BType::UNKNOWN, 0, struct_name, true
            };
        } else if (is_array_type(param.type)) {
            BType elem_type = get_array_elem_type(param.type);
            vars[param.name] = {
                param.name, "%" + param.name + "_type_only", IRType::ARR,
                elem_type, 0
            };
        } else {
            vars[param.name] = {
                param.name, "%" + param.name + "_type_only",
                btype_to_ir(param.type), BType::UNKNOWN, 0
            };
        }
        vars[param.name].source_type = param.type;
    }

    std::vector<const Expr*> returns;
    collect_return_expressions(fn.body.get(), returns);

    BType inferred = BType::UNKNOWN;
    for (const Expr* value : returns) {
        BType current = get_expr_type(value);
        if (current == BType::UNKNOWN) continue;
        if (inferred == BType::UNKNOWN) {
            inferred = current;
        } else if (inferred != current) {
            
            if (inferred == BType::F64 || current == BType::F64) {
                inferred = BType::F64;
            } else if (btype_to_ir(inferred) != btype_to_ir(current)) {
                inferred = BType::UNKNOWN;
                break;
            }
        }
    }

    vars = std::move(saved_vars);

    if (inferred != BType::UNKNOWN && inferred != BType::STRUCT) {
        fn.return_type = inferred;
        fn.return_type_ref = TypeRef{};
        fn.return_type_ref.base = inferred;
        fn.return_type_annotation = type_name(inferred);
    }
    return fn.return_type;
}

bool LLVMEmitter::resolve_call_target(const CallExpr* call,
                                      std::string& callee_name,
                                      bool report_errors) {
    if (!call) return false;

    auto fail = [&](const std::string& message) {
        if (report_errors) gerror(message + " :/\n");
        return false;
    };

    callee_name = call->callee;
    std::vector<TypeRef> receiver_type_args;

    if (call->is_method_call) {
        if (call->args.empty()) return fail("Method call has no receiver");

        std::string receiver_struct = get_expr_struct_name(call->args.front().get());
        auto receiver_it = structs.find(receiver_struct);
        if (receiver_struct.empty() || receiver_it == structs.end()) {
            std::string call_hint;
            if (call->args.size() > 1) {
                if (auto* argument = dynamic_cast<const VariableExpr*>(
                        call->args[1].get())) {
                    call_hint = " (first argument '" + argument->name + "')";
                }
            }
            return fail("Cannot resolve method receiver type for '" + call->callee +
                        "' in function '" + current_function_name + "'" + call_hint);
        }

        const LLVMStructInfo& receiver_info = receiver_it->second;
        const std::string& owner = receiver_info.template_name.empty()
            ? receiver_struct
            : receiver_info.template_name;
        callee_name = mangle_method_name(owner, call->callee);

        for (const auto& type_arg_ref : receiver_info.template_args) {
            if (type_ref_to_btype(type_arg_ref) == BType::UNKNOWN) {
                return fail("Cannot specialize method '" + owner + "." +
                            call->callee + "' for receiver type");
            }
            receiver_type_args.push_back(type_arg_ref);
        }
    }

    const std::string template_name = callee_name;
    if (template_registry.is_template(template_name)) {
        FnDecl* inst = nullptr;

        if (call->is_method_call) {
            callee_name = mangle_template_name(
                template_name, receiver_type_args);
            if (func_types.count(callee_name) == 0) {
                inst = template_registry.instantiate(
                    template_name, receiver_type_args);
            }
        } else {
            std::vector<BType> type_args;
            if (!call->template_args.empty()) {
                type_args = call->template_args;
            } else {
                std::vector<BType> arg_types;
                for (const auto& arg : call->args) {
                    BType arg_type = get_expr_type(arg.get());
                    if (arg_type == BType::UNKNOWN) arg_type = arg->btype;
                    arg_types.push_back(arg_type);
                }
                if (!template_registry.infer_type_args(
                        template_name, arg_types, type_args)) {
                    return fail("Cannot infer template types for '" +
                                template_name + "'");
                }
            }

            callee_name = mangle_template_name(template_name, type_args);
            if (func_types.count(callee_name) == 0) {
                inst = template_registry.instantiate(
                    template_name, type_args);
            }
        }

        if (func_types.count(callee_name) == 0) {
            if (!inst) return false;

            if (inst->is_operator && inst->return_type == BType::UNKNOWN) {
                infer_operator_return_type(*inst);
            }

            func_types[inst->name] =
                function_uses_native_void_abi(*inst)
                    ? IRType::VOID
                    : btype_to_ir(inst->return_type);
            func_return_btypes[inst->name] = inst->return_type;
            remember_struct_return_type(*this, *inst);
            remember_parameter_passing_modes(*this, *inst);
            std::vector<IRType> arg_ir_types;
            for (const auto& param : inst->params) {
                arg_ir_types.push_back(btype_to_ir(param.type));
            }
            func_arg_types[inst->name] = std::move(arg_ir_types);
        }
    }

    if (call->is_method_call && func_types.count(callee_name) == 0) {
        std::string receiver_struct = get_expr_struct_name(call->args.front().get());
        return fail("No method '" + call->callee + "' for struct '" +
                    receiver_struct + "'");
    }

    return true;
}

BType LLVMEmitter::get_expr_type(const Expr* expr) {
    if (!expr) return BType::UNKNOWN;

    if (dynamic_cast<const StructLiteralExpr*>(expr)) {
        return BType::STRUCT;
    }

    if (dynamic_cast<const TupleExpr*>(expr)) {
        return BType::TUPLE;
    }

    if (is_null_expression(expr)) {
        return BType::PTR;
    }

    if (dynamic_cast<const AnonymousFnExpr*>(expr)) {
        return BType::FUNC;
    }

    if (auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        if (!get_expr_struct_name(bin->left.get()).empty()) {
            auto call = make_operator_call(bin->op, bin->left.get(), bin->right.get());
            std::string callee;
            if (call && resolve_call_target(call.get(), callee, false) &&
                func_types.count(callee)) {
                if (func_return_btypes.count(callee)) {
                    return func_return_btypes[callee];
                }
                return ir_to_btype(func_types[callee]);
            }
        }
    } else if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        if (!get_expr_struct_name(unary->operand.get()).empty()) {
            auto call = make_operator_call(unary->op, unary->operand.get());
            std::string callee;
            if (call && resolve_call_target(call.get(), callee, false) &&
                func_types.count(callee)) {
                if (func_return_btypes.count(callee)) {
                    return func_return_btypes[callee];
                }
                return ir_to_btype(func_types[callee]);
            }
        }
    } else if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        const std::string tuple_name = get_expr_struct_name(idx->object.get());
        auto tuple = structs.find(tuple_name);
        if (tuple != structs.end() && tuple->second.is_tuple) {
            auto* number = dynamic_cast<const NumberExpr*>(idx->index.get());
            if (!number || number->is_float || number->value < 0 ||
                static_cast<size_t>(number->value) >= tuple->second.field_types.size()) {
                return BType::UNKNOWN;
            }
            return tuple->second.field_types[static_cast<size_t>(number->value)];
        }
        if (!struct_array_element_name(*this, idx->object.get()).empty()) {
            return BType::STRUCT;
        }
        if (!get_expr_struct_name(idx->object.get()).empty()) {
            auto call = make_operator_call("[]", idx->object.get(), idx->index.get());
            std::string callee;
            if (call && resolve_call_target(call.get(), callee, false) &&
                func_types.count(callee)) {
                if (func_return_btypes.count(callee)) {
                    return func_return_btypes[callee];
                }
                return ir_to_btype(func_types[callee]);
            }
        }
    }
    
    if (expr->btype != BType::UNKNOWN) {
        return expr->btype;
    }
    
    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) {
        if (vars.count(var->name)) {
            LLVMVar& v = vars[var->name];
            
            if (v.type == IRType::ARR) {
                return elem_to_array_type(v.elem_type);
            }
            if (v.source_type != BType::UNKNOWN) {
                return v.source_type;
            }
            return ir_to_btype(v.type);
        }
        if (global_btypes.count(var->name)) {
            return global_btypes[var->name];
        }
        if (func_types.count(var->name)) {
            return BType::FUNC;
        }
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        std::string struct_name = get_expr_struct_name(member->object.get());
        if (struct_name.empty()) {
            struct_name = pointer_pointee_struct_name(
                *this, member->object.get());
        }
        auto struct_it = structs.find(struct_name);
        if (struct_it == structs.end()) return BType::UNKNOWN;

        const LLVMStructInfo& info = struct_it->second;
        auto field_it = info.field_indices.find(member->member);
        if (field_it == info.field_indices.end()) return BType::UNKNOWN;
        return info.field_types[field_it->second];
    }
    
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (!call->is_method_call && call->callee == "volatile_load") {
            return call->template_args.size() == 1
                ? call->template_args.front()
                : BType::UNKNOWN;
        }
        if (!call->is_method_call && call->callee == "volatile_store") {
            return BType::VOID;
        }
        if (!call->is_method_call &&
            (call->callee == "atomic_load" ||
             call->callee == "atomic_add" ||
             call->callee == "atomic_exchange")) {
            return call->template_args.size() == 1
                ? call->template_args.front()
                : BType::UNKNOWN;
        }
        if (!call->is_method_call && call->callee == "atomic_store") {
            return BType::VOID;
        }
        if (!call->is_method_call &&
            call->callee == "atomic_compare_exchange") {
            return BType::BOOL;
        }

        
        if (!call->is_method_call &&
            (call->callee == "typeof" || call->callee == "platform")) {
            return BType::STR;
        }

        auto function_pointer = vars.find(call->callee);
        if (!call->is_method_call &&
            function_pointer != vars.end() &&
            function_pointer->second.is_function_pointer) {
            return function_pointer->second.function_signature_known
                ? ir_to_btype(function_pointer->second.function_return_type)
                : BType::UNKNOWN;
        }

        std::string callee_name;
        if (!resolve_call_target(call, callee_name, false)) {
            return BType::UNKNOWN;
        }
        if (func_return_btypes.count(callee_name)) {
            return func_return_btypes[callee_name];
        }
        if (func_types.count(callee_name)) {
            return ir_to_btype(func_types[callee_name]);
        }
    }

    if (auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        if (bin->op == ">" || bin->op == "<" ||
            bin->op == ">=" || bin->op == "<=" ||
            bin->op == "is" || bin->op == "not" ||
            bin->op == "==" || bin->op == "!=" ||
            bin->op == "and" || bin->op == "or") {
            return BType::BOOL;
        }
        BType left_type = get_expr_type(bin->left.get());
        BType right_type = get_expr_type(bin->right.get());
        if (left_type == BType::F64 || right_type == BType::F64) return BType::F64;
        if (left_type == BType::F32 || right_type == BType::F32) return BType::F32;
        if (left_type != BType::UNKNOWN) return left_type;
        return right_type;
    }

    if (auto* ternary = dynamic_cast<const TernaryExpr*>(expr)) {
        BType then_type = get_expr_type(ternary->then_expr.get());
        BType else_type = get_expr_type(ternary->else_expr.get());
        if (then_type == else_type) return then_type;
        if (then_type == BType::F64 || else_type == BType::F64) return BType::F64;
        if (then_type == BType::F32 || else_type == BType::F32) return BType::F32;
        if (then_type != BType::UNKNOWN) return then_type;
        return else_type;
    }

    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        if (unary->op == "!") return BType::BOOL;
        return get_expr_type(unary->operand.get());
    }
    
    if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        BType object_type = get_expr_type(idx->object.get());
        const std::string tuple_name = get_expr_struct_name(idx->object.get());
        auto tuple = structs.find(tuple_name);
        if (tuple != structs.end() && tuple->second.is_tuple) {
            auto* number = dynamic_cast<const NumberExpr*>(idx->index.get());
            if (!number || number->is_float || number->value < 0 ||
                static_cast<size_t>(number->value) >= tuple->second.field_types.size()) {
                return BType::UNKNOWN;
            }
            return tuple->second.field_types[static_cast<size_t>(number->value)];
        }
        if (object_type == BType::STR || object_type == BType::I8_PTR) {
            return BType::I8;
        }
        if (!struct_array_element_name(*this, idx->object.get()).empty()) {
            return BType::STRUCT;
        }
        if (is_array_type(object_type)) {
            return get_array_elem_type(object_type);
        }

        if (auto* var = dynamic_cast<const VariableExpr*>(idx->object.get())) {
            if (vars.count(var->name)) {
                LLVMVar& v = vars[var->name];
                if (v.type == IRType::ARR) {
                    return v.elem_type;
                }
            }
        }
    }

    if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        const BType pointee_type = pointer_pointee_type_from_expression(
            *this, deref->operand.get());
        return pointee_type == BType::FUNC
            ? BType::UNKNOWN
            : pointee_type;
    }
    
    if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        if (auto* var = dynamic_cast<const VariableExpr*>(ref->operand.get())) {
            if (vars.count(var->name)) {
                LLVMVar& v = vars[var->name];
                
                
                if (v.type == IRType::I64) return BType::I64_PTR;
                if (v.type == IRType::I32) return BType::I32_PTR;
                if (v.type == IRType::I16) return BType::I16_PTR;
                if (v.type == IRType::I8) return BType::I8_PTR;
                if (v.type == IRType::F64) return BType::F64_PTR;
                if (v.type == IRType::F32) return BType::F32_PTR;
                if (v.type == IRType::I8_PTR) return BType::I8_PTR;
                if (v.type == IRType::STRUCT) return BType::PTR;
                return ir_to_btype(v.type);
            }
            if (!global_vars.count(var->name) && func_types.count(var->name)) {
                return BType::FUNC;
            }
        }

        const BType referenced_type = get_expr_type(ref->operand.get());
        if (referenced_type != BType::UNKNOWN) {
            return pointer_type_for_value_type(referenced_type);
        }
    }
    
    return BType::UNKNOWN;
}

std::string LLVMEmitter::emit_expression(const Expr* expr) {
    if (!expr) return "0";

    if (auto* anonymous = dynamic_cast<const AnonymousFnExpr*>(expr)) {
        if (!anonymous->type_params.empty()) {
            gerror("Generic anonymous functions are not supported yet :/\n");
            return "null";
        }

        FnDecl generated;
        generated.name = "__ferra_anon_" +
                         std::to_string(anonymous_counter++);
        generated.params = anonymous->params;
        generated.return_type = anonymous->return_type;
        generated.return_type_ref.base = anonymous->return_type;
        generated.return_type_annotation = anonymous->return_type_annotation;

        if (auto* expression_body =
                dynamic_cast<const ExprStmt*>(anonymous->body.get())) {
            auto result = std::make_unique<ReturnStmt>();
            result->value = clone_expression(*expression_body->expression);
            generated.body = std::move(result);
        } else if (anonymous->body) {
            generated.body = clone_statement(*anonymous->body);
        }

        const std::string outer_body = body.str();
        const auto outer_vars = vars;
        const auto outer_loop_targets = loop_targets;
        const BType outer_return_type = current_fn_return_type;
        const std::string outer_function_name = current_function_name;
        const std::string outer_struct_return =
            g_current_struct_return_type[this];

        body.str("");
        body.clear();
        body.seekp(0);
        emit_function(generated);
        anonymous_functions << body.str();

        body.str(outer_body);
        body.clear();
        body.seekp(0, std::ios::end);
        vars = outer_vars;
        loop_targets = outer_loop_targets;
        current_fn_return_type = outer_return_type;
        current_function_name = outer_function_name;
        g_current_struct_return_type[this] = outer_struct_return;

        const IRType return_ir = btype_to_ir(anonymous->return_type);
        std::vector<IRType> argument_types;
        for (const ParamDecl& parameter : anonymous->params) {
            argument_types.push_back(btype_to_ir(parameter.type));
        }
        const std::string function_type =
            llvm_function_pointer_type(return_ir, argument_types);
        if (function_type.empty()) {
            gerror("Anonymous function has an unsupported signature :/\n");
            return "null";
        }

        std::string address = next_ssa();
        body << "  " << address << " = bitcast " << function_type
             << " @" << generated.name << " to i64*\n";
        return address;
    }
    
    if (auto* num = dynamic_cast<const NumberExpr*>(expr)) {
        if (!num->literal.empty()) return num->literal;
        return num->is_float
            ? std::to_string(num->value)
            : std::to_string((long long)num->value);
    }
    
    if (auto* str = dynamic_cast<const StringExpr*>(expr)) {
        emit_string_literal(str->value);
        
        std::string name = "@.str." + std::to_string(str_counter - 1);
        
        std::string ptr = next_ssa();
        body << "  " << ptr << " = getelementptr [" << str->value.length() + 1 << " x i8], ["
             << str->value.length() + 1 << " x i8]* " << name << ", i32 0, i32 0\n";
        return ptr;
    }
    
    if (auto* boolean = dynamic_cast<const BoolExpr*>(expr)) {
        return boolean->value ? "true" : "false";
    }
    
    if (auto* null = dynamic_cast<const NullExpr*>(expr)) {
        return "null";
    }
    
    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) {
        if (vars.count(var->name)) {
            LLVMVar& v = vars[var->name];
            v.used = true;
            
            if (v.type == IRType::ARR) {
                BType elem_type = v.elem_type;
                if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
                if (is_aggregate_type(elem_type) && !v.struct_name.empty()) {
                    const std::string struct_type =
                        get_struct_type_str(v.struct_name);
                    const std::string element_type = v.inline_struct_array
                        ? struct_type
                        : struct_type + "*";
                    std::string ssa = next_ssa();
                    body << "  " << ssa << " = load " << element_type
                         << "*, " << element_type << "** " << v.alloca
                         << "\n";
                    return ssa;
                }
                IRType elem_ir_type = btype_to_ir(elem_type);
                std::string ssa = next_ssa();
                body << "  " << ssa << " = load " << llvm_type_str(elem_ir_type) << "*, " 
                     << llvm_type_str(elem_ir_type) << "** " << v.alloca << "\n";
                return ssa;
            }
            
            if (v.type == IRType::STRUCT && !v.struct_name.empty()) {
                return emit_lvalue(expr);
            }
            std::string ssa = next_ssa();
            body << "  " << ssa << " = load " << llvm_type_str(v.type) 
                 << ", " << llvm_ptr_type_str(v.type) << " " << v.alloca << "\n";
            
            return ssa;
        }
        
        if (global_vars.count(var->name)) {
            IRType vtype = global_vars[var->name];
            if (vtype == IRType::STRUCT) return "@" + var->name;

            std::string ssa = next_ssa();
            const std::string value_type = llvm_ir_type_name(vtype);
            body << "  " << ssa << " = load " << value_type
                 << ", " << value_type << "* @" << var->name << "\n";
            return ssa;
        }
        
        if (func_types.count(var->name) && func_arg_types.count(var->name)) {
            const std::string function_type = llvm_function_pointer_type(
                func_types[var->name], func_arg_types[var->name]);
            if (function_type.empty()) {
                gerror("Cannot take the address of function '" + var->name +
                       "' because its signature is not representable yet :/\n");
                return "null";
            }

            std::string address = next_ssa();
            body << "  " << address << " = bitcast " << function_type
                 << " @" << var->name << " to i64*\n";
            return address;
        }
        
        return "0";
    }

    if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        const TypeRef tuple_ref = tuple_type_ref_from_expr(*this, tuple);
        if (tuple_ref.base != BType::TUPLE || tuple_ref.type_args.empty() ||
            tuple_ref.type_args.size() != tuple->elements.size()) {
            gerror("Cannot infer a concrete fixed tuple type :/\n");
            return "0";
        }

        const std::string tuple_name = resolve_tuple_type(tuple_ref);
        auto tuple_info = structs.find(tuple_name);
        if (tuple_name.empty() || tuple_info == structs.end()) {
            gerror("Cannot resolve tuple storage type :/\n");
            return "0";
        }

        const std::string tuple_type = get_struct_type_str(tuple_name);
        std::string result = next_ssa();
        body << "  " << result << " = alloca " << tuple_type << "\n";
        body << "  store " << tuple_type << " zeroinitializer, "
             << tuple_type << "* " << result << "\n";

        for (size_t i = 0; i < tuple->elements.size(); ++i) {
            const TypeRef& expected_ref = tuple_ref.type_args[i];
            const BType expected_type = tuple_info->second.field_types[i];
            std::string field_ptr = next_ssa();
            body << "  " << field_ptr << " = getelementptr inbounds "
                 << tuple_type << ", " << tuple_type << "* " << result
                 << ", i32 0, i32 " << i << "\n";

            if (is_aggregate_type(expected_type) && !expected_ref.is_pointer &&
                !expected_ref.is_array) {
                const std::string& expected_name =
                    tuple_info->second.field_annotations[i];
                
                
                
                
                
                if (is_null_expression(tuple->elements[i].get())) {
                    const std::string aggregate_type =
                        get_struct_type_str(expected_name);
                    body << "  store " << aggregate_type
                         << " zeroinitializer, " << aggregate_type << "* "
                         << field_ptr << "\n";
                    continue;
                }
                const std::string actual_name =
                    get_expr_struct_name(tuple->elements[i].get());
                if (expected_name.empty() || actual_name != expected_name) {
                    gerror("Tuple element " + std::to_string(i) +
                           " has an incompatible aggregate type :/\n");
                    continue;
                }
                const std::string aggregate_type =
                    get_struct_type_str(expected_name);
                std::string source = emit_expression(tuple->elements[i].get());
                std::string value = next_ssa();
                body << "  " << value << " = load " << aggregate_type
                     << ", " << aggregate_type << "* " << source << "\n";
                body << "  store " << aggregate_type << " " << value
                     << ", " << aggregate_type << "* " << field_ptr << "\n";
                continue;
            }

            BType actual_type = get_expr_type(tuple->elements[i].get());
            std::string value;
            if (is_null_expression(tuple->elements[i].get())) {
                if (is_pointer_like_btype(expected_type)) {
                    value = "null";
                } else if (expected_type == BType::F32 ||
                           expected_type == BType::F64) {
                    value = "0.0";
                } else {
                    value = "0";
                }
                actual_type = expected_type;
            } else {
                value = emit_expression(tuple->elements[i].get());
                if (actual_type == BType::UNKNOWN) {
                    actual_type = tuple->elements[i]->btype;
                }
            }

            if (!coerce_ir_value(*this, value, btype_to_ir(actual_type),
                                 btype_to_ir(expected_type))) {
                gerror("Tuple element " + std::to_string(i) + " expects '" +
                       type_ref_to_string(expected_ref) + "' :/\n");
                continue;
            }
            const std::string field_type = get_llvm_type(expected_type);
            body << "  store " << field_type << " " << value << ", "
                 << field_type << "* " << field_ptr << "\n";
        }
        return result;
    }

    if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        std::string struct_name = resolve_struct_type(literal->type_ref);
        if (struct_name.empty()) struct_name = literal->struct_name;

        auto struct_it = structs.find(struct_name);
        if (struct_name.empty() || struct_it == structs.end()) {
            gerror("Unknown struct literal type '" + literal->struct_name + "' :/\n");
            return "0";
        }

        const LLVMStructInfo& info = struct_it->second;
        const std::string struct_type = get_struct_type_str(struct_name);
        std::string result = next_ssa();
        body << "  " << result << " = alloca " << struct_type << "\n";
        body << "  store " << struct_type << " zeroinitializer, "
             << struct_type << "* " << result << "\n";

        std::unordered_set<std::string> initialized_fields;
        for (const auto& initializer : literal->fields) {
            if (!initialized_fields.insert(initializer.name).second) {
                gerror("Duplicate field '" + initializer.name +
                       "' in struct literal '" + struct_name + "' :/\n");
                continue;
            }

            auto field_it = info.field_indices.find(initializer.name);
            if (field_it == info.field_indices.end()) {
                gerror("Unknown field '" + initializer.name +
                       "' in struct literal '" + struct_name + "' :/\n");
                continue;
            }

            const size_t field_index = field_it->second;
            const BType field_type = info.field_types[field_index];
            std::string field_ptr = next_ssa();
            body << "  " << field_ptr << " = getelementptr inbounds "
                 << struct_type << ", " << struct_type << "* " << result
                 << ", i32 0, i32 " << field_index << "\n";

            if (is_struct_type(field_type)) {
                const std::string& expected_struct =
                    info.field_annotations[field_index];
                const std::string actual_struct =
                    get_expr_struct_name(initializer.value.get());
                if (actual_struct.empty() || actual_struct != expected_struct) {
                    gerror("Field '" + initializer.name + "' of '" + struct_name +
                           "' expects struct '" + expected_struct + "' :/\n");
                    continue;
                }

                std::string source_ptr = emit_expression(initializer.value.get());
                std::string loaded = next_ssa();
                body << "  " << loaded << " = load %" << expected_struct
                     << ", %" << expected_struct << "* " << source_ptr << "\n";
                body << "  store %" << expected_struct << " " << loaded
                     << ", %" << expected_struct << "* " << field_ptr << "\n";
                continue;
            }

            std::string field_value;
            if (auto* array = dynamic_cast<const ArrayExpr*>(initializer.value.get());
                array && is_array_type(field_type)) {
                BType element_type = get_array_elem_type(field_type);
                if (element_type == BType::UNKNOWN) element_type = BType::INT;
                field_value = emit_array_literal(array, element_type);
            } else {
                field_value = emit_expression(initializer.value.get());
                BType value_type = get_expr_type(initializer.value.get());
                if (value_type == BType::UNKNOWN) {
                    value_type = initializer.value->btype;
                }

                if (is_array_type(field_type) && is_array_type(value_type)) {
                    BType expected_element = get_array_elem_type(field_type);
                    BType actual_element = get_array_elem_type(value_type);
                    if (expected_element != BType::UNKNOWN &&
                        actual_element != BType::UNKNOWN &&
                        expected_element != actual_element) {
                        gerror("Field '" + initializer.name + "' of '" + struct_name +
                               "' has an incompatible array element type :/\n");
                        continue;
                    }
                } else {
                    IRType source_ir = btype_to_ir(value_type);
                    IRType target_ir = btype_to_ir(field_type);
                    if (source_ir != IRType::UNKNOWN && source_ir != target_ir &&
                        !coerce_ir_value(*this, field_value, source_ir, target_ir, "  ")) {
                        gerror("Field '" + initializer.name + "' of '" + struct_name +
                               "' has an incompatible value :/\n");
                        continue;
                    }
                }
            }

            body << "  store " << get_llvm_type(field_type) << " " << field_value
                 << ", " << get_llvm_type(field_type) << "* " << field_ptr << "\n";
        }

        return result;
    }

    if (auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        bool compile_time_value = false;
        if (evaluate_compile_time_condition(bin, compile_time_value)) {
            return compile_time_value ? "true" : "false";
        }

        if (!get_expr_struct_name(bin->left.get()).empty()) {
            auto call = make_operator_call(bin->op, bin->left.get(), bin->right.get());
            std::string callee;
            if (!call || !resolve_call_target(call.get(), callee, false)) {
                gerror("No overload for operator '" + bin->op + "' on struct '" +
                       get_expr_struct_name(bin->left.get()) + "' :/\n");
                return "0";
            }
            return emit_expression(call.get());
        }

        BType left_type = get_expr_type(bin->left.get());
        BType right_type = get_expr_type(bin->right.get());

        if (bin->op == "and" || bin->op == "or") {
            std::string left = emit_expression(bin->left.get());
            IRType left_ir = btype_to_ir(left_type);
            if (left_ir == IRType::UNKNOWN) left_ir = IRType::I1;
            if (left_ir != IRType::I1 &&
                !coerce_ir_value(*this, left, left_ir, IRType::I1, "  ")) {
                gerror("Logical operator requires boolean operands :/\n");
                return "false";
            }

            const std::string rhs_label = next_label("logic_rhs");
            const std::string short_label = next_label("logic_short");
            const std::string end_label = next_label("logic_end");
            if (bin->op == "and") {
                body << "  br i1 " << left << ", label %" << rhs_label
                     << ", label %" << short_label << "\n";
            } else {
                body << "  br i1 " << left << ", label %" << short_label
                     << ", label %" << rhs_label << "\n";
            }

            body << rhs_label << ":\n";
            std::string right = emit_expression(bin->right.get());
            IRType right_ir = btype_to_ir(right_type);
            if (right_ir == IRType::UNKNOWN) right_ir = IRType::I1;
            if (right_ir != IRType::I1 &&
                !coerce_ir_value(*this, right, right_ir, IRType::I1, "  ")) {
                gerror("Logical operator requires boolean operands :/\n");
                return "false";
            }
            body << "  br label %" << end_label << "\n";
            body << short_label << ":\n";
            body << "  br label %" << end_label << "\n";
            body << end_label << ":\n";

            std::string result = next_ssa();
            body << "  " << result << " = phi i1 [ " << right << ", %"
                 << rhs_label << " ], [ "
                 << (bin->op == "and" ? "false" : "true") << ", %"
                 << short_label << " ]\n";
            return result;
        }

        std::string left = emit_expression(bin->left.get());
        std::string right = emit_expression(bin->right.get());

        auto is_pointer_like = [](BType type) {
            return is_pointer_like_btype(type);
        };

        auto get_pointer_element_type = [](BType type) -> BType {
            if (is_array_type(type)) {
                BType element_type = get_array_elem_type(type);

                if (element_type != BType::UNKNOWN) {
                    return element_type;
                }

                return BType::I8;
            }

            switch (type) {
                case BType::STR:
                case BType::PTR:
                case BType::I8_PTR:
                    return BType::I8;

                case BType::I16_PTR:
                    return BType::I16;

                case BType::I32_PTR:
                    return BType::I32;

                case BType::I64_PTR:
                    return BType::I64;

                case BType::U8_PTR:
                    return BType::U8;

                case BType::U16_PTR:
                    return BType::U16;

                case BType::U32_PTR:
                    return BType::U32;

                case BType::U64_PTR:
                    return BType::U64;

                case BType::F32_PTR:
                    return BType::F32;

                case BType::F64_PTR:
                    return BType::F64;

                case BType::STR_PTR:
                    return BType::STR;

                default:
                    return BType::I8;
            }
        };

        if (bin->op == "+" &&
            is_pointer_like(left_type) &&
            !is_pointer_like(right_type)) {

            BType element_type =
                get_pointer_element_type(left_type);

            std::string element_llvm_type =
                get_llvm_type(element_type);

            std::string index = right;
            IRType right_ir_type =
                btype_to_ir(right_type);

            if (right_ir_type == IRType::I8) {
                std::string converted = next_ssa();

                body << "  " << converted
                    << " = sext i8 " << right
                    << " to i64\n";

                index = converted;
            } else if (right_ir_type == IRType::I16) {
                std::string converted = next_ssa();

                body << "  " << converted
                    << " = sext i16 " << right
                    << " to i64\n";

                index = converted;
            } else if (right_ir_type == IRType::I32) {
                std::string converted = next_ssa();

                body << "  " << converted
                    << " = sext i32 " << right
                    << " to i64\n";

                index = converted;
            }

            std::string result = next_ssa();

            body << "  " << result
                << " = getelementptr inbounds "
                << element_llvm_type
                << ", ptr " << left
                << ", i64 " << index
                << "\n";

            return result;
        }

        std::string op;
        if (bin->op == "+") op = "add";
        else if (bin->op == "-") op = "sub";
        else if (bin->op == "*") op = "mul";
        else if (bin->op == "/") op = "sdiv";
        else if (bin->op == "%") op = "srem";
        else if (bin->op == "&") op = "and";
        else if (bin->op == "|") op = "or";
        else if (bin->op == "#") op = "xor";
        else if (bin->op == "or") op = "or";
        else if (bin->op == "and") op = "and";
        
        
        
        if (bin->op == "&" || bin->op == "|" || bin->op == "#") {
            BType value_type = get_expr_type(bin);
            if (value_type == BType::UNKNOWN) value_type = BType::INT;
            IRType value_ir = btype_to_ir(value_type);
            IRType left_ir = btype_to_ir(left_type);
            IRType right_ir = btype_to_ir(right_type);
            if (left_ir == IRType::UNKNOWN) left_ir = value_ir;
            if (right_ir == IRType::UNKNOWN) right_ir = value_ir;
            if (!coerce_ir_value(*this, left, left_ir, value_ir, "  ",
                                 is_unsigned_integer_type(left_type)) ||
                !coerce_ir_value(*this, right, right_ir, value_ir, "  ",
                                 is_unsigned_integer_type(right_type))) {
                gerror("Bitwise operator requires integer operands :/\n");
                return "0";
            }
            std::string ssa = next_ssa();
            body << "  " << ssa << " = " << op << " " << get_llvm_type(value_type)
                 << " " << left << ", " << right << "\n";
            return ssa;
        }
        else if (bin->op == "<<" || bin->op == ">>") {
            op = bin->op == "<<" ? "shl" : "lshr";
            BType value_type = left_type == BType::UNKNOWN ? BType::INT : left_type;
            IRType value_ir = btype_to_ir(value_type);
            IRType left_ir = btype_to_ir(left_type);
            IRType right_ir = btype_to_ir(right_type);
            if (left_ir == IRType::UNKNOWN) left_ir = value_ir;
            if (right_ir == IRType::UNKNOWN) right_ir = value_ir;
            if (!coerce_ir_value(*this, left, left_ir, value_ir, "  ",
                                 is_unsigned_integer_type(left_type)) ||
                !coerce_ir_value(*this, right, right_ir, value_ir, "  ",
                                 is_unsigned_integer_type(right_type))) {
                gerror("Shift operator requires integer operands :/\n");
                return "0";
            }
            std::string ssa = next_ssa();
            body << "  " << ssa << " = " << op << " "
                 << llvm_ir_type_name(value_ir) << " " << left << ", " << right << "\n";
            return ssa;
        }
        else if (bin->op == ">" || bin->op == "<" ||
                 bin->op == ">=" || bin->op == "<=" ||
                 bin->op == "is" || bin->op == "not" ||
                 bin->op == "==" || bin->op == "!=") {
        
        std::string cmp_op;
        if (bin->op == ">") cmp_op = "sgt";
        else if (bin->op == "<") cmp_op = "slt";
        else if (bin->op == ">=") cmp_op = "sge";
        else if (bin->op == "<=") cmp_op = "sle";
        else if (bin->op == "is" || bin->op == "==") cmp_op = "eq";
        else cmp_op = "ne";

        const bool left_is_null = is_null_expression(bin->left.get());
        const bool right_is_null = is_null_expression(bin->right.get());

        if (
            (bin->op == "is" || bin->op == "not" ||
             bin->op == "==" || bin->op == "!=") &&
            left_type == BType::STR &&
            right_type == BType::STR &&
            !left_is_null && !right_is_null
        ) {
            std::string comparison = next_ssa();
            body << "  " << comparison
                 << " = call i32 @strcmp(i8* " << left
                 << ", i8* " << right << ")\n";

            std::string result = next_ssa();
            body << "  " << result << " = icmp "
                 << ((bin->op == "is" || bin->op == "==") ? "eq" : "ne")
                 << " i32 " << comparison << ", 0\n";
            return result;
        }

        const auto single_byte_literal = [](const Expr* expression,
                                            unsigned char& value) {
            const auto* literal =
                dynamic_cast<const StringExpr*>(expression);
            if (!literal || literal->value.size() != 1) return false;
            value = static_cast<unsigned char>(literal->value.front());
            return true;
        };
        const auto is_byte_type = [](BType type) {
            return type == BType::I8 || type == BType::U8;
        };

        unsigned char literal_byte = 0;
        const bool left_byte_right_literal =
            is_byte_type(left_type) &&
            single_byte_literal(bin->right.get(), literal_byte);
        unsigned char left_literal_byte = 0;
        const bool left_literal_right_byte =
            is_byte_type(right_type) &&
            single_byte_literal(bin->left.get(), left_literal_byte);
        if (
            (bin->op == "is" || bin->op == "not" ||
             bin->op == "==" || bin->op == "!=") &&
            (left_byte_right_literal || left_literal_right_byte)
        ) {
            const std::string byte_value = std::to_string(
                left_byte_right_literal ? literal_byte : left_literal_byte);
            std::string result = next_ssa();
            body << "  " << result << " = icmp " << cmp_op << " i8 "
                 << (left_byte_right_literal ? left : byte_value) << ", "
                 << (left_byte_right_literal ? byte_value : right) << "\n";
            return result;
        }

        if (
            (bin->op == "is" || bin->op == "not" ||
             bin->op == "==" || bin->op == "!=") &&
            ((is_pointer_like(left_type) && is_pointer_like(right_type)) ||
             (left_is_null && is_pointer_like(right_type)) ||
             (right_is_null && is_pointer_like(left_type)))
        ) {
            std::string ssa = next_ssa();

            
            
            body << "  " << ssa
                << " = icmp " << cmp_op
                << " ptr " << left
                << ", " << right
                << "\n";

            return ssa;
        }
        if (
            (bin->op == "is" || bin->op == "not" ||
             bin->op == "==" || bin->op == "!=") &&
            left_type == BType::BOOL &&
            right_type == BType::BOOL
        ) {
            std::string ssa = next_ssa();

            body << "  " << ssa
                << " = icmp "
                << ((bin->op == "is" || bin->op == "==") ? "eq" : "ne")
                << " i1 "
                << left
                << ", "
                << right
                << "\n";

            return ssa;
        }
        
        
        std::string left_val = left;
        std::string right_val = right;
        if (is_float_type(left_type) || is_float_type(right_type)) {
            IRType comparison_type =
                left_type == BType::F64 || right_type == BType::F64
                    ? IRType::F64
                    : IRType::F32;
            IRType left_ir_type = btype_to_ir(left_type);
            IRType right_ir_type = btype_to_ir(right_type);
            if (left_ir_type == IRType::UNKNOWN) left_ir_type = comparison_type;
            if (right_ir_type == IRType::UNKNOWN) right_ir_type = comparison_type;
            if (!coerce_ir_value(*this, left_val, left_ir_type,
                                 comparison_type, "  ",
                                 is_unsigned_integer_type(left_type)) ||
                !coerce_ir_value(*this, right_val, right_ir_type,
                                 comparison_type, "  ",
                                 is_unsigned_integer_type(right_type))) {
                gerror("Cannot normalize operands for floating comparison :/\n");
                return "false";
            }

            std::string fcmp_op;
            if (cmp_op == "sgt") fcmp_op = "ogt";
            else if (cmp_op == "slt") fcmp_op = "olt";
            else if (cmp_op == "sge") fcmp_op = "oge";
            else if (cmp_op == "sle") fcmp_op = "ole";
            else if (cmp_op == "eq") fcmp_op = "oeq";
            else fcmp_op = "one";
            std::string ssa = next_ssa();
            body << "  " << ssa << " = fcmp " << fcmp_op << " "
                 << llvm_ir_type_name(comparison_type) << " "
                 << left_val << ", " << right_val << "\n";
            return ssa;
        }

        IRType left_ir_type = btype_to_ir(left_type);
        IRType right_ir_type = btype_to_ir(right_type);
        if (left_ir_type == IRType::UNKNOWN) left_ir_type = IRType::I64;
        if (right_ir_type == IRType::UNKNOWN) right_ir_type = IRType::I64;
        IRType comparison_type =
            ir_integer_bits(left_ir_type) >= ir_integer_bits(right_ir_type)
                ? left_ir_type
                : right_ir_type;
        if (!coerce_ir_value(*this, left_val, left_ir_type,
                             comparison_type, "  ",
                             is_unsigned_integer_type(left_type)) ||
            !coerce_ir_value(*this, right_val, right_ir_type,
                             comparison_type, "  ",
                             is_unsigned_integer_type(right_type))) {
            gerror("Cannot normalize operands for integer comparison :/\n");
            return "false";
        }

        if (cmp_op == "sgt" || cmp_op == "slt" ||
            cmp_op == "sge" || cmp_op == "sle") {
            if (is_unsigned_integer_type(left_type) ||
                is_unsigned_integer_type(right_type)) {
                cmp_op[0] = 'u';
            }
        }
        std::string ssa = next_ssa();
        body << "  " << ssa << " = icmp " << cmp_op << " "
             << llvm_ir_type_name(comparison_type) << " "
             << left_val << ", " << right_val << "\n";
        return ssa;
        }
        
        else if (bin->op == "+" || bin->op == "-" || bin->op == "*" ||
                 bin->op == "/" || bin->op == "%") {
            
            BType left_type = get_expr_type(bin->left.get());
            BType right_type = get_expr_type(bin->right.get());
        if (is_float_type(left_type) || is_float_type(right_type)) {
            std::string left_val = left;
            std::string right_val = right;
            IRType result_ir_type =
                left_type == BType::F64 || right_type == BType::F64
                    ? IRType::F64
                    : IRType::F32;
            IRType left_ir_type = btype_to_ir(left_type);
            IRType right_ir_type = btype_to_ir(right_type);
            if (left_ir_type == IRType::UNKNOWN) left_ir_type = result_ir_type;
            if (right_ir_type == IRType::UNKNOWN) right_ir_type = result_ir_type;
            if (!coerce_ir_value(*this, left_val, left_ir_type,
                                 result_ir_type, "  ",
                                 is_unsigned_integer_type(left_type)) ||
                !coerce_ir_value(*this, right_val, right_ir_type,
                                 result_ir_type, "  ",
                                 is_unsigned_integer_type(right_type))) {
                gerror("Cannot normalize operands for floating arithmetic :/\n");
                return "0";
            }
            
            std::string float_op;
            if (bin->op == "+") float_op = "fadd";
            else if (bin->op == "-") float_op = "fsub";
            else if (bin->op == "*") float_op = "fmul";
            else if (bin->op == "/") float_op = "fdiv";
            else if (bin->op == "%") float_op = "frem";
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = " << float_op << " "
                 << llvm_ir_type_name(result_ir_type) << " "
                 << left_val << ", " << right_val << "\n";
            return ssa;
        }
        
        IRType left_ir_type = btype_to_ir(left_type);
        IRType right_ir_type = btype_to_ir(right_type);
        if (left_ir_type == IRType::UNKNOWN) left_ir_type = IRType::I64;
        if (right_ir_type == IRType::UNKNOWN) right_ir_type = IRType::I64;
        if (!is_ir_integer(left_ir_type) || !is_ir_integer(right_ir_type)) {
            gerror("Arithmetic operator '" + bin->op +
                   "' requires numeric operands :/\n");
            return "0";
        }

        BType result_btype = get_expr_type(bin);
        IRType result_ir_type = btype_to_ir(result_btype);
        if (!is_ir_integer(result_ir_type)) {
            result_ir_type =
                ir_integer_bits(left_ir_type) >= ir_integer_bits(right_ir_type)
                    ? left_ir_type
                    : right_ir_type;
        }

        
        
        
        if (bin->op == "/" &&
            (is_unsigned_integer_type(left_type) ||
             is_unsigned_integer_type(right_type))) {
            op = "udiv";
        } else if (bin->op == "%" &&
                   (is_unsigned_integer_type(left_type) ||
                    is_unsigned_integer_type(right_type))) {
            op = "urem";
        }

        std::string left_value = left;
        std::string right_value = right;
        if (!coerce_ir_value(
                *this, left_value, left_ir_type, result_ir_type, "  ",
                is_unsigned_integer_type(left_type)) ||
            !coerce_ir_value(
                *this, right_value, right_ir_type, result_ir_type, "  ",
                is_unsigned_integer_type(right_type))) {
            gerror("Cannot normalize operands for arithmetic operator '" +
                   bin->op + "' :/\n");
            return "0";
        }

        std::string ssa = next_ssa();
        body << "  " << ssa << " = " << op << " "
             << llvm_ir_type_name(result_ir_type) << " "
             << left_value << ", " << right_value << "\n";
        return ssa;
        }
        else {
            gerror("Operator '" + bin->op +
                   "' has no built-in implementation for non-struct values :/\n");
            return "0";
        }
    }
    
    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        if (!get_expr_struct_name(unary->operand.get()).empty()) {
            auto call = make_operator_call(unary->op, unary->operand.get());
            std::string callee;
            if (!call || !resolve_call_target(call.get(), callee, false)) {
                gerror("No overload for unary operator '" + unary->op +
                       "' on struct '" + get_expr_struct_name(unary->operand.get()) +
                       "' :/\n");
                return "0";
            }
            return emit_expression(call.get());
        }

        std::string val = emit_expression(unary->operand.get());
        if (unary->op == "-") {
            BType operand_type = get_expr_type(unary->operand.get());
            IRType operand_ir = btype_to_ir(operand_type);
            if (operand_type == BType::F64 || operand_type == BType::F32) {
                std::string ssa = next_ssa();
                body << "  " << ssa << " = fsub "
                     << llvm_ir_type_name(operand_ir) << " 0.0, " << val << "\n";
                return ssa;
            }
            if (!is_ir_integer(operand_ir)) operand_ir = IRType::I64;
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sub " << llvm_ir_type_name(operand_ir)
                 << " 0, " << val << "\n";
            return ssa;
        }
        if (unary->op == "!") {
            std::string ssa = next_ssa();
            body << "  " << ssa << " = xor i1 " << val << ", true\n";
            return ssa;
        }
        if (unary->op == "~") {
            BType operand_type = get_expr_type(unary->operand.get());
            if (operand_type == BType::UNKNOWN) operand_type = BType::INT;
            std::string ssa = next_ssa();
            body << "  " << ssa << " = xor " << get_llvm_type(operand_type)
                 << " " << val << ", -1\n";
            return ssa;
        }
        if (unary->op == "+") return val;
    }
    
    if (auto* as_expr = dynamic_cast<const AsExpr*>(expr)) {
        std::string val = emit_expression(as_expr->operand.get());
        BType target_type = as_expr->btype;
        BType source_type = get_expr_type(as_expr->operand.get());
        
        
        if (source_type == BType::UNKNOWN) {
            source_type = BType::INT;
        }
        
        IRType target_ir = btype_to_ir(target_type);
        
        
        IRType source_ir = btype_to_ir(source_type);
        
        
        std::string source_val = val;

        
        
        if (target_type == BType::HEX) {
            if (!is_ir_integer(source_ir)) {
                gerror("Cannot cast non-integer value to hex :/\n");
                return "0";
            }
            if (source_ir == IRType::I64) return source_val;

            std::string ssa = next_ssa();
            body << "  " << ssa << " = zext "
                 << llvm_ir_type_name(source_ir) << " " << source_val
                 << " to i64\n";
            return ssa;
        }

        
        
        
        
        
        
        if ((is_ir_integer(source_ir) || is_ir_float(source_ir)) &&
            (is_ir_integer(target_ir) || is_ir_float(target_ir))) {
            std::string converted = source_val;
            if (coerce_ir_value(
                    *this, converted, source_ir, target_ir, "  ",
                    is_unsigned_integer_type(source_type))) {
                return converted;
            }
        }
        
        
        if (source_ir == IRType::I64 && target_ir == IRType::I32) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = trunc i64 " << source_val << " to i32\n";
            return ssa;
        } else if (source_ir == IRType::I32 && target_ir == IRType::I64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i32 " << source_val << " to i64\n";
            return ssa;
        } else if (source_ir == IRType::I64 && target_ir == IRType::I8) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = trunc i64 " << source_val << " to i8\n";
            return ssa;
        } else if (source_ir == IRType::I8 && target_ir == IRType::I64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i8 " << source_val << " to i64\n";
            return ssa;
        } else if (source_ir == IRType::I64 && target_ir == IRType::I16) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = trunc i64 " << source_val << " to i16\n";
            return ssa;
        } else if (source_ir == IRType::I16 && target_ir == IRType::I64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i16 " << source_val << " to i64\n";
            return ssa;
        } else if (source_ir == IRType::I64 && target_ir == IRType::F64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sitofp i64 " << source_val << " to double\n";
            return ssa;
        } else if (source_ir == IRType::F64 && target_ir == IRType::I64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = fptosi double " << source_val << " to i64\n";
            return ssa;
        } else if (source_ir == IRType::I32 && target_ir == IRType::F64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sitofp i32 " << source_val << " to double\n";
            return ssa;
        } else if (source_ir == IRType::F64 && target_ir == IRType::I32) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = fptosi double " << source_val << " to i32\n";
            return ssa;
        } else if (source_ir == IRType::I8 && target_ir == IRType::I32) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i8 " << source_val << " to i32\n";
            return ssa;
        } else if (source_ir == IRType::I16 && target_ir == IRType::I32) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i16 " << source_val << " to i32\n";
            return ssa;
        } else if (source_ir == IRType::I32 && target_ir == IRType::I8) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = trunc i32 " << source_val << " to i8\n";
            return ssa;
        } else if (source_ir == IRType::I32 && target_ir == IRType::I16) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = trunc i32 " << source_val << " to i16\n";
            return ssa;
        }
        
        else if (source_ir == IRType::I8_PTR && target_ir == IRType::I8) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = getelementptr inbounds i8, i8* " << source_val << ", i32 0\n";
            std::string load_ssa = next_ssa();
            body << "  " << load_ssa << " = load i8, i8* " << ssa << "\n";
            return load_ssa;
        }
        
        else if (source_ir == IRType::I8_PTR && target_ir == IRType::I64) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = getelementptr inbounds i8, i8* " << source_val << ", i32 0\n";
            std::string load_ssa = next_ssa();
            body << "  " << load_ssa << " = load i8, i8* " << ssa << "\n";
            std::string sext_ssa = next_ssa();
            body << "  " << sext_ssa << " = sext i8 " << load_ssa << " to i64\n";
            return sext_ssa;
        }
        
        else if (source_ir == IRType::I8 && target_ir == IRType::I8_PTR) {
            
            std::string ssa = next_ssa();
            body << "  " << ssa << " = alloca [2 x i8]\n";
            std::string gep1 = next_ssa();
            body << "  " << gep1 << " = getelementptr inbounds [2 x i8], [2 x i8]* " << ssa << ", i32 0, i32 0\n";
            body << "  store i8 " << source_val << ", i8* " << gep1 << "\n";
            std::string gep2 = next_ssa();
            body << "  " << gep2 << " = getelementptr inbounds [2 x i8], [2 x i8]* " << ssa << ", i32 0, i32 1\n";
            body << "  store i8 0, i8* " << gep2 << "\n";
            
            std::string result = next_ssa();
            body << "  " << result << " = getelementptr inbounds [2 x i8], [2 x i8]* " << ssa << ", i32 0, i32 0\n";
            return result;
        }
        
        else if (source_ir == IRType::I8 && target_ir == IRType::I32) {
            std::string ssa = next_ssa();
            body << "  " << ssa << " = sext i8 " << source_val << " to i32\n";
            return ssa;
        } else {
            
            return val;
        }
    }
    
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (!call->is_method_call && call->callee == "free") {
            if (call->args.size() != 1) {
                gerror("free() expects exactly one argument :/\n");
                return "0";
            }

            const Expr* argument = call->args.front().get();
            BType argument_type = get_expr_type(argument);
            if (argument_type == BType::UNKNOWN) argument_type = argument->btype;
            if (!is_pointer_like_btype(argument_type) &&
                get_expr_struct_name(argument).empty()) {
                gerror("free() expects a pointer, array, or struct reference :/\n");
                return "0";
            }
            std::string source_type = emitted_pointer_type(*this, argument);
            if (source_type.empty()) {
                gerror("free() expects a pointer, array, or struct reference :/\n");
                return "0";
            }

            std::string address = emit_expression(argument);
            if (address == "0" && !is_null_expression(argument)) {
                gerror("free() argument did not produce an address :/\n");
                return "0";
            }

            if (is_null_expression(argument)) {
                address = "null";
                source_type = "i8*";
            } else if (source_type != "i8*") {
                std::string raw_address = next_ssa();
                body << "  " << raw_address << " = bitcast "
                     << source_type << " " << address << " to i8*\n";
                address = raw_address;
            }

            body << "  call void @free(i8* " << address << ")\n";
            return "0";
        }

        if (!call->is_method_call &&
            (call->callee == "volatile_load" ||
             call->callee == "volatile_store")) {
            const bool is_store = call->callee == "volatile_store";
            const size_t expected_arguments = is_store ? 2 : 1;

            if (call->template_args.size() != 1) {
                gerror(call->callee + " expects exactly one type argument :/\n");
                return "0";
            }
            if (call->args.size() != expected_arguments) {
                gerror(call->callee + " expects " +
                       std::to_string(expected_arguments) + " argument(s) :/\n");
                return "0";
            }

            const BType value_type = call->template_args.front();
            if (!is_supported_volatile_type(value_type)) {
                gerror("Unsupported volatile value type '" +
                       type_name(value_type) + "' :/\n");
                return "0";
            }

            bool valid_address = false;
            const std::string address = emit_typed_address(
                *this, call->args.front().get(), value_type, valid_address,
                "volatile");
            if (!valid_address) return "0";

            const IRType target_ir_type = volatile_value_ir_type(value_type);
            const std::string llvm_value_type =
                llvm_value_type_for_btype(value_type);

            if (!is_store) {
                std::string loaded = next_ssa();
                body << "  " << loaded << " = load volatile "
                     << llvm_value_type << ", " << llvm_value_type
                     << "* " << address << "\n";
                return loaded;
            }

            std::string value = emit_expression(call->args[1].get());
            BType source_type = get_expr_type(call->args[1].get());
            if (source_type == BType::UNKNOWN) {
                source_type = call->args[1]->btype;
            }
            IRType source_ir_type = volatile_value_ir_type(source_type);
            if (!coerce_ir_value(
                    *this, value, source_ir_type, target_ir_type, "  ")) {
                gerror("Cannot convert value passed to volatile_store<" +
                       type_name(value_type) + "> :/\n");
                return "0";
            }

            body << "  store volatile " << llvm_value_type << " " << value
                 << ", " << llvm_value_type << "* " << address << "\n";
            return "0";
        }

        if (!call->is_method_call &&
            (call->callee == "atomic_load" ||
             call->callee == "atomic_store" ||
             call->callee == "atomic_add" ||
             call->callee == "atomic_exchange" ||
             call->callee == "atomic_compare_exchange")) {
            size_t expected_arguments = 1;
            if (call->callee == "atomic_store" ||
                call->callee == "atomic_add" ||
                call->callee == "atomic_exchange") {
                expected_arguments = 2;
            } else if (call->callee == "atomic_compare_exchange") {
                expected_arguments = 3;
            }

            if (call->template_args.size() != 1) {
                gerror(call->callee +
                       " expects exactly one type argument :/\n");
                return "0";
            }
            if (call->args.size() != expected_arguments) {
                gerror(call->callee + " expects " +
                       std::to_string(expected_arguments) +
                       " argument(s) :/\n");
                return "0";
            }

            const BType value_type = call->template_args.front();
            if (!is_supported_atomic_type(value_type)) {
                gerror("Unsupported atomic value type '" +
                       type_name(value_type) + "' :/\n");
                return "0";
            }

            bool valid_address = false;
            const std::string address = emit_typed_address(
                *this, call->args.front().get(), value_type, valid_address,
                "atomic");
            if (!valid_address) return "0";

            const IRType target_ir_type = volatile_value_ir_type(value_type);
            const std::string llvm_value_type =
                llvm_value_type_for_btype(value_type);
            const int alignment = std::max(1, getTypeSize(value_type));

            if (call->callee == "atomic_load") {
                std::string loaded = next_ssa();
                body << "  " << loaded << " = load atomic "
                     << llvm_value_type << ", " << llvm_value_type << "* "
                     << address << " seq_cst, align " << alignment << "\n";
                return loaded;
            }

            bool valid_values = true;
            const auto emit_atomic_value = [&](size_t index) {
                std::string value = emit_expression(call->args[index].get());
                BType source_type = get_expr_type(call->args[index].get());
                if (source_type == BType::UNKNOWN) {
                    source_type = call->args[index]->btype;
                }
                IRType source_ir_type = volatile_value_ir_type(source_type);
                if (!coerce_ir_value(
                        *this, value, source_ir_type, target_ir_type, "  ",
                        is_unsigned_integer_type(source_type))) {
                    gerror("Cannot convert argument " +
                           std::to_string(index + 1) + " in call to '" +
                           call->callee + "' :/\n");
                    valid_values = false;
                }
                return value;
            };

            const std::string first_value = emit_atomic_value(1);
            if (!valid_values) return "0";

            if (call->callee == "atomic_store") {
                body << "  store atomic " << llvm_value_type << " "
                     << first_value << ", " << llvm_value_type << "* "
                     << address << " seq_cst, align " << alignment << "\n";
                return "0";
            }

            if (call->callee == "atomic_add" ||
                call->callee == "atomic_exchange") {
                std::string previous = next_ssa();
                const std::string operation =
                    call->callee == "atomic_add" ? "add" : "xchg";
                body << "  " << previous << " = atomicrmw " << operation
                     << " " << llvm_value_type << "* " << address << ", "
                     << llvm_value_type << " " << first_value
                     << " seq_cst\n";
                return previous;
            }

            const std::string desired = emit_atomic_value(2);
            if (!valid_values) return "0";
            std::string result_pair = next_ssa();
            body << "  " << result_pair << " = cmpxchg "
                 << llvm_value_type << "* " << address << ", "
                 << llvm_value_type << " " << first_value << ", "
                 << llvm_value_type << " " << desired
                 << " seq_cst seq_cst\n";
            std::string succeeded = next_ssa();
            body << "  " << succeeded << " = extractvalue { "
                 << llvm_value_type << ", i1 } " << result_pair << ", 1\n";
            return succeeded;
        }

        
        
        if (!call->is_method_call && call->callee == "platform") {
            if (!call->args.empty()) {
                gerror("platform() expects no arguments :/\n");
            }

            StringExpr platform_literal;
            platform_literal.value = compiler_platform_name();
            platform_literal.btype = BType::STR;
            return emit_expression(&platform_literal);
        }

        
        if (!call->is_method_call && call->callee == "typeof") {
            if (!call->args.empty()) {
                BType arg_type = get_expr_type(call->args[0].get());
                
                if (arg_type == BType::UNKNOWN) {
                    arg_type = call->args[0]->btype;
                }
                
                std::string type_name;
                if (arg_type == BType::STR) {
                    type_name = "@.type_str_str";
                } else if (arg_type == BType::INT) {
                    type_name = "@.type_str_int";
                } else if (arg_type == BType::F64) {
                    type_name = "@.type_str_f64";
                } else if (arg_type == BType::BOOL) {
                    type_name = "@.type_str_bol";
                } else if (arg_type == BType::ARR || arg_type == BType::INT_ARR || arg_type == BType::F64_ARR || 
                           arg_type == BType::BOOL_ARR || arg_type == BType::STR_ARR || arg_type == BType::PTR_ARR ||
                           arg_type == BType::I8_ARR || arg_type == BType::I16_ARR || arg_type == BType::I32_ARR ||
                           arg_type == BType::I64_ARR || arg_type == BType::U8_ARR || arg_type == BType::U16_ARR ||
                           arg_type == BType::U32_ARR || arg_type == BType::U64_ARR || arg_type == BType::F32_ARR ||
                           arg_type == BType::I8_PTR_ARR || arg_type == BType::I16_PTR_ARR || arg_type == BType::I32_PTR_ARR ||
                           arg_type == BType::I64_PTR_ARR || arg_type == BType::U8_PTR_ARR || arg_type == BType::U16_PTR_ARR ||
                           arg_type == BType::U32_PTR_ARR || arg_type == BType::U64_PTR_ARR || arg_type == BType::F32_PTR_ARR ||
                           arg_type == BType::F64_PTR_ARR || arg_type == BType::STR_PTR_ARR) {
                    type_name = "@.type_str_arr";
                } else if (arg_type == BType::OBJ) {
                    type_name = "@.type_str_obj";
                } else if (arg_type == BType::FUNC) {
                    type_name = "@.type_str_fn";
                } else if (arg_type == BType::VOID) {
                    type_name = "@.type_str_nul";
                } else if (arg_type == BType::UNKNOWN) {
                    type_name = "@.type_str_nul";
                } else if (arg_type == BType::PTR) {
                    type_name = "@.type_str_ptr";
                } else if (arg_type == BType::ISIZE) {
                    type_name = "@.type_str_isize";
                } else if (arg_type == BType::USIZE) {
                    type_name = "@.type_str_usize";
                } else if (arg_type == BType::HEX) {
                    type_name = "@.type_str_hex";
                } else if (arg_type == BType::I8) {
                    type_name = "@.type_str_i8";
                } else if (arg_type == BType::I16) {
                    type_name = "@.type_str_i16";
                } else if (arg_type == BType::I32) {
                    type_name = "@.type_str_i32";
                } else if (arg_type == BType::I64) {
                    type_name = "@.type_str_i64";
                } else if (arg_type == BType::U8) {
                    type_name = "@.type_str_u8";
                } else if (arg_type == BType::U16) {
                    type_name = "@.type_str_u16";
                } else if (arg_type == BType::U32) {
                    type_name = "@.type_str_u32";
                } else if (arg_type == BType::U64) {
                    type_name = "@.type_str_u64";
                } else if (arg_type == BType::F32) {
                    type_name = "@.type_str_f32";
                } else if (arg_type == BType::TUPLE) {
                    type_name = "@.type_str_tup";
                } else {
                    type_name = "@.type_str_nul";
                }
                
                
                std::string ptr = next_ssa();
                
                if (type_name == "@.type_str_str" || type_name == "@.type_str_int" || 
                    type_name == "@.type_str_f64" || type_name == "@.type_str_bol" ||
                    type_name == "@.type_str_arr" || type_name == "@.type_str_obj" ||
                    type_name == "@.type_str_nul" || type_name == "@.type_str_ptr" ||
                    type_name == "@.type_str_hex") {
                    body << "  " << ptr << " = getelementptr inbounds [4 x i8], [4 x i8]* " << type_name << ", i32 0, i32 0\n";
                } else if (type_name == "@.type_str_i8" || type_name == "@.type_str_u8" ||
                           type_name == "@.type_str_fn") {
                    body << "  " << ptr << " = getelementptr inbounds [5 x i8], [5 x i8]* " << type_name << ", i32 0, i32 0\n";
                } else if (type_name == "@.type_str_isize" ||
                           type_name == "@.type_str_usize") {
                    body << "  " << ptr << " = getelementptr inbounds [6 x i8], [6 x i8]* " << type_name << ", i32 0, i32 0\n";
                } else if (type_name == "@.type_str_i16" || type_name == "@.type_str_i32" || 
                           type_name == "@.type_str_i64" || type_name == "@.type_str_u16" ||
                           type_name == "@.type_str_u32" || type_name == "@.type_str_u64" ||
                           type_name == "@.type_str_f32") {
                    body << "  " << ptr << " = getelementptr inbounds [4 x i8], [4 x i8]* " << type_name << ", i32 0, i32 0\n";
                } else if (type_name == "@.type_str_tup") {
                    body << "  " << ptr << " = getelementptr inbounds [4 x i8], [4 x i8]* " << type_name << ", i32 0, i32 0\n";
                }else {
                    body << "  " << ptr << " = getelementptr inbounds [4 x i8], [4 x i8]* @.type_str_nul, i32 0, i32 0\n";
                }
                return ptr;
            }
            
            std::string ptr = next_ssa();
            body << "  " << ptr << " = getelementptr inbounds [4 x i8], [4 x i8]* @.type_str_nul, i32 0, i32 0\n";
            return ptr;
        }

        auto function_pointer = vars.find(call->callee);
        if (!call->is_method_call &&
            function_pointer != vars.end() &&
            function_pointer->second.is_function_pointer) {
            LLVMVar& variable = function_pointer->second;
            if (!variable.function_signature_known) {
                gerror("Cannot call function pointer '" + call->callee +
                       "' before its signature is known :/\n");
                return "0";
            }

            if (variable.function_argument_types.size() != call->args.size()) {
                gerror("Call through function pointer '" + call->callee +
                       "' expects " +
                       std::to_string(variable.function_argument_types.size()) +
                       " argument(s), got " +
                       std::to_string(call->args.size()) + " :/\n");
                return "0";
            }

            const std::string function_type = llvm_function_pointer_type(
                variable.function_return_type,
                variable.function_argument_types
            );
            if (function_type.empty()) {
                gerror("Function pointer '" + call->callee +
                       "' has an unsupported signature :/\n");
                return "0";
            }

            std::string arguments;
            for (size_t i = 0; i < call->args.size(); ++i) {
                std::string value = emit_expression(call->args[i].get());
                BType source_btype = get_expr_type(call->args[i].get());
                if (source_btype == BType::UNKNOWN) {
                    source_btype = call->args[i]->btype;
                }
                IRType source_type = btype_to_ir(source_btype);
                const IRType expected_type =
                    variable.function_argument_types[i];
                if (source_type == IRType::UNKNOWN) {
                    source_type = expected_type;
                } else if (source_type != expected_type &&
                           !coerce_ir_value(
                               *this, value, source_type, expected_type, "  ")) {
                    gerror("Cannot convert argument " + std::to_string(i + 1) +
                           " in call through function pointer '" +
                           call->callee + "' :/\n");
                    return "0";
                }

                if (i != 0) arguments += ", ";
                arguments += llvm_ir_type_name(expected_type) + " " + value;
            }

            VariableExpr pointer_expression;
            pointer_expression.name = call->callee;
            const std::string raw_pointer = emit_expression(&pointer_expression);
            const std::string typed_pointer = next_ssa();
            body << "  " << typed_pointer << " = bitcast "
                 << llvm_ir_type_name(variable.type) << " " << raw_pointer
                 << " to " << function_type << "\n";

            if (variable.function_return_type == IRType::VOID) {
                body << "  call void " << typed_pointer
                     << "(" << arguments << ")\n";
                return "0";
            }

            std::string result = next_ssa();
            body << "  " << result << " = call "
                 << llvm_ir_type_name(variable.function_return_type) << " "
                 << typed_pointer << "(" << arguments << ")\n";
            return result;
        }
        
        std::string callee_name;
        if (!resolve_call_target(call, callee_name, true)) {
            return "0";
        }

        const FnDecl* external_function = nullptr;
        auto external = extern_functions.find(callee_name);
        if (external != extern_functions.end()) {
            external_function = external->second;
        }

        const bool variadic_call =
            variadic_functions.count(callee_name) != 0;
        const size_t fixed_argument_count =
            func_arg_types.count(callee_name)
                ? func_arg_types[callee_name].size()
                : 0;
        const bool wrong_argument_count =
            func_arg_types.count(callee_name) &&
            (variadic_call
                ? call->args.size() < fixed_argument_count
                : call->args.size() != fixed_argument_count);
        if (wrong_argument_count) {
            size_t expected = func_arg_types[callee_name].empty()
                ? 0
                : func_arg_types[callee_name].size() - (call->is_method_call ? 1 : 0);
            size_t actual = call->args.size() - (call->is_method_call ? 1 : 0);
            gerror("Call to '" + call->callee + "' expects " +
                   (variadic_call ? "at least " : "") +
                   std::to_string(expected) + " argument(s), got " +
                   std::to_string(actual) + " :/\n");
            return "0";
        }
        
        std::string args_str;
        for (size_t i = 0; i < call->args.size(); i++) {
            if (i > 0) args_str += ", ";
            
            
            std::string arg_val = emit_expression(call->args[i].get());
            BType arg_btype = get_expr_type(call->args[i].get());
            if (arg_btype == BType::UNKNOWN) {
                arg_btype = call->args[i]->btype;
            }
            
            
            IRType arg_type = IRType::UNKNOWN;
            
            
            bool is_array_arg = false;
            bool is_struct_arg = false;
            std::string struct_arg_name;
            std::string struct_array_arg_name =
                struct_array_element_name(*this, call->args[i].get());
            bool struct_array_arg_inline =
                !struct_array_arg_name.empty() &&
                is_inline_struct_array_expression(
                    *this, call->args[i].get());
            BType arr_elem_type = BType::INT;

            if (!struct_array_arg_name.empty()) {
                is_array_arg = true;
                arr_elem_type = BType::STRUCT;
            }

            
            
            
            struct_arg_name = get_expr_struct_name(call->args[i].get());
            is_struct_arg = !struct_arg_name.empty();
            if (auto* var = dynamic_cast<const VariableExpr*>(call->args[i].get())) {
                if (vars.count(var->name)) {
                    LLVMVar& vv = vars[var->name];
                    if (vv.type == IRType::ARR) {
                        is_array_arg = true;
                        arr_elem_type = vv.elem_type;
                        if (arr_elem_type == BType::UNKNOWN) arr_elem_type = BType::INT;
                        if ((arr_elem_type == BType::STRUCT ||
                             arr_elem_type == BType::TUPLE) &&
                            !vv.struct_name.empty()) {
                            struct_array_arg_name = vv.struct_name;
                            struct_array_arg_inline =
                                vv.inline_struct_array;
                        }
                    } else if (vv.type == IRType::STRUCT) {
                        
                        is_struct_arg = true;
                        struct_arg_name = vv.struct_name;
                    } else {
                        arg_type = vv.type;
                    }
                } else if (global_vars.count(var->name)) {
                    arg_type = global_vars[var->name];
                }
            }
            
            
            if (auto* ref = dynamic_cast<const RefExpr*>(call->args[i].get())) {
                if (auto* var = dynamic_cast<const VariableExpr*>(ref->operand.get())) {
                    if (vars.count(var->name)) {
                        LLVMVar& v = vars[var->name];
                        if (v.type == IRType::I64) arg_type = IRType::I64_PTR;
                        else if (v.type == IRType::I32) arg_type = IRType::I32_PTR;
                        else if (v.type == IRType::I16) arg_type = IRType::I16_PTR;
                        else if (v.type == IRType::I8) arg_type = IRType::I8_PTR;
                        else if (v.type == IRType::F64) arg_type = IRType::F64_PTR;
                        else if (v.type == IRType::F32) arg_type = IRType::F32_PTR;
                        else if (v.type == IRType::STRUCT) {
                            
                            
                            
                            
                            arg_type = IRType::I64_PTR;
                        }
                        else arg_type = v.type;
                    }
                }
            }
            
            
            if (arg_type == IRType::UNKNOWN) {
                arg_type = btype_to_ir(arg_btype);
            }

            
            
            
            
            if (external_function && i < external_function->params.size()) {
                const ParamDecl& parameter = external_function->params[i];
                const TypeRef& parameter_type = parameter.type_ref;
                if (parameter_type.base == BType::STRUCT) {
                    const std::string expected_struct = extern_struct_name(
                        *this, parameter_type, parameter.struct_name);
                    const std::string value_type =
                        get_struct_type_str(expected_struct);

                    if (extern_opaque_struct_by_value(
                            *this, parameter_type,
                            parameter.struct_name)) {
                        gerror("Cannot pass opaque struct '" +
                               expected_struct + "' by value to '" +
                               call->callee + "' :/\n");
                        return "0";
                    }

                    if (!parameter_type.is_pointer &&
                        !parameter_type.is_array) {
                        if (!is_struct_arg ||
                            struct_arg_name != expected_struct) {
                            gerror("Cannot convert argument " +
                                   std::to_string(i + 1) +
                                   " in call to '" + call->callee +
                                   "'; expected struct '" +
                                   expected_struct + "' :/\n");
                            return "0";
                        }

                        const ExternStructAbi abi = get_extern_struct_abi(
                            *this, parameter_type,
                            parameter.struct_name);
                        if (abi.parameter_indirect) {
                            if (extern_indirect_struct_uses_byval()) {
                                args_str += value_type + "* byval(" +
                                    value_type + ") align 8 " + arg_val;
                            } else {
                                
                                
                                
                                std::string copied_value = next_ssa();
                                body << "  " << copied_value << " = load "
                                     << value_type << ", " << value_type
                                     << "* " << arg_val << "\n";
                                std::string copied_storage = next_ssa();
                                body << "  " << copied_storage << " = alloca "
                                     << value_type << "\n";
                                body << "  store " << value_type << " "
                                     << copied_value << ", " << value_type
                                     << "* " << copied_storage << "\n";
                                args_str += value_type + "* " + copied_storage;
                            }
                            continue;
                        }

                        if (!abi.direct_parameter_type.empty()) {
                            std::string aggregate_pointer = next_ssa();
                            body << "  " << aggregate_pointer << " = bitcast "
                                 << value_type << "* " << arg_val << " to "
                                 << abi.direct_parameter_type << "*\n";
                            std::string aggregate_value = next_ssa();
                            body << "  " << aggregate_value << " = load "
                                 << abi.direct_parameter_type << ", "
                                 << abi.direct_parameter_type << "* "
                                 << aggregate_pointer << "\n";
                            args_str += abi.direct_parameter_type + " " +
                                aggregate_value;
                            continue;
                        }

                        std::string byte_pointer;
                        for (size_t piece_index = 0;
                             piece_index < abi.pieces.size();
                             ++piece_index) {
                            const ExternAbiPiece& piece =
                                abi.pieces[piece_index];
                            std::string piece_pointer;
                            if (piece.offset == 0) {
                                piece_pointer = next_ssa();
                                body << "  " << piece_pointer
                                     << " = bitcast " << value_type << "* "
                                     << arg_val << " to " << piece.llvm_type
                                     << "*\n";
                            } else {
                                if (byte_pointer.empty()) {
                                    byte_pointer = next_ssa();
                                    body << "  " << byte_pointer
                                         << " = bitcast " << value_type
                                         << "* " << arg_val << " to i8*\n";
                                }
                                std::string offset_pointer = next_ssa();
                                body << "  " << offset_pointer
                                     << " = getelementptr inbounds i8, i8* "
                                     << byte_pointer << ", i64 "
                                     << piece.offset << "\n";
                                piece_pointer = next_ssa();
                                body << "  " << piece_pointer
                                     << " = bitcast i8* " << offset_pointer
                                     << " to " << piece.llvm_type << "*\n";
                            }

                            std::string piece_value = next_ssa();
                            body << "  " << piece_value << " = load "
                                 << piece.llvm_type << ", "
                                 << piece.llvm_type << "* "
                                 << piece_pointer << "\n";
                            if (piece_index != 0) args_str += ", ";
                            args_str += piece.llvm_type + " " + piece_value;
                        }
                        continue;
                    }

                    const std::string expected_pointer_type =
                        parameter_type.is_array
                            ? value_type + "**"
                            : value_type + "*";
                    if (is_null_expression(call->args[i].get())) {
                        args_str += expected_pointer_type + " null";
                        continue;
                    }

                    std::string source_pointer_type;
                    if (is_struct_arg) {
                        source_pointer_type =
                            get_struct_type_str(struct_arg_name) + "*";
                    } else {
                        source_pointer_type = emitted_pointer_type(
                            *this, call->args[i].get());
                    }
                    if (source_pointer_type.empty()) {
                        gerror("Cannot convert argument " +
                               std::to_string(i + 1) +
                               " in call to '" + call->callee +
                               "' to a struct pointer :/\n");
                        return "0";
                    }
                    if (source_pointer_type != expected_pointer_type) {
                        std::string converted = next_ssa();
                        body << "  " << converted << " = bitcast "
                             << source_pointer_type << " " << arg_val
                             << " to " << expected_pointer_type << "\n";
                        arg_val = converted;
                    }
                    args_str += expected_pointer_type + " " + arg_val;
                    continue;
                }
            }
            
            
            if (is_array_arg) {
                if ((arr_elem_type == BType::STRUCT ||
                     arr_elem_type == BType::TUPLE) &&
                    !struct_array_arg_name.empty()) {
                    args_str += get_struct_type_str(
                        struct_array_arg_name) +
                        (struct_array_arg_inline ? "* " : "** ") +
                        arg_val;
                } else {
                    args_str += get_array_ptr_type(arr_elem_type) + " " + arg_val;
                }
                continue;
            }
            
            
            if (is_struct_arg) {
                bool pass_by_value = false;
                std::string expected_struct = struct_arg_name;

                auto modes = func_param_by_value.find(callee_name);
                if (modes != func_param_by_value.end() &&
                    i < modes->second.size()) {
                    pass_by_value = modes->second[i];
                }
                auto names = func_param_struct_names.find(callee_name);
                if (names != func_param_struct_names.end() &&
                    i < names->second.size() &&
                    !names->second[i].empty()) {
                    expected_struct = names->second[i];
                }

                if (struct_arg_name != expected_struct) {
                    gerror("Cannot convert argument " +
                           std::to_string(i + 1) + " in call to '" +
                           call->callee + "'; expected struct '" +
                           expected_struct + "' :/\n");
                    return "0";
                }

                const std::string struct_type =
                    get_struct_type_str(expected_struct);
                if (pass_by_value) {
                    std::string aggregate = next_ssa();
                    body << "  " << aggregate << " = load " << struct_type
                         << ", " << struct_type << "* " << arg_val << "\n";
                    args_str += struct_type + " " + aggregate;
                } else {
                    args_str += struct_type + "* " + arg_val;
                }
                continue;
            }
            
            
            const bool variadic_argument =
                variadic_call && i >= fixed_argument_count;
            IRType expected_type = IRType::I64;
            if (variadic_argument) {
                
                
                
                expected_type = arg_type;
                if (expected_type == IRType::I1 ||
                    expected_type == IRType::I8 ||
                    expected_type == IRType::I16) {
                    expected_type = IRType::I32;
                } else if (expected_type == IRType::F32) {
                    expected_type = IRType::F64;
                } else if (expected_type == IRType::UNKNOWN) {
                    expected_type = IRType::I64;
                }
            } else if (func_arg_types.count(callee_name) &&
                       i < func_arg_types[callee_name].size()) {
                expected_type = func_arg_types[callee_name][i];
            }
            
            
            if (arg_type == IRType::UNKNOWN) {
                arg_type = expected_type;
            } else if (arg_type != expected_type) {
                if (!coerce_ir_value(
                        *this, arg_val, arg_type, expected_type, "  ",
                        is_unsigned_integer_type(arg_btype))) {
                    gerror("Cannot convert argument " + std::to_string(i + 1) +
                           " in call to '" + call->callee + "' :/\n");
                    return "0";
                }
                arg_type = expected_type;
            }

            args_str += llvm_ir_type_name(arg_type) + " " + arg_val;
        }
        
        
        IRType ret_type = IRType::I64;
        if (func_types.count(callee_name)) {
            ret_type = func_types[callee_name];
        }
        BType return_btype = BType::UNKNOWN;
        if (func_return_btypes.count(callee_name)) {
            return_btype = func_return_btypes[callee_name];
        }

        if (external_function &&
            external_function->return_type_ref.base == BType::STRUCT) {
            const TypeRef& return_type =
                external_function->return_type_ref;
            const std::string struct_name = extern_struct_name(
                *this, return_type,
                external_function->return_type_annotation);
            const std::string value_type =
                get_struct_type_str(struct_name);

            if (extern_opaque_struct_by_value(
                    *this, return_type,
                    external_function->return_type_annotation)) {
                gerror("Cannot return opaque struct '" + struct_name +
                       "' by value from '" + call->callee + "' :/\n");
                return "0";
            }

            if (return_type.is_pointer || return_type.is_array) {
                const std::string pointer_type = return_type.is_array
                    ? value_type + "**"
                    : value_type + "*";
                std::string result = next_ssa();
                body << "  " << result << " = call " << pointer_type
                     << " @" << callee_name << "(" << args_str << ")\n";
                return result;
            }

            std::string storage = next_ssa();
            body << "  " << storage << " = alloca " << value_type << "\n";

            const ExternStructAbi abi = get_extern_struct_abi(
                *this, return_type,
                external_function->return_type_annotation);
            if (abi.return_indirect) {
                std::string call_arguments = value_type + "* sret(" +
                    value_type + ") align " +
                    std::to_string(abi.layout.alignment) + " " + storage;
                if (!args_str.empty()) call_arguments += ", " + args_str;
                body << "  call void @" << callee_name << "("
                     << call_arguments << ")\n";
                return storage;
            }

            const std::string coerced_type =
                extern_coerced_return_type(abi);
            std::string coerced_value = next_ssa();
            body << "  " << coerced_value << " = call " << coerced_type
                 << " @" << callee_name << "(" << args_str << ")\n";
            std::string coerced_pointer = next_ssa();
            body << "  " << coerced_pointer << " = bitcast "
                 << value_type << "* " << storage << " to "
                 << coerced_type << "*\n";
            body << "  store " << coerced_type << " " << coerced_value
                 << ", " << coerced_type << "* " << coerced_pointer
                 << "\n";
            return storage;
        }

        if (variadic_call) {
            std::string return_type_name;
            if (is_array_type(return_btype)) {
                BType element_type = get_array_elem_type(return_btype);
                if (element_type == BType::UNKNOWN) {
                    element_type = BType::INT;
                }
                return_type_name = get_array_ptr_type(element_type);
            } else {
                return_type_name = llvm_ir_type_name(ret_type);
            }

            std::vector<std::string> fixed_types;
            auto exact_signature =
                extern_variadic_fixed_types.find(callee_name);
            if (exact_signature != extern_variadic_fixed_types.end()) {
                fixed_types = exact_signature->second;
            } else if (func_arg_types.count(callee_name)) {
                for (IRType type : func_arg_types[callee_name]) {
                    fixed_types.push_back(llvm_ir_type_name(type));
                }
            }

            std::string function_type = return_type_name + " (";
            for (size_t i = 0; i < fixed_types.size(); ++i) {
                if (i != 0) function_type += ", ";
                function_type += fixed_types[i];
            }
            if (!fixed_types.empty()) function_type += ", ";
            function_type += "...)";

            if (ret_type == IRType::VOID) {
                body << "  call " << function_type << " @" << callee_name
                     << "(" << args_str << ")\n";
                return "0";
            }

            std::string ssa = next_ssa();
            body << "  " << ssa << " = call " << function_type << " @"
                 << callee_name << "(" << args_str << ")\n";
            return ssa;
        }

        
        
        
        if (is_array_type(return_btype)) {
            BType element_type = get_array_elem_type(return_btype);
            if (element_type == BType::UNKNOWN) element_type = BType::INT;
            const std::string pointer_type =
                get_array_ptr_type(element_type);
            std::string ssa = next_ssa();
            body << "  " << ssa << " = call " << pointer_type
                 << " @" << callee_name << "(" << args_str << ")\n";
            return ssa;
        }
        
        
        if (ret_type == IRType::VOID) {
            body << "  call void @" << callee_name << "(" << args_str << ")\n";
            return "0";
        }
        
        
        if (ret_type == IRType::I8_PTR) {
            std::string ssa = next_ssa();
            body << "  " << ssa << " = call i8* @" << callee_name << "(" << args_str << ")\n";
            return ssa;
        }
        
        
        if (ret_type == IRType::PTR) {
            std::string ssa = next_ssa();
            body << "  " << ssa << " = call i8* @" << callee_name << "(" << args_str << ")\n";
            return ssa;
        }

        
        if (ret_type == IRType::STRUCT) {
            auto emitter_it = g_struct_return_types.find(this);

            if (emitter_it == g_struct_return_types.end() ||
                !emitter_it->second.count(callee_name)) {
                gerror(
                    "Unknown concrete struct return type for '" +
                    callee_name + "' :/\n"
                );
                return "0";
            }
            const std::string& struct_name =
                emitter_it->second.at(callee_name);
            std::string ssa = next_ssa();

            body << "  " << ssa
                 << " = call "
                 << get_struct_type_str(struct_name)
                 << "* @" << callee_name
                 << "(" << args_str << ")\n";
            return ssa;
        }
        
        std::string ssa = next_ssa();
        body << "  " << ssa << " = call " << llvm_ir_type_name(ret_type) 
             << " @" << callee_name << "(" << args_str << ")\n";
        return ssa;
    }
    
    if (auto* arr = dynamic_cast<const ArrayExpr*>(expr)) {
        BType element_type = BType::UNKNOWN;
        for (const auto& element : arr->elements) {
            element_type = get_expr_type(element.get());
            if (element_type == BType::UNKNOWN) element_type = element->btype;
            if (element_type != BType::UNKNOWN) break;
        }
        if (element_type == BType::UNKNOWN) element_type = BType::INT;
        return emit_array_literal(arr, element_type);
    }
    
    if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        const std::string tuple_name =
            get_expr_struct_name(idx->object.get());
        auto tuple = structs.find(tuple_name);
        if (tuple != structs.end() && tuple->second.is_tuple) {
            auto* number = dynamic_cast<const NumberExpr*>(idx->index.get());
            if (!number || number->is_float || number->value < 0 ||
                static_cast<size_t>(number->value) >= tuple->second.field_types.size()) {
                gerror("Tuple indices must be in-range integer literals :/\n");
                return "0";
            }
            std::string tuple_ptr = emit_lvalue(idx->object.get());
            if (tuple_ptr == "0") return "0";
            std::string field = next_ssa();
            const std::string tuple_type = get_struct_type_str(tuple_name);
            body << "  " << field << " = getelementptr inbounds "
                 << tuple_type << ", " << tuple_type << "* " << tuple_ptr
                 << ", i32 0, i32 " << static_cast<size_t>(number->value)
                 << "\n";
            const BType element_type =
                tuple->second.field_types[static_cast<size_t>(number->value)];
            if (is_aggregate_type(element_type)) return field;
            const std::string element_llvm = get_llvm_type(element_type);
            std::string value = next_ssa();
            body << "  " << value << " = load " << element_llvm << ", "
                 << element_llvm << "* " << field << "\n";
            return value;
        }

        if (struct_array_element_name(*this, idx->object.get()).empty() &&
            !get_expr_struct_name(idx->object.get()).empty()) {
            auto call = make_operator_call("[]", idx->object.get(), idx->index.get());
            std::string callee;
            if (!call || !resolve_call_target(call.get(), callee, false)) {
                gerror("No overload for operator '[]' on struct '" +
                       get_expr_struct_name(idx->object.get()) + "' :/\n");
                return "0";
            }
            return emit_expression(call.get());
        }

        const BType object_type = get_expr_type(idx->object.get());
        if (object_type == BType::STR || object_type == BType::I8_PTR) {
            std::string string_pointer = emit_expression(idx->object.get());
            std::string index = emit_expression(idx->index.get());
            if (!normalize_integer_to_i64(
                    *this, idx->index.get(), index, "String index")) {
                return "0";
            }

            std::string element_pointer = next_ssa();
            body << "  " << element_pointer
                 << " = getelementptr inbounds i8, i8* " << string_pointer
                 << ", i64 " << index << "\n";
            std::string element = next_ssa();
            body << "  " << element << " = load i8, i8* "
                 << element_pointer << "\n";
            return element;
        }


        const std::string struct_element =
            struct_array_element_name(*this, idx->object.get());
        if (!struct_element.empty()) {
            std::string element_slot = emit_lvalue(idx);
            if (element_slot == "0") return "0";

            if (is_inline_struct_array_expression(
                    *this, idx->object.get())) {
                
                
                return element_slot;
            }

            const std::string element_type =
                get_struct_type_str(struct_element) + "*";
            std::string element = next_ssa();
            body << "  " << element << " = load " << element_type
                 << ", " << element_type << "* " << element_slot << "\n";
            return element;
        }

        std::string element_ptr = emit_lvalue(idx);
        BType element_type = get_expr_type(idx);
        if (element_ptr == "0" || element_type == BType::UNKNOWN) return "0";

        std::string loaded = next_ssa();
        body << "  " << loaded << " = load " << get_llvm_type(element_type)
             << ", " << get_llvm_type(element_type) << "* " << element_ptr << "\n";
        return loaded;
    }
    
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        std::string field_ptr = emit_lvalue(member);
        BType field_type = get_expr_type(member);
        if (field_ptr == "0" || field_type == BType::UNKNOWN) return "0";

        if (is_struct_type(field_type)) return field_ptr;

        const std::string element_struct =
            struct_array_element_name(*this, member);
        if (!element_struct.empty()) {
            const std::string array_type =
                get_struct_type_str(element_struct) +
                (struct_array_field_is_inline(*this, member)
                    ? "*"
                    : "**");
            std::string loaded = next_ssa();
            body << "  " << loaded << " = load " << array_type
                 << ", " << array_type << "* " << field_ptr << "\n";
            return loaded;
        }
        
        
        if (is_array_type(field_type)) {
            std::string loaded = next_ssa();
            body << "  " << loaded << " = load " << get_llvm_type(field_type)
                 << ", " << get_llvm_type(field_type) << "* " << field_ptr << "\n";
            return loaded;
        }

        std::string loaded = next_ssa();
        body << "  " << loaded << " = load " << get_llvm_type(field_type)
             << ", " << get_llvm_type(field_type) << "* " << field_ptr << "\n";
        return loaded;
    }
    
    if (auto* tern = dynamic_cast<const TernaryExpr*>(expr)) {
        std::string cond = emit_expression(tern->cond.get());
        BType condition_type = get_expr_type(tern->cond.get());
        IRType condition_ir = btype_to_ir(condition_type);
        if (condition_ir == IRType::UNKNOWN) condition_ir = IRType::I1;
        if (condition_ir != IRType::I1 &&
            !coerce_ir_value(*this, cond, condition_ir, IRType::I1, "  ")) {
            gerror("Ternary condition must be boolean :/\n");
            return "0";
        }

        BType result_type = get_expr_type(tern);
        IRType result_ir = btype_to_ir(result_type);
        if (result_ir == IRType::UNKNOWN || result_ir == IRType::STRUCT ||
            result_ir == IRType::VOID) {
            gerror("Unsupported ternary result type :/\n");
            return "0";
        }
        const std::string llvm_result_type = llvm_ir_type_name(result_ir);
        std::string result_slot = next_ssa();
        body << "  " << result_slot << " = alloca " << llvm_result_type << "\n";
        
        std::string then_lbl = next_label("tern_then");
        std::string else_lbl = next_label("tern_else");
        std::string end_lbl = next_label("tern_end");
        
        body << "  br i1 " << cond << ", label %" << then_lbl
             << ", label %" << else_lbl << "\n";
        
        body << then_lbl << ":\n";
        std::string then_val = emit_expression(tern->then_expr.get());
        BType then_type = get_expr_type(tern->then_expr.get());
        IRType then_ir = btype_to_ir(then_type);
        if (then_ir == IRType::UNKNOWN) then_ir = result_ir;
        if (then_ir != result_ir &&
            !coerce_ir_value(*this, then_val, then_ir, result_ir, "  ",
                             is_unsigned_integer_type(then_type))) {
            gerror("Incompatible true branch in ternary expression :/\n");
            return "0";
        }
        body << "  store " << llvm_result_type << " " << then_val << ", "
             << llvm_result_type << "* " << result_slot << "\n";
        body << "  br label %" << end_lbl << "\n";
        
        body << else_lbl << ":\n";
        std::string else_val = emit_expression(tern->else_expr.get());
        BType else_type = get_expr_type(tern->else_expr.get());
        IRType else_ir = btype_to_ir(else_type);
        if (else_ir == IRType::UNKNOWN) else_ir = result_ir;
        if (else_ir != result_ir &&
            !coerce_ir_value(*this, else_val, else_ir, result_ir, "  ",
                             is_unsigned_integer_type(else_type))) {
            gerror("Incompatible false branch in ternary expression :/\n");
            return "0";
        }
        body << "  store " << llvm_result_type << " " << else_val << ", "
             << llvm_result_type << "* " << result_slot << "\n";
        body << "  br label %" << end_lbl << "\n";
        
        body << end_lbl << ":\n";
        std::string result = next_ssa();
        body << "  " << result << " = load " << llvm_result_type << ", "
             << llvm_result_type << "* " << result_slot << "\n";
        return result;
    }
    
    if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        if (auto* function = dynamic_cast<const VariableExpr*>(ref->operand.get());
            function && !vars.count(function->name) &&
            !global_vars.count(function->name) &&
            func_types.count(function->name) &&
            func_arg_types.count(function->name)) {
            const std::string function_type = llvm_function_pointer_type(
                func_types[function->name], func_arg_types[function->name]);
            if (function_type.empty()) {
                gerror("Cannot take the address of function '" + function->name +
                       "' because its signature is not representable yet :/\n");
                return "null";
            }

            std::string address = next_ssa();
            body << "  " << address << " = bitcast " << function_type
                 << " @" << function->name << " to i64*\n";
            return address;
        }

        std::string const_name;
        if (is_const_assignment_target(*this, ref->operand.get(), &const_name)) {
            gerror("Cannot take a mutable reference to constant '" +
                   const_name + "' :/\n");
            return "null";
        }

        
        if (auto* var = dynamic_cast<const VariableExpr*>(ref->operand.get())) {
            if (vars.count(var->name)) {
                LLVMVar& v = vars[var->name];
                if (v.type == IRType::STRUCT && !v.struct_name.empty()) {
                    std::string struct_address = emit_lvalue(ref->operand.get());
                    std::string generic_address = next_ssa();
                    body << "  " << generic_address << " = bitcast "
                         << get_struct_type_str(v.struct_name) << "* "
                         << struct_address << " to i64*\n";
                    return generic_address;
                }
                
                return v.alloca;
            }
            
            if (global_vars.count(var->name)) {
                return "@" + var->name;
            }
        }
        
        
        if (dynamic_cast<const MemberExpr*>(ref->operand.get())) {
            std::string field_address = emit_lvalue(ref->operand.get());
            if (field_address == "0") {
                gerror("Cannot take the address of this struct field :/\n");
                return "null";
            }
            return field_address;
        }
        
        if (auto* idx = dynamic_cast<const IndexExpr*>(ref->operand.get())) {
            
            
            
            
            const std::string element_struct =
                struct_array_element_name(*this, idx->object.get());
            if (!element_struct.empty()) {
                std::string object = emit_expression(idx);
                if (object == "0") return "null";

                std::string generic_address = next_ssa();
                body << "  " << generic_address << " = bitcast "
                     << get_struct_type_str(element_struct) << "* "
                     << object << " to i64*\n";
                return generic_address;
            }

            
            std::string arr_ptr;
            BType object_type = get_expr_type(idx->object.get());
            BType elem_type = is_array_type(object_type)
                ? get_array_elem_type(object_type)
                : get_pointer_base_type(object_type);
            if (elem_type == BType::UNKNOWN) elem_type = BType::INT;
            
            
            if (auto* var = dynamic_cast<const VariableExpr*>(idx->object.get())) {
                if (vars.count(var->name)) {
                    LLVMVar& v = vars[var->name];
                    
                    if (v.type == IRType::ARR) {
                        elem_type = v.elem_type;
                        if (elem_type == BType::UNKNOWN) {
                            elem_type = BType::INT;
                        }
                        IRType elem_ir_type = btype_to_ir(elem_type);
                        std::string ssa = next_ssa();
                        body << "  " << ssa << " = load " << llvm_type_str(elem_ir_type) << "*, " << llvm_type_str(elem_ir_type) << "** " << v.alloca << "\n";
                        arr_ptr = ssa;
                    } else {
                        
                        arr_ptr = emit_expression(idx->object.get());
                    }
                } else {
                    arr_ptr = emit_expression(idx->object.get());
                }
            } else {
                
                arr_ptr = emit_expression(idx->object.get());
            }
            
            
            std::string index_val = emit_expression(idx->index.get());
            
            
            IRType elem_ir_type = btype_to_ir(elem_type);
            std::string result = next_ssa();
            body << "  " << result << " = getelementptr inbounds " << llvm_type_str(elem_ir_type) << ", " << llvm_type_str(elem_ir_type) << "* " << arr_ptr 
                 << ", i64 " << index_val << "\n";
            
            return result;
        }
        
        return "0";
    }
    
    if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        std::string ptr_val = emit_expression(deref->operand.get());
        BType ptr_type = get_expr_type(deref->operand.get());

        BType base_type = pointer_pointee_type_from_expression(
            *this, deref->operand.get());
        if (base_type == BType::UNKNOWN) base_type = BType::INT;
        if (base_type == BType::STRUCT) {
            const std::string struct_name = pointer_pointee_struct_name(
                *this, deref->operand.get());
            if (struct_name.empty() || !structs.count(struct_name)) {
                gerror("Cannot resolve struct type behind this pointer :/\n");
                return "0";
            }

            const IRType pointer_ir_type = btype_to_ir(ptr_type);
            const std::string source_pointer_type =
                is_ir_pointer(pointer_ir_type)
                    ? llvm_ir_type_name(pointer_ir_type)
                    : "i64*";
            const std::string target_pointer_type =
                get_struct_type_str(struct_name) + "*";
            if (source_pointer_type == target_pointer_type) return ptr_val;

            std::string struct_pointer = next_ssa();
            body << "  " << struct_pointer << " = bitcast "
                 << source_pointer_type << " " << ptr_val
                 << " to " << target_pointer_type << "\n";
            return struct_pointer;
        }
        if (base_type == BType::FUNC || is_array_type(base_type)) {
            gerror("Cannot dereference this pointer type :/\n");
            return "0";
        }

        ptr_val = cast_pointer_to_pointee(
            *this, ptr_val, ptr_type, base_type, "  ");
        IRType base_ir_type = btype_to_ir(base_type);
        std::string ssa = next_ssa();
        body << "  " << ssa << " = load " << llvm_type_str(base_ir_type) 
             << ", " << llvm_ptr_type_str(base_ir_type) << " " << ptr_val << "\n";
        return ssa;
    }
    
     if (auto* sz = dynamic_cast<const SizeofExpr*>(expr)) {
         if (sz->expr) {
             
             if (auto* var = dynamic_cast<const VariableExpr*>(sz->expr.get())) {
                 if (vars.count(var->name)) {
                     LLVMVar& v = vars[var->name];
                     if (v.type == IRType::ARR) {
                         int elem_size = getTypeSize(v.elem_type);
                         if (v.array_size > 0) {
                             return std::to_string(elem_size * v.array_size);
                         }
                         
                         return std::to_string(sizeof(void*));
                     }
                 }
             }
             
             
             BType expr_type = get_expr_type(sz->expr.get());
             
             return std::to_string(getTypeSize(expr_type));
         }
        
        
        
        if (structs.count(sz->name)) {
            if (structs.at(sz->name).is_opaque) {
                gerror("Cannot use sizeof on opaque extern struct '" +
                       sz->name +
                       "'; declare an external size function instead :/\n");
                return "0";
            }
            const std::string struct_type = get_struct_type_str(sz->name);
            std::string end_pointer = next_ssa();
            body << "  " << end_pointer << " = getelementptr "
                 << struct_type << ", " << struct_type
                 << "* null, i32 1\n";
            std::string size = next_ssa();
            body << "  " << size << " = ptrtoint " << struct_type
                 << "* " << end_pointer << " to i64\n";
            return size;
        }

        
        if (sz->name == "int") return "8";
        if (sz->name == "f64") return "8";
        if (sz->name == "f32") return "4";
        if (sz->name == "bol") return "1";
        if (sz->name == "str") return std::to_string(sizeof(void*));
        if (sz->name == "ptr") return std::to_string(sizeof(void*));
        if (sz->name == "isize" || sz->name == "usize") {
            return std::to_string(sizeof(void*));
        }
        if (sz->name == "hex") return "8";
        
        if (sz->name == "i8") return "1";
        if (sz->name == "i16") return "2";
        if (sz->name == "i32") return "4";
        if (sz->name == "i64") return "8";
        
        if (sz->name == "u8") return "1";
        if (sz->name == "u16") return "2";
        if (sz->name == "u32") return "4";
        if (sz->name == "u64") return "8";

        if ((!sz->name.empty() && sz->name.back() == '*') ||
            (sz->name.size() >= 2 &&
             sz->name.substr(sz->name.size() - 2) == "[]")) {
            return std::to_string(sizeof(void*));
        }
        
        
        if (sz->name == "i8*") return "8";
        if (sz->name == "i16*") return "8";
        if (sz->name == "i32*") return "8";
        if (sz->name == "i64*") return "8";
        if (sz->name == "u8*") return "8";
        if (sz->name == "u16*") return "8";
        if (sz->name == "u32*") return "8";
        if (sz->name == "u64*") return "8";
        if (sz->name == "f32*") return "8";
        if (sz->name == "f64*") return "8";
        if (sz->name == "str*") return "8";
        
        
        if (sz->name == "int[]") return "8";
        if (sz->name == "f64[]") return "8";
        if (sz->name == "f32[]") return "8";
        if (sz->name == "bol[]") return "8";
        if (sz->name == "str[]") return "8";
        if (sz->name == "ptr[]") return "8";
        if (sz->name == "i8[]") return "8";
        if (sz->name == "i16[]") return "8";
        if (sz->name == "i32[]") return "8";
        if (sz->name == "i64[]") return "8";
        if (sz->name == "u8[]") return "8";
        if (sz->name == "u16[]") return "8";
        if (sz->name == "u32[]") return "8";
        if (sz->name == "u64[]") return "8";
        
        
        if (vars.count(sz->name)) {
            LLVMVar& v = vars[sz->name];
            if (v.type == IRType::ARR) {
                
                int elem_size = getTypeSize(v.elem_type);
                if (v.array_size > 0) {
                    return std::to_string(elem_size * v.array_size);
                }
                
                return "0";
            }
            return std::to_string(getTypeSize(ir_to_btype(v.type)));
        }
        
        
        if (global_vars.count(sz->name)) {
            IRType vtype = global_vars[sz->name];
            return std::to_string(getTypeSize(ir_to_btype(vtype)));
        }
        
        return "0";
    }
    
    return "0";
}

std::string LLVMEmitter::emit_lvalue(const Expr* expr) {
    if (!expr) return "0";
    
    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) {
        if (vars.count(var->name)) {
            LLVMVar& v = vars[var->name];
            
            
            if (v.type == IRType::STRUCT && v.struct_pointer_slot) {
                std::string struct_type = get_struct_type_str(v.struct_name);
                std::string ssa = next_ssa();
                body << "  " << ssa << " = load " << struct_type << "*, "
                     << struct_type << "** " << v.alloca << "\n";
                return ssa;
            }
            return v.alloca;
        }
        
        if (global_vars.count(var->name)) {
            return "@" + var->name;
        }
        return "0";
    }
    
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        
        std::string struct_name = get_expr_struct_name(member->object.get());
        if (struct_name.empty()) {
            struct_name = pointer_pointee_struct_name(
                *this, member->object.get());
        }
        std::string object_ptr;

        const std::string pointee_struct = pointer_pointee_struct_name(
            *this, member->object.get());
        BType object_type = get_expr_type(member->object.get());
        if (object_type == BType::UNKNOWN) {
            object_type = member->object->btype;
        }

        
        
        
        if (auto* index = dynamic_cast<const IndexExpr*>(
                member->object.get());
            index && struct_array_element_name(
                *this, index->object.get()) == struct_name) {
            std::string element_slot = emit_lvalue(index);
            if (element_slot == "0") return "0";

            const std::string struct_type = get_struct_type_str(struct_name);
            if (is_inline_struct_array_expression(
                    *this, index->object.get())) {
                object_ptr = element_slot;
            } else {
                object_ptr = next_ssa();
                body << "  " << object_ptr << " = load " << struct_type
                     << "*, " << struct_type << "** " << element_slot
                     << "\n";
            }
        } else if (!pointee_struct.empty() &&
                   pointee_struct == struct_name &&
                   is_pointer_like_btype(object_type)) {
            
            
            
            
            std::string raw_pointer = emit_expression(member->object.get());
            if (raw_pointer == "0") return "0";

            std::string source_type = get_llvm_type(object_type);
            const std::string target_type =
                get_struct_type_str(struct_name) + "*";

            
            
            if (auto* call = dynamic_cast<const CallExpr*>(
                    member->object.get())) {
                std::string callee;
                if (resolve_call_target(call, callee, false)) {
                    auto external = extern_functions.find(callee);
                    if (external != extern_functions.end() &&
                        external->second->return_type_ref.base ==
                            BType::STRUCT &&
                        external->second->return_type_ref.is_pointer) {
                        source_type = target_type;
                    }
                }
            }

            if (source_type == target_type) {
                object_ptr = raw_pointer;
            } else {
                object_ptr = next_ssa();
                body << "  " << object_ptr << " = bitcast " << source_type
                     << " " << raw_pointer << " to " << target_type << "\n";
            }
        } else {
            object_ptr = emit_lvalue(member->object.get());
        }

        auto struct_it = structs.find(struct_name);
        if (object_ptr == "0" || struct_it == structs.end()) {
            gerror("Field object is not a known struct :/\n");
            return "0";
        }

        const LLVMStructInfo& info = struct_it->second;
        auto field_it = info.field_indices.find(member->member);
        if (field_it == info.field_indices.end()) {
            gerror("Field '" + member->member +
                   "' was not found in struct '" + struct_name + "' :/\n");
            return "0";
        }

        std::string gep = next_ssa();
        body << "  " << gep << " = getelementptr inbounds " << get_struct_type_str(struct_name)
             << ", " << get_struct_type_str(struct_name) << "* " << object_ptr
             << ", i32 0, i32 " << field_it->second << "\n";
        return gep;
    }
    
    if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        const std::string tuple_name =
            get_expr_struct_name(idx->object.get());
        auto tuple = structs.find(tuple_name);
        if (tuple != structs.end() && tuple->second.is_tuple) {
            auto* number = dynamic_cast<const NumberExpr*>(idx->index.get());
            if (!number || number->is_float || number->value < 0 ||
                static_cast<size_t>(number->value) >= tuple->second.field_types.size()) {
                gerror("Tuple indices must be in-range integer literals :/\n");
                return "0";
            }
            std::string tuple_ptr = emit_lvalue(idx->object.get());
            if (tuple_ptr == "0") return "0";
            std::string field = next_ssa();
            const std::string tuple_type = get_struct_type_str(tuple_name);
            body << "  " << field << " = getelementptr inbounds "
                 << tuple_type << ", " << tuple_type << "* " << tuple_ptr
                 << ", i32 0, i32 " << static_cast<size_t>(number->value)
                 << "\n";
            return field;
        }

        if (struct_array_element_name(*this, idx->object.get()).empty() &&
            !get_expr_struct_name(idx->object.get()).empty()) {
            auto call = make_operator_call(
                "[]", idx->object.get(), idx->index.get());
            std::string callee;
            if (call && resolve_call_target(call.get(), callee, false) &&
                func_types.count(callee) &&
                func_types.at(callee) == IRType::STRUCT) {
                return emit_expression(call.get());
            }
            gerror("Overloaded operator '[]' returns a value and cannot be used as an l-value :/\n");
            return "0";
        }

        BType array_type = get_expr_type(idx->object.get());
        if (!is_array_type(array_type)) {
            gerror("Invalid index l-value in function '" +
                   current_function_name + "' (object " +
                   idx->object->node_type() + ") :/\n");
            return "0";
        }

        const std::string struct_element_name =
            struct_array_element_name(*this, idx->object.get());
        if (!struct_element_name.empty()) {
            std::string array_slot = emit_lvalue(idx->object.get());
            if (array_slot == "0") return "0";

            const bool inline_elements = is_inline_struct_array_expression(
                *this, idx->object.get());
            const std::string struct_type =
                get_struct_type_str(struct_element_name);
            const std::string element_type = inline_elements
                ? struct_type
                : struct_type + "*";
            std::string array_pointer = next_ssa();
            body << "  " << array_pointer << " = load " << element_type
                 << "*, " << element_type << "** " << array_slot << "\n";

            std::string index_value = emit_expression(idx->index.get());
            if (!normalize_integer_to_i64(
                    *this, idx->index.get(), index_value, "Struct-array index")) {
                return "0";
            }

            std::string element_slot = next_ssa();
            body << "  " << element_slot << " = getelementptr inbounds "
                 << element_type << ", " << element_type << "* "
                 << array_pointer << ", i64 " << index_value << "\n";
            return element_slot;
        }

        BType element_type = get_array_elem_type(array_type);
        if (element_type == BType::UNKNOWN) element_type = BType::INT;

        std::string array_ptr;
        if (dynamic_cast<const ArrayExpr*>(idx->object.get())) {
            
            array_ptr = emit_expression(idx->object.get());
        } else {
            
            
            std::string array_slot = emit_lvalue(idx->object.get());
            if (array_slot == "0") return "0";

            std::string loaded_ptr = next_ssa();
            body << "  " << loaded_ptr << " = load " << get_llvm_type(array_type)
                 << ", " << get_llvm_type(array_type) << "* " << array_slot << "\n";
            array_ptr = loaded_ptr;
        }

        std::string index_val = emit_expression(idx->index.get());
        if (!normalize_integer_to_i64(
                *this, idx->index.get(), index_val, "Array index")) {
            return "0";
        }
        std::string result = next_ssa();
        body << "  " << result << " = getelementptr inbounds " << get_llvm_type(element_type)
             << ", " << get_llvm_type(element_type) << "* " << array_ptr
             << ", i64 " << index_val << "\n";
        return result;
    }
    
    return emit_expression(expr);
}
