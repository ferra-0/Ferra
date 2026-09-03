#include "ast.h"
#include "template_instantiator.h"
#include "tokens.h"
#include "global.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>


class Parser {
public:
    struct AttributeUse {
        std::string name;
        std::vector<std::unique_ptr<Expr>> arguments;
    };

    struct AttributeDefinition {
        std::vector<ParamDecl> parameters;
        std::unique_ptr<Stmt> body;
    };

    std::vector<Token>& tokens;
    size_t pos = 0;
    std::vector<std::string> current_type_params;  
    std::unordered_set<std::string> struct_names;
    std::unordered_map<std::string, AttributeDefinition> attribute_definitions;
    std::vector<AttributeUse> pending_attributes;
    size_t decorator_counter = 0;
    std::unordered_set<size_t> deprecated_keyword_warning_positions;

    Parser(std::vector<Token>& t) : tokens(t) {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].type == TWORD && tokens[i].value == "stct" &&
                tokens[i + 1].type == TWORD) {
                struct_names.insert(tokens[i + 1].value);
            }
        }
    }

    bool is_at_end() { return pos >= tokens.size() || tokens[pos].type == TCODEEND; }
    Token peek() { return is_at_end() ? Token{TCODEEND, ""} : tokens[pos]; }
    Token previous() { return pos > 0 ? tokens[pos - 1] : Token{TCODEEND, ""}; }

    int token_line(size_t token_pos) const {
        if (tokens.empty()) return 1;
        return tokens[std::min(token_pos, tokens.size() - 1)].line;
    }

    void err(const std::string& t) {
        gerror_at(token_line(pos), t);
    }

    void warn_deprecated_keyword(size_t token_pos) {
        if (token_pos >= tokens.size() ||
            !deprecated_keyword_warning_positions.insert(token_pos).second) {
            return;
        }
        const std::string& value = tokens[token_pos].value;
        if (value == "fn") {
            gwarn_at(tokens[token_pos].line,
                     "fn is deprecated. use func\n");
        } else if (value == "let") {
            gwarn_at(tokens[token_pos].line,
                     "let is deprecated. use var\n");
        }
    }

    bool check(const std::string& v) {
        if (is_at_end() || tokens[pos].type == TSTRING) return false;
        const std::string& value = tokens[pos].value;
        const bool is_deprecated_alias =
            (v == "func" && value == "fn") ||
            (v == "var" && value == "let");
        if (is_deprecated_alias) warn_deprecated_keyword(pos);
        return value == v || is_deprecated_alias;
    }

    bool starts_attribute_on_new_line() const {
        return pos + 1 < tokens.size() &&
               tokens[pos].value == "#" &&
               tokens[pos].line_break_before &&
               tokens[pos + 1].type == TWORD;
    }

    bool match(const std::string& v) {
        if (check(v)) { pos++; return true; }
        return false;
    }

    bool is_type_close() { return check(">") || check(">>"); }
    bool match_type_close() {
        if (check(">")) {
            pos++;
            return true;
        }
        if (check(">>")) {
            tokens[pos].value = ">";
            return true;
        }
        return false;
    }
    Token advance() {
        if (!is_at_end()) return tokens[pos++];
        return Token{TCODEEND, ""};
    }

    // parseTypeRef
    TypeRef parse_type_ref() {
        TypeRef result;

        if (match("(")) {
            result.base = BType::TUPLE;
            while (!check(")") && !is_at_end()) {
                TypeRef element = parse_type_ref();
                if (element.base == BType::UNKNOWN && element.name.empty()) {
                    err("Expected tuple element type :/\n");
                    break;
                }
                result.type_args.push_back(std::move(element));
                if (!match(",")) break;
            }
            if (result.type_args.empty()) {
                err("A tuple type needs at least one element :/\n");
            }
            if (!match(")")) {
                err("Expected ')' after tuple type :/\n");
            }
        } else if (check("tup")) { pos++; result.base = BType::TUPLE; }
        else if (check("int")) { pos++; result.base = BType::INT; }
        else if (check("f64")) { pos++; result.base = BType::F64; }
        else if (check("bol")) { pos++; result.base = BType::BOOL; }
        else if (check("str")) { pos++; result.base = BType::STR; }
        else if (check("ptr")) { pos++; result.base = BType::PTR; }
        else if (check("arr")) { pos++; result.base = BType::ARR; }
        else if (check("obj")) { pos++; result.base = BType::OBJ; }
        else if (check("nul")) { pos++; result.base = BType::VOID; }
        else if (check("func"))  {
            pos++;
            result.base = BType::FUNC;
            if (match("(")) {
                while (!check(")") && !is_at_end()) {
                    TypeRef parameter = parse_parameter_type_ref();
                    if (parameter.base == BType::UNKNOWN &&
                        parameter.name.empty()) {
                        err("Expected function parameter type :/\n");
                        break;
                    }
                    result.function_params.push_back(std::move(parameter));
                    if (!match(",")) break;
                }
                if (!match(")")) {
                    err("Expected ')' after function parameter types :/\n");
                }
                if (!match(":")) {
                    err("Expected ':' and return type after function type :/\n");
                } else {
                    result.function_return =
                        std::make_shared<TypeRef>(parse_type_ref());
                    if (result.function_return->base == BType::UNKNOWN &&
                        result.function_return->name.empty()) {
                        err("Expected function return type :/\n");
                    }
                }
            }
        }
        else if (check("i1")) { pos++; result.base = BType::BOOL; }
        else if (check("i8")) { pos++; result.base = BType::I8; }
        else if (check("i16")) { pos++; result.base = BType::I16; }
        else if (check("i32")) { pos++; result.base = BType::I32; }
        else if (check("i64")) { pos++; result.base = BType::I64; }
        else if (check("u8")) { pos++; result.base = BType::U8; }
        else if (check("u16")) { pos++; result.base = BType::U16; }
        else if (check("u32")) { pos++; result.base = BType::U32; }
        else if (check("u64")) { pos++; result.base = BType::U64; }
        else if (check("isize")) { pos++; result.base = BType::ISIZE; }
        else if (check("usize")) { pos++; result.base = BType::USIZE; }
        else if (check("hex")) { pos++; result.base = BType::HEX; }
        else if (check("f32")) { pos++; result.base = BType::F32; }
        else if (tokens[pos].type == TWORD) {
            result.name = tokens[pos].value;
            result.base = is_type_param(result.name, current_type_params)
                ? BType::UNKNOWN
                : BType::STRUCT;
            pos++;

            if (result.base == BType::STRUCT && match("<")) {
                while (!is_type_close() && !is_at_end()) {
                    if (check(",")) { pos++; continue; }
                    size_t arg_start = pos;
                    result.type_args.push_back(parse_type_ref());
                    if (pos == arg_start) {
                        err("Expected type argument :/\n");
                        break;
                    }
                }
                if (!match_type_close()) {
                    err("Expected '>' after type arguments :/\n");
                }
            }
        } else {
            return result;
        }

        if (match("*")) {
            result.is_pointer = true;
        }

        if (match("[")) {
            if (!match("]")) {
                err("Expected ']' in array type :/\n");
            } else {
                result.is_array = true;
            }
        }

        if (match("!")) {
            result.is_const = true;
        }

        return result;
    }

    // parseParamRef
    TypeRef parse_parameter_type_ref() {
        TypeRef result = parse_type_ref();
        if (!result.is_const) return result;

        const BType parameter_type = type_ref_to_btype(result);
        if (result.base == BType::VOID ||
            result.base == BType::FUNC || result.base == BType::PTR ||
            result.base == BType::OBJ || result.is_pointer ||
            is_pointer_type(parameter_type)) {
            err("The by-value marker '!' requires a value, tuple, generic, or array parameter :/\n");
            return result;
        }

        result.pass_by_value = true;
        result.is_const = false;
        return result;
    }

    // parseType
    BType parse_type() {
        return type_ref_to_btype(parse_type_ref());
    }

    // readNoteRef
    TypeRef try_read_type_annotation_ref() {
        if (check(":") && pos + 1 < tokens.size() &&
            (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
             tokens[pos + 1].value == "(")) {
            pos++; 
            return parse_type_ref();
        }
        return {};
    }

    // readNoteRefAndName
    std::pair<BType, std::string> try_read_type_annotation_with_name() {
        TypeRef type_ref = try_read_type_annotation_ref();
        return {type_ref_to_btype(type_ref), type_ref.name};
    }

    // readTypeNote
    BType try_read_type_annotation() {
        return type_ref_to_btype(try_read_type_annotation_ref());
    }

    // parseStructDec
    std::unique_ptr<StructDecl> parse_struct_decl(bool is_extern = false) {
        const size_t declaration_start = pos;
        pos++; 

        auto decl = std::make_unique<StructDecl>();
        decl->line = token_line(declaration_start);
        decl->is_extern = is_extern;

        if (tokens[pos].type != TWORD) {
            err("Expected struct name :/\n");
            return decl;
        }
        decl->name = tokens[pos].value;
        pos++;

        if (check("<")) {
            decl->type_params = parse_type_params();
        }

        if (is_extern) {
            if (!decl->type_params.empty()) {
                err("Extern structs cannot have type parameters :/\n");
            }
            if (check(";")) pos++;
            return decl;
        }

        auto saved_type_params = current_type_params;
        current_type_params = decl->type_params;

        if (!match("{")) {
            err("Expected '{' after struct name :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        while (!check("}") && !is_at_end()) {
            
            if (check(",")) { pos++; continue; }
            
            
            if (check(";")) { pos++; continue; }

            if (tokens[pos].type != TWORD) {
                err("Expected field name :/\n");
                break;
            }
            StructField field;
            field.name = tokens[pos].value;
            pos++;

            if (check(":") && pos + 1 < tokens.size() &&
                (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
                 tokens[pos + 1].value == "(")) {
                pos++; 
                field.type_ref = parse_type_ref();
                field.type = type_ref_to_btype(field.type_ref);
                field.type_annotation = type_ref_to_string(field.type_ref);
                if (field.type_ref.base == BType::STRUCT) {
                    field.struct_name = field.type_ref.name;
                }
            } else {
                err("Expected ': type' after field name :/\n");
                break;
            }

            decl->fields.push_back(field);

            
            if (check(",")) { pos++; }
        }

        
        if (check(",")) { pos++; }
        
        if (!match("}")) {
            err("Expected '}' after struct fields :/\n");
        }

        current_type_params = saved_type_params;

        return decl;
    }

    // parseDropDecl
    std::unique_ptr<FnDecl> parse_drop_decl() {
        const size_t declaration_start = pos;
        pos++; 

        auto decl = std::make_unique<FnDecl>();
        decl->line = token_line(declaration_start);

        decl->is_drop = true;
        decl->is_method = true;
        decl->return_type = BType::VOID;
        decl->return_type_ref.base = BType::VOID;
        decl->return_type_annotation = "nul";

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected struct name after 'drop' :/\n");
            return decl;
        }

        TypeRef owner_type;
        owner_type.base = BType::STRUCT;
        owner_type.name = tokens[pos].value;

        decl->method_owner = owner_type.name;
        pos++;

        if (check("<")) {
            decl->type_params = parse_type_params();

            for (const std::string& type_param : decl->type_params) {
                TypeRef argument;
                argument.base = BType::UNKNOWN;
                argument.name = type_param;

                owner_type.type_args.push_back(argument);
            }
        }

        auto saved_type_params = current_type_params;
        current_type_params = decl->type_params;

        if (!match("(")) {
            err("Expected '(' after drop type :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        if (!match(")")) {
            err("Drop declaration takes no parameters :/\n");

            while (!check(")") && !is_at_end()) {
                pos++;
            }

            match(")");
        }

        decl->method_name = "__drop__";
        decl->name = mangle_method_name(
            decl->method_owner,
            decl->method_name
        );

        ParamDecl this_param;
        this_param.name = "this";
        this_param.type = BType::STRUCT;
        this_param.type_ref = owner_type;
        this_param.type_annotation = type_ref_to_string(owner_type);
        this_param.struct_name = owner_type.name;

        decl->params.push_back(std::move(this_param));

        decl->body = parse_block();

        current_type_params = saved_type_params;
        return decl;
    }

    // attrUseP
    AttributeUse parse_attribute_use() {
        AttributeUse attribute;

        if (!match("#")) {
            err("Expected '#' before attribute :/\n");
            return attribute;
        }

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected attribute name after '#' :/\n");
            return attribute;
        }

        attribute.name = tokens[pos].value;
        pos++;

        if (!match("(")) return attribute;

        while (!check(")") && !is_at_end()) {
            if (match(",")) continue;
            size_t argument_start = pos;
            attribute.arguments.push_back(parse_expression());
            if (pos == argument_start) {
                err("Expected decorator argument :/\n");
                pos++;
            }
        }

        if (!match(")")) {
            err("Expected ')' after attribute arguments :/\n");
        }
        return attribute;
    }

    // parseAttrD
    void parse_attribute_declaration() {
        match("#");
        match("func");

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected decorator name after '#func' :/\n");
            return;
        }

        const std::string name = tokens[pos].value;
        pos++;

        AttributeDefinition definition;
        if (!match("(")) {
            err("Expected '(' after decorator name :/\n");
            return;
        }

        while (!check(")") && !is_at_end()) {
            if (match(",")) continue;
            if (tokens[pos].type != TWORD) {
                err("Expected decorator parameter name :/\n");
                pos++;
                continue;
            }

            ParamDecl parameter;
            parameter.name = tokens[pos].value;
            pos++;

            if (match(":")) {
                parameter.type_ref = parse_parameter_type_ref();
                parameter.type = type_ref_to_btype(parameter.type_ref);
                parameter.type_annotation = type_ref_to_string(parameter.type_ref);
                if (parameter.type_ref.base == BType::STRUCT) {
                    parameter.struct_name = parameter.type_ref.name;
                }
            }
            definition.parameters.push_back(std::move(parameter));
        }

        if (!match(")")) {
            err("Expected ')' after decorator parameters :/\n");
            return;
        }

        if (match(":")) parse_type_ref();
        definition.body = parse_block();

        if (name == "inline" || name == "noinline" || name == "func") {
            err("Cannot redefine reserved attribute '#" + name + "' :/\n");
            return;
        }
        if (attribute_definitions.count(name)) {
            err("Decorator '#" + name + "' is already defined :/\n");
            return;
        }
        if (definition.parameters.empty() ||
            definition.parameters.front().type != BType::FUNC) {
            err("Decorator '#" + name +
                   "' must take a function as its first parameter :/\n");
            return;
        }

        attribute_definitions.emplace(name, std::move(definition));
    }

    // remakeAttrExpr
    void rewrite_decorator_expr(
        std::unique_ptr<Expr>& expr,
        const std::string& callable_parameter,
        const std::string& target_name,
        const std::vector<ParamDecl>& target_parameters,
        const std::unordered_map<std::string, const Expr*>& substitutions
    ) {
        if (!expr) return;

        if (auto* variable = dynamic_cast<VariableExpr*>(expr.get())) {
            auto substitution = substitutions.find(variable->name);
            if (substitution != substitutions.end() && substitution->second) {
                expr = clone_expression(*substitution->second);
            }
            return;
        }

        if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
            for (auto& argument : call->args) {
                rewrite_decorator_expr(argument, callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
            if (!call->is_method_call && call->callee == callable_parameter) {
                call->callee = target_name;
                call->template_args.clear();
                if (call->args.empty()) {
                    for (const ParamDecl& parameter : target_parameters) {
                        auto forwarded = std::make_unique<VariableExpr>();
                        forwarded->name = parameter.name;
                        forwarded->btype = parameter.type;
                        call->args.push_back(std::move(forwarded));
                    }
                }
            }
            return;
        }

        if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
            rewrite_decorator_expr(binary->left, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(binary->right, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
            rewrite_decorator_expr(unary->operand, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
            rewrite_decorator_expr(ternary->cond, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(ternary->then_expr, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(ternary->else_expr, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* array = dynamic_cast<ArrayExpr*>(expr.get())) {
            for (auto& element : array->elements) {
                rewrite_decorator_expr(element, callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
        } else if (auto* literal = dynamic_cast<StructLiteralExpr*>(expr.get())) {
            for (auto& field : literal->fields) {
                rewrite_decorator_expr(field.value, callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
        } else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
            rewrite_decorator_expr(index->object, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(index->index, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
            rewrite_decorator_expr(member->object, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* size = dynamic_cast<SizeofExpr*>(expr.get())) {
            rewrite_decorator_expr(size->expr, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* ref = dynamic_cast<RefExpr*>(expr.get())) {
            rewrite_decorator_expr(ref->operand, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* deref = dynamic_cast<DerefExpr*>(expr.get())) {
            rewrite_decorator_expr(deref->operand, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* cast = dynamic_cast<AsExpr*>(expr.get())) {
            rewrite_decorator_expr(cast->operand, callable_parameter, target_name,
                                   target_parameters, substitutions);
        }
    }

    // remakeAttrStmt
    void rewrite_decorator_stmt(
        Stmt* stmt,
        const std::string& callable_parameter,
        const std::string& target_name,
        const std::vector<ParamDecl>& target_parameters,
        const std::unordered_map<std::string, const Expr*>& substitutions
    ) {
        if (!stmt) return;

        if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
            for (auto& child : block->statements) {
                rewrite_decorator_stmt(child.get(), callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
        } else if (auto* variable = dynamic_cast<VarDeclStmt*>(stmt)) {
            rewrite_decorator_expr(variable->initializer, callable_parameter, target_name,
                                   target_parameters, substitutions);
            for (auto& argument : variable->constructor_args) {
                rewrite_decorator_expr(argument, callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
            rewrite_decorator_expr(variable->array_size, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
            rewrite_decorator_expr(assign->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* assign = dynamic_cast<ArrayAssignStmt*>(stmt)) {
            rewrite_decorator_expr(assign->index, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(assign->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* assign = dynamic_cast<MemberAssignStmt*>(stmt)) {
            rewrite_decorator_expr(assign->lhs, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(assign->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* assign = dynamic_cast<DerefAssignStmt*>(stmt)) {
            rewrite_decorator_expr(assign->pointer, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_expr(assign->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* branch = dynamic_cast<IfStmt*>(stmt)) {
            rewrite_decorator_expr(branch->condition, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_stmt(branch->then_branch.get(), callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_stmt(branch->else_branch.get(), callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* loop = dynamic_cast<ForStmt*>(stmt)) {
            rewrite_decorator_expr(loop->bound, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_stmt(loop->body.get(), callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* loop = dynamic_cast<ForWhileStmt*>(stmt)) {
            rewrite_decorator_expr(loop->condition, callable_parameter, target_name,
                                   target_parameters, substitutions);
            rewrite_decorator_stmt(loop->body.get(), callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
            rewrite_decorator_expr(return_stmt->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* drop_now = dynamic_cast<DropNowStmt*>(stmt)) {
            rewrite_decorator_expr(drop_now->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
        } else if (auto* match_stmt = dynamic_cast<MatchStmt*>(stmt)) {
            rewrite_decorator_expr(match_stmt->value, callable_parameter, target_name,
                                   target_parameters, substitutions);
            for (auto& match_case : match_stmt->cases) {
                rewrite_decorator_expr(match_case.first, callable_parameter, target_name,
                                       target_parameters, substitutions);
                rewrite_decorator_stmt(match_case.second.get(), callable_parameter, target_name,
                                       target_parameters, substitutions);
            }
            rewrite_decorator_stmt(match_stmt->default_case.get(), callable_parameter,
                                   target_name, target_parameters, substitutions);
        } else if (auto* expression = dynamic_cast<ExprStmt*>(stmt)) {
            rewrite_decorator_expr(expression->expression, callable_parameter, target_name,
                                   target_parameters, substitutions);
        }
    }

    // wrapFunc
    std::unique_ptr<FnDecl> make_wrapper_shell(
        const FnDecl& source,
        const std::string& name,
        bool preserve_public_role
    ) {
        auto wrapper = std::make_unique<FnDecl>();
        wrapper->line = source.line;
        wrapper->name = name;
        wrapper->type_params = source.type_params;
        wrapper->params = source.params;
        wrapper->return_type = source.return_type;
        wrapper->return_type_ref = source.return_type_ref;
        wrapper->return_type_annotation = source.return_type_annotation;
        if (preserve_public_role) {
            wrapper->is_method = source.is_method;
            wrapper->is_drop = source.is_drop;
            wrapper->method_owner = source.method_owner;
            wrapper->method_name = source.method_name;
            wrapper->is_operator = source.is_operator;
            wrapper->operator_symbol = source.operator_symbol;
        }
        return wrapper;
    }

    // addAttr
    void apply_builtin_attribute(FnDecl& fn, const AttributeUse& attribute) {
        if (!attribute.arguments.empty()) {
            err("Built-in attribute '#" + attribute.name +
                   "' does not take arguments :/\n");
        }
        if (attribute.name == "inline") {
            if (fn.force_noinline) {
                err("Function '" + fn.name +
                       "' cannot have both #inline and #noinline :/\n");
            }
            fn.force_inline = true;
            fn.force_noinline = false;
        } else {
            if (fn.force_inline) {
                err("Function '" + fn.name +
                       "' cannot have both #inline and #noinline :/\n");
            }
            fn.force_noinline = true;
            fn.force_inline = false;
        }
    }

    // addWaitAttrs
    std::vector<std::unique_ptr<FnDecl>> apply_pending_attributes(
        std::unique_ptr<FnDecl> fn
    ) {
        std::vector<const AttributeUse*> decorators;
        for (const AttributeUse& attribute : pending_attributes) {
            if (attribute.name == "inline" || attribute.name == "noinline") continue;

            auto definition = attribute_definitions.find(attribute.name);
            if (definition == attribute_definitions.end()) {
                err("Unknown decorator '#" + attribute.name + "' :/\n");
                continue;
            }

            const size_t expected_arguments = definition->second.parameters.size() - 1;
            if (attribute.arguments.size() != expected_arguments) {
                err("Decorator '#" + attribute.name + "' expects " +
                       std::to_string(expected_arguments) + " argument(s), got " +
                       std::to_string(attribute.arguments.size()) + " :/\n");
                continue;
            }
            decorators.push_back(&attribute);
        }

        std::vector<std::unique_ptr<FnDecl>> result;
        FnDecl* public_function = nullptr;

        if (decorators.empty()) {
            public_function = fn.get();
            result.push_back(std::move(fn));
        } else {
            const std::string public_name = fn->name;
            auto public_signature = make_wrapper_shell(*fn, public_name, true);

            fn->name = "__decorated__" + public_name + "__" +
                       std::to_string(decorator_counter++);
            fn->is_method = false;
            fn->is_drop = false;
            fn->method_owner.clear();
            fn->method_name.clear();
            fn->is_operator = false;
            fn->operator_symbol.clear();

            std::string target_name = fn->name;
            result.push_back(std::move(fn));

            for (size_t index = decorators.size(); index-- > 0;) {
                const AttributeUse& decorator = *decorators[index];
                const AttributeDefinition& definition =
                    attribute_definitions.at(decorator.name);
                const bool is_public_wrapper = index == 0;
                const std::string wrapper_name = is_public_wrapper
                    ? public_name
                    : "__decorated__" + public_name + "__" +
                      std::to_string(decorator_counter++);

                auto wrapper = make_wrapper_shell(
                    *public_signature, wrapper_name, is_public_wrapper);
                wrapper->body = clone_statement(*definition.body);

                std::unordered_map<std::string, const Expr*> substitutions;
                for (size_t parameter_index = 1;
                     parameter_index < definition.parameters.size();
                     ++parameter_index) {
                    substitutions[definition.parameters[parameter_index].name] =
                        decorator.arguments[parameter_index - 1].get();
                }

                rewrite_decorator_stmt(
                    wrapper->body.get(),
                    definition.parameters.front().name,
                    target_name,
                    wrapper->params,
                    substitutions);

                target_name = wrapper->name;
                public_function = wrapper.get();
                result.push_back(std::move(wrapper));
            }
        }

        for (const AttributeUse& attribute : pending_attributes) {
            if (attribute.name == "inline" || attribute.name == "noinline") {
                apply_builtin_attribute(*public_function, attribute);
            }
        }
        pending_attributes.clear();
        return result;
    }

    // fuckAttrs
    void reject_pending_attributes(const std::string& target) {
        if (pending_attributes.empty()) return;
        err("Function attributes cannot be applied to " + target + " :/\n");
        pending_attributes.clear();
    }

    // parseMain
    std::unique_ptr<Program> parse_program() {
        auto prog = std::make_unique<Program>();

        while (!is_at_end()) {
            if (check("#")) {
                if (check_next("func")) {
                    reject_pending_attributes("a custom attribute declaration");
                    parse_attribute_declaration();
                } else {
                    pending_attributes.push_back(parse_attribute_use());
                }
            } else if (check("stct")) {
                reject_pending_attributes("a struct declaration");
                prog->statements.push_back(parse_struct_decl());
            } else if (check("extern")) {
                reject_pending_attributes("an extern declaration");
                pos++;
                if (check("stct")) {
                    prog->statements.push_back(parse_struct_decl(true));
                } else if (check("func")) {
                    prog->functions.push_back(parse_fn_decl(true));
                } else {
                    err("Expected 'stct' or 'func' after 'extern' :/\n");
                    if (!is_at_end()) pos++;
                }
            } else if (check("drop")) {
                auto functions = apply_pending_attributes(parse_drop_decl());
                for (auto& fn : functions) {
                    prog->functions.push_back(std::move(fn));
                }
            } else if (check("oper")) {
                auto functions = apply_pending_attributes(parse_oper_decl());
                for (auto& fn : functions) {
                    prog->functions.push_back(std::move(fn));
                }
            } else if (check("impl")) {
                auto functions = apply_pending_attributes(parse_impl_decl());
                for (auto& fn : functions) {
                    prog->functions.push_back(std::move(fn));
                }
            } else if (check("func")) {
                auto functions = apply_pending_attributes(parse_fn_decl());
                for (auto& fn : functions) {
                    prog->functions.push_back(std::move(fn));
                }
            } else if (check("take") || check("ftake") || check("plugin")) {
                reject_pending_attributes("a plugin declaration");
                prog->statements.push_back(parse_plugin_load());
            } else {
                reject_pending_attributes("a statement");
                std::unique_ptr<Stmt> statement = parse_statement();
                
                
                if (auto* group = dynamic_cast<BlockStmt*>(statement.get());
                    group && group->is_declaration_group) {
                    for (auto& declaration : group->statements) {
                        prog->statements.push_back(std::move(declaration));
                    }
                } else {
                    prog->statements.push_back(std::move(statement));
                }
            }
        }

        if (!pending_attributes.empty()) {
            err("Expected function declaration after attribute :/\n");
            pending_attributes.clear();
        }

        
        bool has_main = false;
        for (const auto& fn : prog->functions) {
            if (fn->name == "main" && !fn->is_extern) {
                has_main = true;
                if (!fn->params.empty()) {
                    err("func main() does not take source parameters; use _argc and _args for runtime arguments :/\n");
                }
                break;
            }
        }
        if (!has_main) {
            err("Missing required 'func main()' function :/\n");
        }

        return prog;
    }

    // parseTypeParams
    std::vector<std::string> parse_type_params() {
        std::vector<std::string> params;
        if (!check("<")) return params;
        
        pos++; 
        
        while (!is_type_close() && !is_at_end()) {
            if (check(",")) { pos++; continue; }
            if (tokens[pos].type == TWORD) {
                params.push_back(tokens[pos].value);
                pos++;
            } else {
                err("Expected type parameter name :/\n");
                break;
            }
        }
        
        if (!match_type_close()) {
            err("Expected '>' after type parameters :/\n");
        }
        
        return params;
    }

    std::unique_ptr<FnDecl> parse_fn_decl(bool is_extern = false) {
        const size_t declaration_start = pos;
        pos++; 

        auto decl = std::make_unique<FnDecl>();
        decl->line = token_line(declaration_start);
        decl->is_extern = is_extern;
        if (is_extern) {
            decl->return_type = BType::VOID;
            decl->return_type_ref.base = BType::VOID;
            decl->return_type_annotation = "nul";
        }

        if (tokens[pos].type != TWORD) {
            err("Expected function name :/\n");
            return decl;
        }
        decl->name = tokens[pos].value;
        pos++;

        
        if (check("<")) {
            decl->type_params = parse_type_params();
            if (is_extern) {
                err("Extern functions cannot have type parameters :/\n");
            }
        }

        
        auto saved_type_params = current_type_params;
        current_type_params = decl->type_params;

        if (!match("(")) {
            err("Expected '(' after function name :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        size_t external_parameter_index = 0;
        bool saw_default_parameter = false;
        while (!check(")") && !is_at_end()) {
            if (check(",")) { pos++; continue; }

            if (check("...")) {
                if (!is_extern) {
                    err("Only extern functions can be variadic :/\n");
                } else {
                    decl->is_variadic = true;
                }
                pos++;
                if (!check(")")) {
                    err("Variadic marker '...' must be the last parameter :/\n");
                    while (!check(")") && !is_at_end()) pos++;
                }
                break;
            }

            if (tokens[pos].type != TWORD) {
                err("Expected parameter name :/\n");
                current_type_params = saved_type_params;
                return decl;
            }
            TypeRef param_type_ref;
            std::string param_name;
            if (is_extern && !check_next(":")) {
                param_name = "arg" + std::to_string(external_parameter_index);
                param_type_ref = parse_parameter_type_ref();
            } else {
                param_name = tokens[pos].value;
                pos++;
                if (check(":") && pos + 1 < tokens.size() &&
                    (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
                     tokens[pos + 1].value == "(")) {
                    pos++;
                    param_type_ref = parse_parameter_type_ref();
                } else if (is_extern) {
                    err("Expected parameter type in extern function :/\n");
                }
            }
            ParamDecl pdecl;
            pdecl.name = param_name;
            pdecl.type_ref = param_type_ref;
            pdecl.type = type_ref_to_btype(param_type_ref);
            if (param_type_ref.base != BType::UNKNOWN || !param_type_ref.name.empty() ||
                param_type_ref.is_pointer || param_type_ref.is_array) {
                pdecl.type_annotation = type_ref_to_string(param_type_ref);
            }
            if (param_type_ref.base == BType::STRUCT) {
                pdecl.struct_name = param_type_ref.name;
            }
            if (match("=")) {
                pdecl.default_value = std::shared_ptr<Expr>(parse_expression());
                saw_default_parameter = true;
            } else if (saw_default_parameter) {
                err("Required parameter '" + pdecl.name +
                       "' cannot follow an optional parameter :/\n");
            }
            decl->params.push_back(std::move(pdecl));
            external_parameter_index++;
        }
        if (!match(")")) {
            err("Expected ')' after function parameters :/\n");
        }

        
        if (check(":") && pos + 1 < tokens.size() &&
            (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
             tokens[pos + 1].value == "(")) {
            pos++; 
            decl->return_type_ref = parse_type_ref();
            decl->return_type = type_ref_to_btype(decl->return_type_ref);
            decl->return_type_annotation = type_ref_to_string(decl->return_type_ref);
        }

        if (is_extern) {
            if (check(";")) pos++;
        } else {
            decl->body = parse_block();
        }

        
        current_type_params = saved_type_params;

        return decl;
    }

    std::unique_ptr<FnDecl> parse_impl_decl() {
        const size_t declaration_start = pos;
        pos++; 

        auto decl = std::make_unique<FnDecl>();
        decl->line = token_line(declaration_start);
        decl->is_method = true;

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected struct name after 'impl' :/\n");
            return decl;
        }

        TypeRef owner_type;
        owner_type.base = BType::STRUCT;
        owner_type.name = tokens[pos].value;
        decl->method_owner = owner_type.name;
        pos++;

        
        
        if (check("<")) {
            decl->type_params = parse_type_params();
            for (const auto& type_param : decl->type_params) {
                TypeRef arg;
                arg.base = BType::UNKNOWN;
                arg.name = type_param;
                owner_type.type_args.push_back(arg);
            }
        }

        auto saved_type_params = current_type_params;
        current_type_params = decl->type_params;

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected method name after impl type :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        decl->method_name = tokens[pos].value;
        decl->name = mangle_method_name(decl->method_owner, decl->method_name);
        pos++;

        if (!match("(")) {
            err("Expected '(' after method name :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        ParamDecl this_param;
        this_param.name = "this";
        this_param.type = BType::STRUCT;
        this_param.type_ref = owner_type;
        this_param.type_annotation = type_ref_to_string(owner_type);
        this_param.struct_name = owner_type.name;
        decl->params.push_back(std::move(this_param));

        bool saw_default_parameter = false;
        while (!check(")") && !is_at_end()) {
            if (check(",")) { pos++; continue; }

            if (tokens[pos].type != TWORD) {
                err("Expected parameter name :/\n");
                current_type_params = saved_type_params;
                return decl;
            }

            ParamDecl param;
            param.name = tokens[pos].value;
            pos++;

            if (check(":") && pos + 1 < tokens.size() &&
                (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
                 tokens[pos + 1].value == "(")) {
                pos++; 
                param.type_ref = parse_parameter_type_ref();
                param.type = type_ref_to_btype(param.type_ref);
                param.type_annotation = type_ref_to_string(param.type_ref);
                if (param.type_ref.base == BType::STRUCT) {
                    param.struct_name = param.type_ref.name;
                }
            }
            if (match("=")) {
                param.default_value = std::shared_ptr<Expr>(parse_expression());
                saw_default_parameter = true;
            } else if (saw_default_parameter) {
                err("Required parameter '" + param.name +
                       "' cannot follow an optional parameter :/\n");
            }
            decl->params.push_back(std::move(param));
        }

        if (!match(")")) {
            err("Expected ')' after method parameters :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        if (check(":") && pos + 1 < tokens.size() &&
            (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
             tokens[pos + 1].value == "(")) {
            pos++; 
            decl->return_type_ref = parse_type_ref();
            decl->return_type = type_ref_to_btype(decl->return_type_ref);
            decl->return_type_annotation = type_ref_to_string(decl->return_type_ref);
        }

        
        
        if (decl->method_name == decl->method_owner &&
            decl->return_type == BType::UNKNOWN) {
            decl->return_type = BType::VOID;
            decl->return_type_ref.base = BType::VOID;
            decl->return_type_annotation = "nul";
        }

        decl->body = parse_block();
        current_type_params = saved_type_params;
        return decl;
    }

    std::unique_ptr<FnDecl> parse_oper_decl() {
        const size_t declaration_start = pos;
        pos++; 

        auto decl = std::make_unique<FnDecl>();
        decl->line = token_line(declaration_start);
        decl->is_method = true;
        decl->is_operator = true;

        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected struct name after 'oper' :/\n");
            return decl;
        }

        TypeRef owner_type;
        owner_type.base = BType::STRUCT;
        owner_type.name = tokens[pos].value;
        decl->method_owner = owner_type.name;
        pos++;

        
        
        
        bool has_owner_type_params = false;
        if (check("<")) {
            size_t lookahead = pos + 1;
            while (lookahead < tokens.size() &&
                   tokens[lookahead].value != ">" &&
                   tokens[lookahead].value != ">>") {
                lookahead++;
            }
            if (lookahead + 1 < tokens.size()) {
                const std::string& after = tokens[lookahead + 1].value;
                has_owner_type_params = after == "<" || after == "<<";
            }
        }

        if (has_owner_type_params) {
            decl->type_params = parse_type_params();
            for (const auto& type_param : decl->type_params) {
                TypeRef arg;
                arg.base = BType::UNKNOWN;
                arg.name = type_param;
                owner_type.type_args.push_back(arg);
            }
        }

        auto saved_type_params = current_type_params;
        current_type_params = decl->type_params;

        
        
        
        if (check("<<")) {
            tokens[pos].value = "<";
        } else if (!match("<")) {
            err("Expected '<' before operator pattern :/\n");
            current_type_params = saved_type_params;
            return decl;
        }

        ParamDecl this_param;
        this_param.name = "this";
        this_param.type = BType::STRUCT;
        this_param.type_ref = owner_type;
        this_param.type_annotation = type_ref_to_string(owner_type);
        this_param.struct_name = owner_type.name;
        decl->params.push_back(std::move(this_param));

        auto parse_operand = [&]() -> bool {
            if (is_at_end() || tokens[pos].type != TWORD) {
                err("Expected operator operand name :/\n");
                return false;
            }

            ParamDecl operand;
            operand.name = tokens[pos].value;
            if (operand.name == "this") {
                err("Operator operand cannot be named 'this' :/\n");
            }
            pos++;

            if (check(":") && pos + 1 < tokens.size() &&
                (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
                 tokens[pos + 1].value == "(")) {
                pos++; 
                operand.type_ref = parse_parameter_type_ref();
                operand.type = type_ref_to_btype(operand.type_ref);
                operand.type_annotation = type_ref_to_string(operand.type_ref);
                if (operand.type_ref.base == BType::STRUCT) {
                    operand.struct_name = operand.type_ref.name;
                }
            } else {
                err("Expected ': type' after operator operand :/\n");
            }

            decl->params.push_back(std::move(operand));
            return true;
        };

        if (match("[")) {
            
            decl->operator_symbol = "[]";
            parse_operand();
            if (!match("]")) {
                err("Expected ']' after index operator operand :/\n");
            }
        } else {
            if (is_at_end()) {
                err("Expected operator symbol :/\n");
                current_type_params = saved_type_params;
                return decl;
            }

            decl->operator_symbol = tokens[pos].value;
            pos++;

            
            
            std::string operand_close;
            if (match("[")) operand_close = "]";
            else if (match("(")) operand_close = ")";

            if (!check(">") && !is_type_close()) {
                parse_operand();
            }

            if (!operand_close.empty() && !match(operand_close)) {
                err("Expected '" + operand_close + "' after operator operand :/\n");
            }
        }

        static const std::vector<std::string> supported = {
            "[]", "+", "-", "*", "/", "%", "<<", ">>",
            "<", ">", "<=", ">=", "is", "not", "==", "!=",
            "&", "|", "#", "~", "and", "or", "!", ":",
            "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "#="
        };
        if (std::find(supported.begin(), supported.end(), decl->operator_symbol) == supported.end()) {
            err("Unsupported operator '" + decl->operator_symbol + "' :/\n");
        }

        const size_t operand_count = decl->params.size() - 1;
        if (decl->operator_symbol == "[]" && operand_count != 1) {
            err("Index operator requires exactly one operand :/\n");
        } else if ((decl->operator_symbol == "!" ||
                    decl->operator_symbol == "~" ||
                    (decl->operator_symbol == "+" && operand_count == 0) ||
                    (decl->operator_symbol == "-" && operand_count == 0))) {
            
        } else if (decl->operator_symbol != "[]" && operand_count != 1) {
            err("Binary operator requires exactly one operand :/\n");
        }

        if (!match_type_close()) {
            err("Expected '>' after operator pattern :/\n");
        }

        decl->method_name = operator_method_name(decl->operator_symbol, operand_count);
        decl->name = mangle_method_name(decl->method_owner, decl->method_name);

        if (check(":") && pos + 1 < tokens.size() &&
            (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
             tokens[pos + 1].value == "(")) {
            pos++; 
            decl->return_type_ref = parse_type_ref();
            decl->return_type = type_ref_to_btype(decl->return_type_ref);
            decl->return_type_annotation = type_ref_to_string(decl->return_type_ref);
        }

        const bool boolean_operator =
            decl->operator_symbol == "<" || decl->operator_symbol == ">" ||
            decl->operator_symbol == "<=" || decl->operator_symbol == ">=" ||
            decl->operator_symbol == "is" || decl->operator_symbol == "not" ||
            decl->operator_symbol == "==" || decl->operator_symbol == "!=" ||
            decl->operator_symbol == "and" || decl->operator_symbol == "or" ||
            decl->operator_symbol == "!";
        if (boolean_operator) {
            if (decl->return_type != BType::UNKNOWN && decl->return_type != BType::BOOL) {
                err("Comparison/logical operators must return bol :/\n");
            }
            decl->return_type = BType::BOOL;
            decl->return_type_ref = TypeRef{};
            decl->return_type_ref.base = BType::BOOL;
            decl->return_type_annotation = "bol";
        }
        if (is_compound_assignment_operator(decl->operator_symbol)) {
            if (decl->return_type != BType::UNKNOWN &&
                decl->return_type != BType::VOID) {
                err("Compound assignment operators must return nul :/\n");
            }
            decl->return_type = BType::VOID;
            decl->return_type_ref = TypeRef{};
            decl->return_type_ref.base = BType::VOID;
            decl->return_type_annotation = "nul";
        }

        decl->body = parse_block();
        current_type_params = saved_type_params;
        return decl;
    }

    bool check_deref_assign() {
        if (is_at_end()) return false;
        
        if (tokens[pos].type == TWORD && pos + 2 < tokens.size() && 
            tokens[pos + 1].value == "^" &&
            is_assignment_operator(tokens[pos + 2].value)) {
            return true;
        }
        
        if (check("^") && pos + 1 < tokens.size() &&
            is_assignment_operator(tokens[pos + 1].value)) {
            return true;
        }
        return false;
    }

    std::unique_ptr<Stmt> parse_statement() {
        const size_t statement_start = pos;
        auto statement = parse_statement_impl();
        if (statement && statement->line <= 0) {
            statement->line = token_line(statement_start);
        }
        return statement;
    }

    std::unique_ptr<Stmt> parse_statement_impl() {
        if (is_at_end()) {
            auto stmt = std::make_unique<ExprStmt>();
            stmt->expression = std::make_unique<NullExpr>();
            return stmt;
        }

        if (check("var")) return parse_var_decl(false);
        if (check("const")) return parse_var_decl(true);
        if (check("if")) return parse_if();
        if (check("for")) return parse_for();
        if (check("match")) return parse_match();
        if (check("ret")) return parse_return();
        if (check("nodrop")) return parse_nodrop();
        if (check("dropnow")) return parse_dropnow();
        if (check("stop")) return parse_break();
        if (check("pass")) return parse_continue();
        if (check("{")) return parse_block();
        if (check("__ll")) return parse_ll();
        if (check("__llh")) return parse_llh();

        if (tokens[pos].type == TWORD && pos + 2 < tokens.size() &&
            ((tokens[pos + 1].value == "+" && tokens[pos + 2].value == "+") ||
             (tokens[pos + 1].value == "-" && tokens[pos + 2].value == "-"))) {
            auto stmt = std::make_unique<AssignStmt>();
            stmt->name = tokens[pos].value;
            stmt->assignment_op = tokens[pos + 1].value == "+" ? "+=" : "-=";
            pos += 3;

            auto one = std::make_unique<NumberExpr>();
            one->value = 1;
            one->is_float = false;
            one->btype = BType::INT;
            stmt->value = std::move(one);
            if (check(";")) pos++;
            return stmt;
        }

        if (check_deref_assign()) {
            return parse_deref_assign();
        }

        if (tokens[pos].type == TWORD && pos + 1 < tokens.size() &&
            is_assignment_operator(tokens[pos + 1].value)) {
            return parse_assign();
        }

        auto expr = parse_expression();
        
        if (is_assignment_operator(peek().value)) {
            if (dynamic_cast<MemberExpr*>(expr.get()) || dynamic_cast<IndexExpr*>(expr.get())) {
                std::string assignment_op = advance().value;
                auto stmt = std::make_unique<MemberAssignStmt>();
                stmt->lhs = std::move(expr);
                stmt->assignment_op = assignment_op;
                stmt->value = parse_expression();
                if (check(";")) pos++;
                return stmt;
            }
        }
        
        if (check(";")) pos++;
        auto stmt = std::make_unique<ExprStmt>();
        stmt->expression = std::move(expr);
        return stmt;
    }

    std::unique_ptr<NodropStmt> parse_nodrop() {
        pos++;
        auto stmt = std::make_unique<NodropStmt>();
        if (is_at_end() || tokens[pos].type != TWORD) {
            err("Expected variable name after 'nodrop' :/\n");
            return stmt;
        }
        stmt->name = tokens[pos].value;
        pos++;
        if (check(";")) pos++;
        return stmt;
    }

    std::unique_ptr<DropNowStmt> parse_dropnow() {
        pos++;
        auto stmt = std::make_unique<DropNowStmt>();
        if (is_at_end() || check("}")) {
            err("Expected a value after 'dropnow' :/\n");
            return stmt;
        }
        stmt->value = parse_expression();
        if (check(";")) pos++;
        return stmt;
    }

    std::unique_ptr<DerefAssignStmt> parse_deref_assign() {
        auto stmt = std::make_unique<DerefAssignStmt>();

        std::unique_ptr<Expr> ptr_operand;
        
        if (tokens[pos].value == "^") {
            pos++; 
            ptr_operand = parse_unary();
        } else {
            ptr_operand = parse_primary();
        }

        
        if (!match("^")) {
            err("Expected '^' in dereference assignment :/\n");
        }

        
        if (!is_assignment_operator(peek().value)) {
            err("Expected assignment operator after dereference :/\n");
        } else {
            stmt->assignment_op = advance().value;
        }

        
        
        stmt->pointer = std::move(ptr_operand);

        
        stmt->value = parse_expression();

        if (check(";")) pos++;

        return stmt;
    }

    std::unique_ptr<AssignStmt> parse_assign() {
        auto stmt = std::make_unique<AssignStmt>();

        if (tokens[pos].type != TWORD) {
            err("Expected variable name in assignment :/\n");
            return stmt;
        }
        stmt->name = tokens[pos].value;
        pos++;

        
        if (!is_assignment_operator(peek().value)) {
            err("Expected assignment operator in assignment :/\n");
            return stmt;
        }
        stmt->assignment_op = advance().value;

        
        stmt->value = parse_expression();

        if (check(";")) pos++;

        return stmt;
    }

    std::unique_ptr<ArrayAssignStmt> parse_array_assign() {
        auto stmt = std::make_unique<ArrayAssignStmt>();

        if (tokens[pos].type != TWORD) {
            err("Expected array name in array assignment :/\n");
            return stmt;
        }
        stmt->array_name = tokens[pos].value;
        pos++;

        
        if (!match("[")) {
            err("Expected '[' in array assignment :/\n");
            return stmt;
        }

        stmt->index = parse_expression();

        if (!match("]")) {
            err("Expected ']' in array assignment :/\n");
            return stmt;
        }

        if (!is_assignment_operator(peek().value)) {
            err("Expected assignment operator in array assignment :/\n");
            return stmt;
        }
        stmt->assignment_op = advance().value;

        stmt->value = parse_expression();

        if (check(";")) pos++;

        return stmt;
    }

    std::unique_ptr<LLStmt> parse_ll() {
        pos++; 

        auto stmt = std::make_unique<LLStmt>();

        
        if (tokens[pos].type == TSTRING) {
            stmt->llvm_code = tokens[pos].value;
            pos++;
            return stmt;
        }

        
        if (match("{")) {
            
            std::string llvm_code;
            while (!check("}")) {
                if (is_at_end()) {
                    err("Unterminated __ll block :/\n");
                    return stmt;
                }
                llvm_code += tokens[pos].value + " ";
                pos++;
            }
            pos++; 

            stmt->llvm_code = llvm_code;
            return stmt;
        }

        err("Expected string or '{' after __ll :/\n");
        return stmt;
    }

    std::unique_ptr<LLHStmt> parse_llh() {
        pos++; 

        auto stmt = std::make_unique<LLHStmt>();

        
        if (tokens[pos].type == TSTRING) {
            stmt->llvm_code = tokens[pos].value;
            pos++;
            return stmt;
        }

        err("Expected string after __llh :/\n");
        return stmt;
    }

    std::unique_ptr<TakeStmt> parse_take() {
        const std::string keyword = tokens[pos].value;
        pos++;

        auto stmt = std::make_unique<TakeStmt>();

        if (tokens[pos].type == TSTRING) {
            stmt->path = tokens[pos].value;
            pos++;
        } else {
            err("Expected string path in " + keyword + " :/\n");
        }

        return stmt;
    }

    std::unique_ptr<Stmt> parse_plugin_load() {
        if (check("take") || check("ftake")) return parse_take();
        return std::make_unique<ExprStmt>();
    }

    std::unique_ptr<BlockStmt> parse_block() {
        auto block = std::make_unique<BlockStmt>();

        if (!match("{")) {
            err("Expected '{' :/\n");
            return block;
        }

        while (!check("}") && !is_at_end()) {
            block->statements.push_back(parse_statement());
        }
        if (!match("}")) {
            err("Expected '}' before end of file :/\n");
        }

        return block;
    }

    std::unique_ptr<Stmt> parse_var_decl(bool is_const) {
        pos++;

        auto parse_named_declaration = [this, is_const](
            const TypeRef* shared_type = nullptr
        ) {
            auto decl = std::make_unique<VarDeclStmt>();
            decl->is_const = is_const;

            if (is_at_end() || tokens[pos].type != TWORD) {
                err("Expected variable name :/\n");
                return decl;
            }
            decl->name = tokens[pos].value;
            pos++;

            if (match("[")) {
                decl->array_size = parse_expression();
                if (!match("]")) {
                    err("Expected ']' in array size :/\n");
                }
            }

            if (shared_type) {
                decl->type_ref = *shared_type;
            } else {
                decl->type_ref = try_read_type_annotation_ref();
            }
            decl->type = type_ref_to_btype(decl->type_ref);
            decl->struct_name = decl->type_ref.name;
            if (decl->type_ref.base != BType::UNKNOWN ||
                !decl->type_ref.name.empty() || decl->type_ref.is_pointer ||
                decl->type_ref.is_array) {
                decl->type_annotation = type_ref_to_string(decl->type_ref);
            }
            if (decl->array_size && decl->type == BType::UNKNOWN) {
                decl->type = BType::INT;
            }

            if (match("=")) {
                decl->initializer = parse_expression();
            } else if (decl->type_ref.base == BType::STRUCT && match("(")) {
                decl->has_constructor_call = true;
                while (!check(")") && !is_at_end()) {
                    if (check(",")) {
                        pos++;
                        continue;
                    }
                    decl->constructor_args.push_back(parse_expression());
                }
                if (!match(")")) {
                    err("Expected ')' after constructor arguments :/\n");
                }
            }
            return decl;
        };

        const size_t type_start = pos;
        const bool may_have_grouped_type =
            !is_at_end() && pos + 1 < tokens.size() &&
            (tokens[pos + 1].value == "(" || tokens[pos + 1].value == "<");
        if (may_have_grouped_type) {
            const std::vector<Token> tokens_before_type_probe = tokens;
            TypeRef shared_type = parse_type_ref();
            if ((shared_type.base != BType::UNKNOWN || !shared_type.name.empty()) &&
                match("(")) {
                auto group = std::make_unique<BlockStmt>();
                group->is_declaration_group = true;
                const auto is_integer_enum_type = [](const TypeRef& type) {
                    if (type.is_pointer || type.is_array ||
                        !type.type_args.empty()) {
                        return false;
                    }
                    switch (type.base) {
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
                };
                const auto make_next_enum_value = [&shared_type](
                    const std::string& previous_name
                ) -> std::unique_ptr<Expr> {
                    auto previous = std::make_unique<VariableExpr>();
                    previous->name = previous_name;
                    previous->btype = type_ref_to_btype(shared_type);

                    auto one = std::make_unique<NumberExpr>();
                    one->value = 1;
                    one->is_float = false;
                    one->literal = "1";
                    one->btype = BType::INT;

                    auto next = std::make_unique<BinaryExpr>();
                    next->op = "+";
                    next->left = std::move(previous);
                    next->right = std::move(one);
                    next->btype = type_ref_to_btype(shared_type);
                    return next;
                };

                bool automatic_enum = false;
                std::string previous_enum_name;
                while (!check(")") && !is_at_end()) {
                    std::unique_ptr<Stmt> declaration =
                        parse_named_declaration(&shared_type);
                    auto* variable =
                        dynamic_cast<VarDeclStmt*>(declaration.get());
                    const bool had_explicit_initializer =
                        variable && variable->initializer != nullptr;
                    bool generated_initializer = false;

                    if (automatic_enum && variable &&
                        !variable->initializer) {
                        variable->initializer =
                            make_next_enum_value(previous_enum_name);
                        generated_initializer = true;
                    }

                    const bool continues_automatically = match("pass");
                    if (continues_automatically) {
                        bool valid_marker = true;
                        if (!is_const) {
                            err("Automatic enum continuation is only available for const declarations :/\n");
                            valid_marker = false;
                        }
                        if (!is_integer_enum_type(shared_type)) {
                            err("Automatic enum continuation requires an integer declaration type :/\n");
                            valid_marker = false;
                        }
                        if (!variable || !variable->initializer) {
                            err("Automatic enum continuation requires an initialized value before 'pass' :/\n");
                            valid_marker = false;
                        }
                        automatic_enum = valid_marker;
                        previous_enum_name = valid_marker
                            ? variable->name : "";
                    } else if (automatic_enum && generated_initializer) {
                        previous_enum_name = variable->name;
                    } else if (had_explicit_initializer) {
                        automatic_enum = false;
                        previous_enum_name.clear();
                    }

                    group->statements.push_back(std::move(declaration));
                    if (!match(",")) break;
                }

                if (!match(")")) {
                    err("Expected ')' after grouped variable declarations :/\n");
                }
                if (check(";")) pos++;
                return group;
            }
            pos = type_start;
            tokens = tokens_before_type_probe;
        }

        if (!is_at_end() && tokens[pos].type == TWORD &&
            pos + 1 < tokens.size() && tokens[pos + 1].value == ",") {
            auto destructure = std::make_unique<TupleDestructureStmt>();
            destructure->is_const = is_const;
            destructure->names.push_back(tokens[pos++].value);
            while (match(",")) {
                if (is_at_end() || tokens[pos].type != TWORD) {
                    err("Expected variable name after ',' in tuple destructuring :/\n");
                    return destructure;
                }
                destructure->names.push_back(tokens[pos++].value);
            }
            if (!match("=")) {
                err("Tuple destructuring requires an initializer :/\n");
                return destructure;
            }
            destructure->initializer = parse_expression();
            if (check(";")) pos++;
            return destructure;
        }

        auto first = parse_named_declaration();
        if (!check(",")) {
            if (check(";")) pos++;
            return first;
        }

        auto group = std::make_unique<BlockStmt>();
        group->is_declaration_group = true;
        group->statements.push_back(std::move(first));
        while (match(",")) {
            group->statements.push_back(parse_named_declaration());
        }
        if (check(";")) pos++;
        return group;
    }

    std::unique_ptr<IfStmt> parse_if_after_keyword() {
        auto stmt = std::make_unique<IfStmt>();
        stmt->condition = parse_expression();
        stmt->then_branch = parse_statement();

        if (match("elif")) {
            stmt->else_branch = parse_if_after_keyword();
        } else if (match("else")) {
            stmt->else_branch = parse_statement();
        }

        return stmt;
    }

    std::unique_ptr<IfStmt> parse_if() {
        pos++; 
        return parse_if_after_keyword();
    }

    std::unique_ptr<Stmt> parse_for() {
        pos++; 

        if (tokens[pos].type == TWORD && check_next("in")) {
            auto stmt = std::make_unique<ForStmt>();
            stmt->var_name = tokens[pos].value;
            pos++;
            pos++; 
            stmt->bound = parse_expression();
            stmt->body = parse_statement();
            return stmt;
        }

        auto fws = std::make_unique<ForWhileStmt>();
        fws->condition = parse_expression();
        fws->body = parse_statement();
        return fws;
    }

    bool check_next(const std::string& v) {
        if (pos + 1 >= tokens.size() || tokens[pos + 1].type == TSTRING) {
            return false;
        }
        const std::string& value = tokens[pos + 1].value;
        const bool is_deprecated_alias =
            (v == "func" && value == "fn") ||
            (v == "var" && value == "let");
        if (is_deprecated_alias) warn_deprecated_keyword(pos + 1);
        return value == v || is_deprecated_alias;
    }

    std::unique_ptr<ReturnStmt> parse_return() {
        pos++; 

        auto stmt = std::make_unique<ReturnStmt>();

        if (!check(";")) {
            stmt->value = parse_expression();
        }

        if (check(";")) pos++;

        return stmt;
    }

    std::unique_ptr<BreakStmt> parse_break() {
        pos++; 
        if (check(";")) pos++;
        return std::make_unique<BreakStmt>();
    }

    std::unique_ptr<ContinueStmt> parse_continue() {
        pos++; 
        if (check(";")) pos++;
        return std::make_unique<ContinueStmt>();
    }

    std::unique_ptr<MatchStmt> parse_match() {
        pos++; 

        auto stmt = std::make_unique<MatchStmt>();
        stmt->value = parse_expression();

        if (!match("{")) {
            err("Expected '{' after match value :/\n");
            return stmt;
        }

        while (!check("}")) {
            if (check("_")) {
                pos++; 
                if (match(">")) {
                    stmt->default_case = parse_statement();
                }
                continue;
            }

            
            
            auto case_val = parse_ternary();
            if (match(":")) {
                stmt->cases.push_back({std::move(case_val), parse_statement()});
            }
        }
        pos++; 

        return stmt;
    }

    std::unique_ptr<Expr> parse_expression() {
        const size_t expression_start = pos;
        auto expression = parse_expression_impl();
        if (expression && expression->line <= 0) {
            expression->line = token_line(expression_start);
        }
        return expression;
    }

    std::unique_ptr<Expr> parse_expression_impl() {
        return parse_colon();
    }

    std::unique_ptr<Expr> parse_colon() {
        auto left = parse_ternary();

        while (match(":")) {
            auto expr = std::make_unique<BinaryExpr>();
            expr->op = ":";
            expr->left = std::move(left);
            expr->right = parse_ternary();
            left = std::move(expr);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_ternary() {
        auto expr = parse_or();

        if (match("?")) {
            auto stmt = std::make_unique<TernaryExpr>();
            stmt->cond = std::move(expr);
            stmt->then_expr = parse_ternary();

            if (!match(":")) {
                err("Expected ':' in ternary :/\n");
                return std::move(stmt->then_expr);
            }
            stmt->else_expr = parse_ternary();

            return stmt;
        }

        return expr;
    }

    std::unique_ptr<Expr> parse_or() {
        auto left = parse_and();

        while (match("or")) {
            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = "or";
            stmt->btype = BType::BOOL;
            stmt->left = std::move(left);
            stmt->right = parse_and();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_and() {
        auto left = parse_xor();

        while (match("and")) {
            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = "and";
            stmt->btype = BType::BOOL;
            stmt->left = std::move(left);
            stmt->right = parse_xor();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_xor() {
        auto left = parse_bit_or();

        while (check("#") && !starts_attribute_on_new_line()) {
            pos++;
            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = "#";
            stmt->left = std::move(left);
            stmt->right = parse_bit_or();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_bit_or() {
        auto left = parse_bit_and();

        while (match("|")) {
            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = "|";
            stmt->left = std::move(left);
            stmt->right = parse_bit_and();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_bit_and() {
        auto left = parse_shift();

        while (match("&")) {
            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = "&";
            stmt->left = std::move(left);
            stmt->right = parse_shift();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_shift() {
        auto left = parse_comparison();

        while (check("<<") || check(">>")) {
            std::string op = tokens[pos].value;
            pos++;

            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = op;
            stmt->left = std::move(left);
            stmt->right = parse_comparison();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_comparison() {
        auto left = parse_term();

        while (check(">") || check("<") || check(">=") || check("<=") ||
               check("is") || check("not") || check("==") || check("!=")) {
            std::string op = tokens[pos].value;
            pos++;

            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = op;
            stmt->btype = BType::BOOL;
            stmt->left = std::move(left);
            stmt->right = parse_term();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_term() {
        auto left = parse_factor();

        while (check("+") || check("-")) {
            std::string op = tokens[pos].value;
            pos++;

            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = op;
            stmt->left = std::move(left);
            stmt->right = parse_factor();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_factor() {
        auto left = parse_unary();

        while (check("*") || check("/") || check("%")) {
            std::string op = tokens[pos].value;
            pos++;

            auto stmt = std::make_unique<BinaryExpr>();
            stmt->op = op;
            stmt->left = std::move(left);
            stmt->right = parse_unary();
            left = std::move(stmt);
        }

        return left;
    }

    std::unique_ptr<Expr> parse_unary() {
        if (check("-") || check("!") || check("+") || check("~")) {
            std::string op = tokens[pos].value;
            pos++;
            auto stmt = std::make_unique<UnaryExpr>();
            stmt->op = op;
            stmt->operand = parse_unary();
            return stmt;
        }

        
        if (check("^")) {
            pos++;
            auto ref = std::make_unique<RefExpr>();
            ref->operand = parse_unary();
            return ref;
        }

        return parse_postfix(parse_primary());
    }

    std::unique_ptr<Expr> parse_postfix(std::unique_ptr<Expr> expr) {
        while (true) {
            // A newline ends a statement in Ferra, so never join the next
            // line's parenthesized expression onto the preceding value.
            if (check("(") && !tokens[pos].line_break_before) {
                pos++;
                auto call = std::make_unique<CallExpr>();
                call->callee_expr = std::move(expr);
                while (!check(")") && !is_at_end()) {
                    if (check(",")) { pos++; continue; }
                    call->args.push_back(parse_expression());
                }
                if (!match(")")) {
                    err("Expected ')' after call arguments :/\n");
                }
                expr = std::move(call);
                continue;
            }

            if (check("[")) {
                pos++; 

                auto idx = std::make_unique<IndexExpr>();
                idx->object = std::move(expr);
                idx->index = parse_expression();
                idx->btype = BType::UNKNOWN;  

                if (!match("]")) {
                    err("Expected ']' in index expression :/\n");
                }

                expr = std::move(idx);
                continue;
            }

            if (check("^")) {
                pos++; 
                auto deref = std::make_unique<DerefExpr>();
                deref->operand = std::move(expr);
                expr = std::move(deref);
                continue;
            }

            if (check(".")) {
                pos++; 

                if (tokens[pos].type != TWORD) {
                    err("Expected member name after '.' :/\n");
                    break;
                }

                std::string member_name = tokens[pos].value;
                pos++;

                
                
                if (match("(")) {
                    auto call = std::make_unique<CallExpr>();
                    call->callee = member_name;
                    call->is_method_call = true;
                    call->args.push_back(std::move(expr));

                    while (!check(")") && !is_at_end()) {
                        if (check(",")) { pos++; continue; }
                        call->args.push_back(parse_expression());
                    }
                    if (!match(")")) {
                        err("Expected ')' after method arguments :/\n");
                    }

                    expr = std::move(call);
                    continue;
                }

                auto member = std::make_unique<MemberExpr>();
                member->object = std::move(expr);
                member->member = member_name;
                member->btype = BType::UNKNOWN;

                expr = std::move(member);
                continue;
            }

            break;
        }

        
        if (match("as")) {
            auto cast = std::make_unique<AsExpr>();
            cast->operand = std::move(expr);
            
            if (tokens[pos].type == TWORD || tokens[pos].type == TNUM) {
                size_t annot_start = pos;
                BType parsed = parse_type();
                
                for (size_t j = annot_start; j < pos; j++) {
                    if (!cast->type_annotation.empty()) cast->type_annotation += " ";
                    cast->type_annotation += tokens[j].value;
                }
                cast->btype = parsed;
            }
            
            expr = std::move(cast);
        }

        return expr;
    }

    std::unique_ptr<Expr> parse_primary() {
        if (is_at_end()) {
            err("Unexpected end of file in expression :/\n");
            return std::make_unique<NullExpr>();
        }

        if (check("true")) {
            pos++;
            auto expr = std::make_unique<BoolExpr>();
            expr->value = true;
            expr->btype = BType::BOOL;
            return expr;
        }
        if (check("false")) {
            pos++;
            auto expr = std::make_unique<BoolExpr>();
            expr->value = false;
            expr->btype = BType::BOOL;
            return expr;
        }
        if (check("null")) {
            pos++;

            auto expr = std::make_unique<NullExpr>();
            expr->btype = BType::PTR;

            return expr;
        }

    
        if (check("sizeof")) {
            pos++; 

            if (!match("(")) {
                err("Expected '(' after sizeof :/\n");
                return std::make_unique<NullExpr>();
            }

            auto sizeof_expr = std::make_unique<SizeofExpr>();

            
            if (check("int") || check("f64") || check("f32") || check("bol") || check("i1") || check("str") || check("ptr") ||
                check("isize") || check("usize") || check("hex") ||
                check("i8") || check("i16") || check("i32") || check("i64") ||
                check("u8") || check("u16") || check("u32") || check("u64") ||
                (!is_at_end() && tokens[pos].type == TWORD &&
                struct_names.count(tokens[pos].value))) {
                sizeof_expr->name = tokens[pos].value;
                pos++;

                
                if (check("*")) {
                    sizeof_expr->name += "*";
                    pos++; 
                }
                
                else if (check("[")) {
                    pos++; 
                    if (match("]")) {
                        sizeof_expr->name += "[]";
                    }
                }
            } else {
                sizeof_expr->expr = parse_expression();
            }

            if (!match(")")) {
                err("Expected ')' after sizeof argument :/\n");
            }

            sizeof_expr->btype = BType::INT;
            return sizeof_expr;
        }

        if (tokens[pos].type == TNUM) {
            std::string literal = tokens[pos].value;
            bool is_float = literal.find('.') != std::string::npos;
            double val = 0.0;
            try {
                if (is_float) {
                    val = std::stod(literal);
                } else {
                    val = static_cast<double>(std::stoull(literal));
                }
            } catch (const std::exception&) {
                err("Integer literal is out of u64 range: '" + literal + "' :/\n");
                literal = "0";
            }
            pos++;
            auto expr = std::make_unique<NumberExpr>();
            expr->value = val;
            expr->is_float = is_float;
            expr->literal = literal;
            expr->btype = is_float ? BType::F64 : BType::INT;
            return expr;
        }

        if (tokens[pos].type == TSTRING) {
            std::string val = tokens[pos].value;
            pos++;
            auto expr = std::make_unique<StringExpr>();
            expr->value = val;
            expr->btype = BType::STR;
            return expr;
        }

        if (tokens[pos].type == TWORD) {
            if (check("func")) {
                return parse_anonymous_fn();
            }

            if (looks_like_struct_literal()) {
                return parse_struct_literal();
            }

            if (pos + 1 < tokens.size() && tokens[pos + 1].value == "(" &&
                !tokens[pos + 1].line_break_before) {
                std::string callee = tokens[pos].value;
                pos++;
                pos++; 

                auto call = std::make_unique<CallExpr>();
                call->callee = callee;

                while (!check(")") && !is_at_end()) {
                    if (check(",")) { pos++; continue; }
                    call->args.push_back(parse_expression());
                }
                if (!match(")")) {
                    err("Expected ')' after function arguments :/\n");
                }

                return call;
            }

            
            if (pos + 1 < tokens.size() && tokens[pos + 1].value == "<") {
                
                
                size_t lookahead = pos + 2;
                bool has_template_args = false;
                int depth = 1;
                while (lookahead < tokens.size() && depth > 0) {
                    if (tokens[lookahead].value == "<") depth++;
                    else if (tokens[lookahead].value == ">") depth--;
                    else if (tokens[lookahead].value == "(" && depth == 1) {
                        
                        break;
                    }
                    if (depth == 0 && lookahead + 1 < tokens.size() && tokens[lookahead + 1].value == "(") {
                        has_template_args = true;
                    }
                    lookahead++;
                }

                if (has_template_args) {
                    std::string callee = tokens[pos].value;
                    pos++;
                    pos++; 

                    auto call = std::make_unique<CallExpr>();
                    call->callee = callee;

                    
                    while (!check(">") && !is_at_end()) {
                        if (check(",")) { pos++; continue; }
                        BType targ = parse_type();
                        call->template_args.push_back(targ);
                    }
                    pos++; 

                    
                    if (!match("(")) {
                        err("Expected '(' after template args :/\n");
                        return call;
                    }

                    while (!check(")") && !is_at_end()) {
                        if (check(",")) { pos++; continue; }
                        call->args.push_back(parse_expression());
                    }
                    if (!match(")")) {
                        err("Expected ')' after function arguments :/\n");
                    }

                    return call;
                }
            }

            
            std::string name = tokens[pos].value;
            pos++;
            auto expr = std::make_unique<VariableExpr>();
            expr->name = name;
            return expr;
        }

        if (check("(")) {
            pos++;
            auto first = parse_expression();
            if (match(",")) {
                auto tuple = std::make_unique<TupleExpr>();
                tuple->btype = BType::TUPLE;
                tuple->elements.push_back(std::move(first));
                while (!check(")") && !is_at_end()) {
                    tuple->elements.push_back(parse_expression());
                    if (!match(",")) break;
                }
                if (!match(")")) {
                    err("Expected ')' after tuple literal :/\n");
                }
                return tuple;
            }
            if (!match(")")) {
                err("Expected ')' :/\n");
            }
            return first;
        }

        if (check("[")) {
            return parse_array();
        }

        std::string context;
        const size_t context_begin = pos > 4 ? pos - 4 : 0;
        const size_t context_end = std::min(tokens.size(), pos + 5);
        for (size_t i = context_begin; i < context_end; ++i) {
            if (!context.empty()) context += " ";
            if (i == pos) context += ">>";
            context += tokens[i].value;
            if (i == pos) context += "<<";
        }
        err("Unexpected token in expression: " + peek().value +
               " near '" + context + "' :/\n");
        pos++;
        return std::make_unique<NullExpr>();
    }

    bool looks_like_struct_literal() const {
        if (pos >= tokens.size() || tokens[pos].type != TWORD ||
            !struct_names.count(tokens[pos].value)) {
            return false;
        }

        size_t cursor = pos + 1;
        if (cursor < tokens.size() && tokens[cursor].value == "{") {
            return true;
        }
        if (cursor >= tokens.size() || tokens[cursor].value != "<") {
            return false;
        }

        int depth = 0;
        for (; cursor < tokens.size(); ++cursor) {
            const std::string& value = tokens[cursor].value;
            if (value == "<") {
                ++depth;
            } else if (value == ">") {
                --depth;
            } else if (value == ">>") {
                depth -= 2;
            }

            if (depth <= 0) {
                return cursor + 1 < tokens.size() &&
                       tokens[cursor + 1].value == "{";
            }
        }
        return false;
    }

    std::unique_ptr<StructLiteralExpr> parse_struct_literal() {
        auto literal = std::make_unique<StructLiteralExpr>();
        literal->type_ref = parse_type_ref();
        literal->struct_name = literal->type_ref.name;
        literal->btype = BType::STRUCT;

        if (!match("{")) {
            err("Expected '{' after struct literal type :/\n");
            return literal;
        }

        while (!check("}") && !is_at_end()) {
            if (check(",") || check(";")) {
                pos++;
                continue;
            }

            if (tokens[pos].type != TWORD) {
                err("Expected field name in struct literal :/\n");
                pos++;
                continue;
            }

            StructLiteralField field;
            field.name = advance().value;
            if (!match(":")) {
                err("Expected ':' after struct literal field '" +
                       field.name + "' :/\n");
                break;
            }
            field.value = parse_expression();
            literal->fields.push_back(std::move(field));

            if (check(",") || check(";")) pos++;
        }

        if (!match("}")) {
            err("Expected '}' after struct literal fields :/\n");
        }
        return literal;
    }

    std::unique_ptr<ArrayExpr> parse_array() {
        pos++; 

        auto arr = std::make_unique<ArrayExpr>();
        arr->btype = BType::ARR;

        while (!check("]") && !is_at_end()) {
            if (check(",")) { pos++; continue; }
            arr->elements.push_back(parse_expression());
        }
        if (!match("]")) {
            err("Expected ']' before end of file :/\n");
        }

        return arr;
    }

    std::unique_ptr<AnonymousFnExpr> parse_anonymous_fn() {
        pos++; 

        auto fn = std::make_unique<AnonymousFnExpr>();

        
        if (check("<")) {
            fn->type_params = parse_type_params();
        }

        
        auto saved_type_params = current_type_params;
        current_type_params = fn->type_params;

        if (!match("(")) {
            err("Expected '(' in anonymous function :/\n");
            current_type_params = saved_type_params;
            return fn;
        }

        while (!check(")") && !is_at_end()) {
            if (check(",")) { pos++; continue; }

            if (tokens[pos].type != TWORD) {
                err("Expected parameter name :/\n");
                current_type_params = saved_type_params;
                return fn;
            }
            std::string param_name = tokens[pos].value;
            pos++;

            
            TypeRef param_type_ref;
            if (check(":") && pos + 1 < tokens.size() &&
                (tokens[pos + 1].type == TWORD || tokens[pos + 1].type == TNUM ||
                 tokens[pos + 1].value == "(")) {
                pos++; 
                param_type_ref = parse_parameter_type_ref();
            }
            ParamDecl pdecl;
            pdecl.name = param_name;
            pdecl.type_ref = param_type_ref;
            pdecl.type = type_ref_to_btype(param_type_ref);
            pdecl.type_annotation = type_ref_to_string(param_type_ref);
            if (param_type_ref.base == BType::STRUCT) {
                pdecl.struct_name = param_type_ref.name;
            }
            fn->params.push_back(pdecl);
        }
        if (!match(")")) {
            err("Expected ')' in anonymous function :/\n");
            current_type_params = saved_type_params;
            return fn;
        }

        fn->return_type = try_read_type_annotation();

        if (match(">")) {
            auto expr_stmt = std::make_unique<ExprStmt>();
            expr_stmt->expression = parse_expression();
            fn->body = std::unique_ptr<Stmt>(std::move(expr_stmt));
        } else {
            fn->body = parse_block();
        }

        
        current_type_params = saved_type_params;

        return fn;
    }
};


std::unique_ptr<Program> parse_ast(std::vector<Token>& tokens) {
    Parser parser(tokens);
    return parser.parse_program();
}
