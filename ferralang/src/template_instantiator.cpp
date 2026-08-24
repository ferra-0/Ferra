#include "template_instantiator.h"
#include "global.h"
#include <cassert>

static std::unique_ptr<Expr> clone_expr(const Expr* expr, const TypeSubstitution& subst);
static std::unique_ptr<Stmt> clone_stmt(const Stmt* stmt, const TypeSubstitution& subst);

static BType substitute_type_from_annotation(const std::string& annotation, const TypeSubstitution& subst) {
    if (annotation.empty()) return BType::UNKNOWN;
    
    if (subst.has(annotation)) {
        return subst.get(annotation);
    }
    
    if (annotation.size() >= 2 && annotation.back() == '*') {
        std::string base = annotation.substr(0, annotation.size() - 1);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        
        BType base_type = BType::UNKNOWN;
        if (subst.has(base)) {
            base_type = subst.get(base);
        } else {
            if (base == "i8") base_type = BType::I8;
            else if (base == "i16") base_type = BType::I16;
            else if (base == "i32") base_type = BType::I32;
            else if (base == "i64") base_type = BType::I64;
            else if (base == "u8") base_type = BType::U8;
            else if (base == "u16") base_type = BType::U16;
            else if (base == "u32") base_type = BType::U32;
            else if (base == "u64") base_type = BType::U64;
            else if (base == "isize") base_type = BType::ISIZE;
            else if (base == "usize") base_type = BType::USIZE;
            else if (base == "hex") base_type = BType::HEX;
            else if (base == "f32") base_type = BType::F32;
            else if (base == "f64") base_type = BType::F64;
            else if (base == "str") base_type = BType::STR;
            else if (base == "int") base_type = BType::INT;
            else if (base == "bol") base_type = BType::BOOL;
            else if (base == "ptr") base_type = BType::PTR;
        }
        
        
        switch (base_type) {
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
            case BType::UNKNOWN: return BType::PTR;
            default: return BType::PTR;
        }
    }
    
    
    if (annotation.size() >= 2 && annotation.back() == ']') {
        size_t bracket_pos = annotation.find('[');
        if (bracket_pos != std::string::npos) {
            std::string base = annotation.substr(0, bracket_pos);
            
            while (!base.empty() && base.back() == ' ') base.pop_back();
            
            BType base_type = BType::UNKNOWN;
            if (subst.has(base)) {
                base_type = subst.get(base);
            } else {
                
                if (base == "i8") base_type = BType::I8;
                else if (base == "i16") base_type = BType::I16;
                else if (base == "i32") base_type = BType::I32;
                else if (base == "i64") base_type = BType::I64;
                else if (base == "u8") base_type = BType::U8;
                else if (base == "u16") base_type = BType::U16;
                else if (base == "u32") base_type = BType::U32;
                else if (base == "u64") base_type = BType::U64;
                else if (base == "isize") base_type = BType::ISIZE;
                else if (base == "usize") base_type = BType::USIZE;
                else if (base == "hex") base_type = BType::HEX;
                else if (base == "f32") base_type = BType::F32;
                else if (base == "f64") base_type = BType::F64;
                else if (base == "str") base_type = BType::STR;
                else if (base == "int") base_type = BType::INT;
                else if (base == "bol") base_type = BType::BOOL;
                else if (base == "ptr") base_type = BType::PTR;
            }
            
            
            switch (base_type) {
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
                case BType::UNKNOWN: return BType::ARR;
                default: return BType::ARR;
            }
        }
    }
    
    
    if (annotation == "i8") return BType::I8;
    if (annotation == "i16") return BType::I16;
    if (annotation == "i32") return BType::I32;
    if (annotation == "i64") return BType::I64;
    if (annotation == "u8") return BType::U8;
    if (annotation == "u16") return BType::U16;
    if (annotation == "u32") return BType::U32;
    if (annotation == "u64") return BType::U64;
    if (annotation == "isize") return BType::ISIZE;
    if (annotation == "usize") return BType::USIZE;
    if (annotation == "hex") return BType::HEX;
    if (annotation == "f32") return BType::F32;
    if (annotation == "f64") return BType::F64;
    if (annotation == "str") return BType::STR;
    if (annotation == "int") return BType::INT;
    if (annotation == "bol") return BType::BOOL;
    if (annotation == "ptr") return BType::PTR;
    if (annotation == "nul") return BType::VOID;
    if (annotation == "arr") return BType::ARR;
    
    return BType::UNKNOWN;
}

static TypeRef substitute_type_ref(const TypeRef& type_ref, const TypeSubstitution& subst) {
    if (type_ref.base == BType::UNKNOWN && !type_ref.name.empty() && subst.has(type_ref.name)) {
        TypeRef result = subst.get_type_ref(type_ref.name);
        result.is_pointer = result.is_pointer || type_ref.is_pointer;
        result.is_array = result.is_array || type_ref.is_array;
        result.pass_by_value =
            result.pass_by_value || type_ref.pass_by_value;
        return result;
    }

    TypeRef result = type_ref;
    result.type_args.clear();
    for (const auto& arg : type_ref.type_args) {
        result.type_args.push_back(substitute_type_ref(arg, subst));
    }
    return result;
}

static std::unique_ptr<Expr> clone_expr(const Expr* expr, const TypeSubstitution& subst) {
    if (!expr) return nullptr;
    
    if (auto* num = dynamic_cast<const NumberExpr*>(expr)) {
        auto clone = std::make_unique<NumberExpr>();
        clone->value = num->value;
        clone->is_float = num->is_float;
        clone->literal = num->literal;
        clone->btype = num->btype;
        return clone;
    }
    
    if (auto* str = dynamic_cast<const StringExpr*>(expr)) {
        auto clone = std::make_unique<StringExpr>();
        clone->value = str->value;
        clone->btype = str->btype;
        return clone;
    }
    
    if (auto* boolean = dynamic_cast<const BoolExpr*>(expr)) {
        auto clone = std::make_unique<BoolExpr>();
        clone->value = boolean->value;
        clone->btype = boolean->btype;
        return clone;
    }
    
    if (auto* null = dynamic_cast<const NullExpr*>(expr)) {
        return std::make_unique<NullExpr>();
    }
    
    if (auto* var = dynamic_cast<const VariableExpr*>(expr)) {
        auto clone = std::make_unique<VariableExpr>();
        clone->name = var->name;
        clone->btype = var->btype;
        return clone;
    }
    
    if (auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        auto clone = std::make_unique<BinaryExpr>();
        clone->op = bin->op;
        clone->left = clone_expr(bin->left.get(), subst);
        clone->right = clone_expr(bin->right.get(), subst);
        clone->btype = bin->btype;
        return clone;
    }
    
    if (auto* unary = dynamic_cast<const UnaryExpr*>(expr)) {
        auto clone = std::make_unique<UnaryExpr>();
        clone->op = unary->op;
        clone->operand = clone_expr(unary->operand.get(), subst);
        clone->btype = unary->btype;
        return clone;
    }
    
    if (auto* tern = dynamic_cast<const TernaryExpr*>(expr)) {
        auto clone = std::make_unique<TernaryExpr>();
        clone->cond = clone_expr(tern->cond.get(), subst);
        clone->then_expr = clone_expr(tern->then_expr.get(), subst);
        clone->else_expr = clone_expr(tern->else_expr.get(), subst);
        clone->btype = tern->btype;
        return clone;
    }
    
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        auto clone = std::make_unique<CallExpr>();
        clone->callee = call->callee;
        clone->template_args = call->template_args;  
        clone->is_method_call = call->is_method_call;
        for (const auto& arg : call->args) {
            clone->args.push_back(clone_expr(arg.get(), subst));
        }
        clone->btype = call->btype;
        return clone;
    }
    
    if (auto* arr = dynamic_cast<const ArrayExpr*>(expr)) {
        auto clone = std::make_unique<ArrayExpr>();
        for (const auto& elem : arr->elements) {
            clone->elements.push_back(clone_expr(elem.get(), subst));
        }
        clone->btype = arr->btype;
        return clone;
    }

    if (auto* tuple = dynamic_cast<const TupleExpr*>(expr)) {
        auto clone = std::make_unique<TupleExpr>();
        for (const auto& element : tuple->elements) {
            clone->elements.push_back(clone_expr(element.get(), subst));
        }
        clone->btype = tuple->btype;
        return clone;
    }

    if (auto* literal = dynamic_cast<const StructLiteralExpr*>(expr)) {
        auto clone = std::make_unique<StructLiteralExpr>();
        clone->type_ref = substitute_type_ref(literal->type_ref, subst);
        clone->struct_name = clone->type_ref.name.empty()
            ? literal->struct_name
            : clone->type_ref.name;
        for (const auto& field : literal->fields) {
            StructLiteralField field_clone;
            field_clone.name = field.name;
            field_clone.value = clone_expr(field.value.get(), subst);
            clone->fields.push_back(std::move(field_clone));
        }
        clone->btype = BType::STRUCT;
        return clone;
    }
    
    if (auto* idx = dynamic_cast<const IndexExpr*>(expr)) {
        auto clone = std::make_unique<IndexExpr>();
        clone->object = clone_expr(idx->object.get(), subst);
        clone->index = clone_expr(idx->index.get(), subst);
        clone->btype = idx->btype;
        return clone;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        auto clone = std::make_unique<MemberExpr>();
        clone->object = clone_expr(member->object.get(), subst);
        clone->member = member->member;
        clone->struct_name = member->struct_name;
        clone->btype = member->btype;
        return clone;
    }
    
    if (auto* sz = dynamic_cast<const SizeofExpr*>(expr)) {
        auto clone = std::make_unique<SizeofExpr>();
        clone->name = sz->name;
        clone->expr = clone_expr(sz->expr.get(), subst);
        clone->btype = sz->btype;
        return clone;
    }
    
    if (auto* ref = dynamic_cast<const RefExpr*>(expr)) {
        auto clone = std::make_unique<RefExpr>();
        clone->operand = clone_expr(ref->operand.get(), subst);
        clone->btype = ref->btype;
        return clone;
    }
    
    if (auto* deref = dynamic_cast<const DerefExpr*>(expr)) {
        auto clone = std::make_unique<DerefExpr>();
        clone->operand = clone_expr(deref->operand.get(), subst);
        clone->btype = deref->btype;
        return clone;
    }
    
    if (auto* as_expr = dynamic_cast<const AsExpr*>(expr)) {
        auto clone = std::make_unique<AsExpr>();
        clone->operand = clone_expr(as_expr->operand.get(), subst);
        clone->type_annotation = as_expr->type_annotation;
        clone->btype = substitute_type_from_annotation(as_expr->type_annotation, subst);
        return clone;
    }
    
    if (auto* anon = dynamic_cast<const AnonymousFnExpr*>(expr)) {
        auto clone = std::make_unique<AnonymousFnExpr>();
        clone->type_params = anon->type_params;
        clone->return_type_annotation = anon->return_type_annotation;
        clone->return_type = substitute_type_from_annotation(anon->return_type_annotation, subst);
        for (const auto& p : anon->params) {
            ParamDecl pclone;
            pclone.name = p.name;
            pclone.type_annotation = p.type_annotation;
            pclone.type = substitute_type_from_annotation(p.type_annotation, subst);
            clone->params.push_back(pclone);
        }
        clone->body = clone_stmt(anon->body.get(), subst);
        clone->btype = anon->btype;
        return clone;
    }
    
    return nullptr;
}

std::unique_ptr<Expr> clone_expression(const Expr& original) {
    TypeSubstitution empty;
    return clone_expr(&original, empty);
}

static std::unique_ptr<Stmt> clone_stmt(const Stmt* stmt, const TypeSubstitution& subst) {
    if (!stmt) return nullptr;
    
    if (auto* block = dynamic_cast<const BlockStmt*>(stmt)) {
        auto clone = std::make_unique<BlockStmt>();
        clone->is_declaration_group = block->is_declaration_group;
        for (const auto& s : block->statements) {
            clone->statements.push_back(clone_stmt(s.get(), subst));
        }
        return clone;
    }
    
    if (auto* var = dynamic_cast<const VarDeclStmt*>(stmt)) {
        auto clone = std::make_unique<VarDeclStmt>();
        clone->name = var->name;
        clone->type_ref = substitute_type_ref(var->type_ref, subst);
        clone->type = type_ref_to_btype(clone->type_ref);
        if (clone->type == BType::UNKNOWN) {
            clone->type = substitute_type_from_annotation(var->type_annotation, subst);
        }
        clone->type_annotation = type_ref_to_string(clone->type_ref);
        if (clone->type_annotation == "any" && !var->type_annotation.empty()) {
            clone->type_annotation = var->type_annotation;
        }
        clone->struct_name = clone->type_ref.name.empty() ? var->struct_name : clone->type_ref.name;
        clone->initializer = clone_expr(var->initializer.get(), subst);
        clone->has_constructor_call = var->has_constructor_call;
        for (const auto& argument : var->constructor_args) {
            clone->constructor_args.push_back(clone_expr(argument.get(), subst));
        }
        clone->array_size = clone_expr(var->array_size.get(), subst);
        clone->is_const = var->is_const;
        return clone;
    }

    if (auto* destructure = dynamic_cast<const TupleDestructureStmt*>(stmt)) {
        auto clone = std::make_unique<TupleDestructureStmt>();
        clone->names = destructure->names;
        clone->initializer = clone_expr(destructure->initializer.get(), subst);
        clone->is_const = destructure->is_const;
        return clone;
    }
    
    if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        auto clone = std::make_unique<AssignStmt>();
        clone->name = assign->name;
        clone->assignment_op = assign->assignment_op;
        clone->value = clone_expr(assign->value.get(), subst);
        return clone;
    }
    
    if (auto* arr_assign = dynamic_cast<const ArrayAssignStmt*>(stmt)) {
        auto clone = std::make_unique<ArrayAssignStmt>();
        clone->array_name = arr_assign->array_name;
        clone->index = clone_expr(arr_assign->index.get(), subst);
        clone->assignment_op = arr_assign->assignment_op;
        clone->value = clone_expr(arr_assign->value.get(), subst);
        return clone;
    }

    if (auto* member_assign = dynamic_cast<const MemberAssignStmt*>(stmt)) {
        auto clone = std::make_unique<MemberAssignStmt>();
        clone->lhs = clone_expr(member_assign->lhs.get(), subst);
        clone->assignment_op = member_assign->assignment_op;
        clone->value = clone_expr(member_assign->value.get(), subst);
        return clone;
    }

    if (auto* deref_assign = dynamic_cast<const DerefAssignStmt*>(stmt)) {
        auto clone = std::make_unique<DerefAssignStmt>();
        clone->pointer = clone_expr(deref_assign->pointer.get(), subst);
        clone->assignment_op = deref_assign->assignment_op;
        clone->value = clone_expr(deref_assign->value.get(), subst);
        return clone;
    }
    
    if (auto* ifs = dynamic_cast<const IfStmt*>(stmt)) {
        auto clone = std::make_unique<IfStmt>();
        clone->condition = clone_expr(ifs->condition.get(), subst);
        clone->then_branch = clone_stmt(ifs->then_branch.get(), subst);
        clone->else_branch = clone_stmt(ifs->else_branch.get(), subst);
        return clone;
    }
    
    if (auto* fors = dynamic_cast<const ForStmt*>(stmt)) {
        auto clone = std::make_unique<ForStmt>();
        clone->var_name = fors->var_name;
        clone->var_type = fors->var_type;
        clone->bound = clone_expr(fors->bound.get(), subst);
        clone->body = clone_stmt(fors->body.get(), subst);
        return clone;
    }
    
    if (auto* fws = dynamic_cast<const ForWhileStmt*>(stmt)) {
        auto clone = std::make_unique<ForWhileStmt>();
        clone->condition = clone_expr(fws->condition.get(), subst);
        clone->body = clone_stmt(fws->body.get(), subst);
        return clone;
    }
    
    if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt)) {
        auto clone = std::make_unique<ReturnStmt>();
        clone->value = clone_expr(ret->value.get(), subst);
        return clone;
    }

    if (auto* nodrop = dynamic_cast<const NodropStmt*>(stmt)) {
        auto clone = std::make_unique<NodropStmt>();
        clone->name = nodrop->name;
        return clone;
    }

    if (auto* drop_now = dynamic_cast<const DropNowStmt*>(stmt)) {
        auto clone = std::make_unique<DropNowStmt>();
        clone->value = clone_expr(drop_now->value.get(), subst);
        return clone;
    }
    
    if (auto* brk = dynamic_cast<const BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>();
    }
    
    if (auto* cont = dynamic_cast<const ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>();
    }
    
    if (auto* match = dynamic_cast<const MatchStmt*>(stmt)) {
        auto clone = std::make_unique<MatchStmt>();
        clone->value = clone_expr(match->value.get(), subst);
        for (const auto& c : match->cases) {
            clone->cases.push_back({
                clone_expr(c.first.get(), subst),
                clone_stmt(c.second.get(), subst)
            });
        }
        clone->default_case = clone_stmt(match->default_case.get(), subst);
        return clone;
    }
    
    if (auto* exprs = dynamic_cast<const ExprStmt*>(stmt)) {
        auto clone = std::make_unique<ExprStmt>();
        clone->expression = clone_expr(exprs->expression.get(), subst);
        return clone;
    }
    
    if (auto* ll = dynamic_cast<const LLStmt*>(stmt)) {
        auto clone = std::make_unique<LLStmt>();
        clone->llvm_code = ll->llvm_code;
        return clone;
    }
    
    if (auto* llh = dynamic_cast<const LLHStmt*>(stmt)) {
        auto clone = std::make_unique<LLHStmt>();
        clone->llvm_code = llh->llvm_code;
        return clone;
    }
    
    if (auto* take = dynamic_cast<const TakeStmt*>(stmt)) {
        auto clone = std::make_unique<TakeStmt>();
        clone->path = take->path;
        return clone;
    }
    
    if (auto* plugin = dynamic_cast<const PluginStmt*>(stmt)) {
        auto clone = std::make_unique<PluginStmt>();
        clone->path = plugin->path;
        return clone;
    }
    
    return nullptr;
}

std::unique_ptr<Stmt> clone_statement(const Stmt& original) {
    TypeSubstitution empty;
    return clone_stmt(&original, empty);
}

std::unique_ptr<FnDecl> clone_and_substitute(
    const FnDecl& original,
    const TypeSubstitution& subst,
    const std::string& new_name)
{
    auto clone = std::make_unique<FnDecl>();
    clone->name = new_name;
    clone->type_params = original.type_params;  
    clone->is_method = original.is_method;
    clone->method_owner = original.method_owner;
    clone->method_name = original.method_name;
    clone->is_operator = original.is_operator;
    clone->operator_symbol = original.operator_symbol;
    clone->force_inline = original.force_inline;
    clone->force_noinline = original.force_noinline;
    
    
    for (const auto& p : original.params) {
        ParamDecl pclone;
        pclone.name = p.name;
        pclone.type_ref = substitute_type_ref(p.type_ref, subst);
        pclone.type = type_ref_to_btype(pclone.type_ref);
        if (pclone.type == BType::UNKNOWN) {
            pclone.type = substitute_type_from_annotation(p.type_annotation, subst);
        }
        pclone.type_annotation = type_ref_to_string(pclone.type_ref);
        if (pclone.type_annotation == "any" && !p.type_annotation.empty()) {
            pclone.type_annotation = p.type_annotation;
        }
        pclone.struct_name = pclone.type_ref.name.empty() ? p.struct_name : pclone.type_ref.name;
        clone->params.push_back(pclone);
    }
    
    
    clone->return_type_ref = substitute_type_ref(original.return_type_ref, subst);
    clone->return_type = type_ref_to_btype(clone->return_type_ref);
    if (clone->return_type == BType::UNKNOWN) {
        clone->return_type = substitute_type_from_annotation(original.return_type_annotation, subst);
    }
    clone->return_type_annotation = type_ref_to_string(clone->return_type_ref);
    if (clone->return_type_annotation == "any" && !original.return_type_annotation.empty()) {
        clone->return_type_annotation = original.return_type_annotation;
    }
    
    
    clone->body = clone_stmt(original.body.get(), subst);
    
    return clone;
}

void TemplateRegistry::register_template(const FnDecl& fn) {
    if (fn.type_params.empty()) return;  
    
    templates[fn.name] = &fn;
    template_type_params[fn.name] = fn.type_params;
}

bool TemplateRegistry::is_template(const std::string& name) const {
    return templates.find(name) != templates.end();
}

const std::vector<std::string>* TemplateRegistry::get_type_params(const std::string& name) const {
    auto it = template_type_params.find(name);
    if (it != template_type_params.end()) return &it->second;
    return nullptr;
}

FnDecl* TemplateRegistry::instantiate(const std::string& fn_name, const std::vector<BType>& type_args) {
    std::vector<TypeRef> type_refs;
    type_refs.reserve(type_args.size());
    for (BType type : type_args) {
        TypeRef ref;
        ref.base = type;
        type_refs.push_back(ref);
    }
    return instantiate(fn_name, type_refs);
}

FnDecl* TemplateRegistry::instantiate(const std::string& fn_name,
                                      const std::vector<TypeRef>& type_args) {
    
    auto tmpl_it = templates.find(fn_name);
    if (tmpl_it == templates.end()) {
        gerror("Template not found: " + fn_name + " :/\n");
        return nullptr;
    }
    
    const FnDecl* tmpl = tmpl_it->second;
    
    
    if (type_args.size() != tmpl->type_params.size()) {
        gerror("Template '" + fn_name + "' expects " + std::to_string(tmpl->type_params.size()) 
               + " type arguments, got " + std::to_string(type_args.size()) + " :/\n");
        return nullptr;
    }
    
    
    std::string mangled = mangle_template_name(fn_name, type_args);
    
    
    auto inst_it = instantiations.find(mangled);
    if (inst_it != instantiations.end()) {
        return inst_it->second.get();
    }
    
    
    TypeSubstitution subst;
    for (size_t i = 0; i < type_args.size(); i++) {
        subst.mapping[tmpl->type_params[i]] = type_ref_to_btype(type_args[i]);
        subst.type_ref_mapping[tmpl->type_params[i]] = type_args[i];
    }
    
    
    auto instantiated = clone_and_substitute(*tmpl, subst, mangled);
    FnDecl* result = instantiated.get();
    
    
    instantiations[mangled] = std::move(instantiated);
    
    return result;
}

std::vector<FnDecl*> TemplateRegistry::get_instantiations() const {
    std::vector<FnDecl*> result;
    for (const auto& [name, fn] : instantiations) {
        result.push_back(fn.get());
    }
    return result;
}

std::vector<std::unique_ptr<FnDecl>> TemplateRegistry::take_instantiations() {
    std::vector<std::unique_ptr<FnDecl>> result;
    for (auto& [name, fn] : instantiations) {
        result.push_back(std::move(fn));
    }
    instantiations.clear();
    return result;
}

bool TemplateRegistry::infer_type_args(const std::string& fn_name,
                                        const std::vector<BType>& arg_types,
                                        std::vector<BType>& out_type_args) const {
    auto tmpl_it = templates.find(fn_name);
    if (tmpl_it == templates.end()) return false;
    
    const FnDecl* tmpl = tmpl_it->second;
    
    
    
    out_type_args.clear();
    out_type_args.resize(tmpl->type_params.size(), BType::UNKNOWN);
    
    for (size_t tp_idx = 0; tp_idx < tmpl->type_params.size(); tp_idx++) {
        const std::string& tp_name = tmpl->type_params[tp_idx];
        
        
        for (size_t p_idx = 0; p_idx < tmpl->params.size() && p_idx < arg_types.size(); p_idx++) {
            const ParamDecl& param = tmpl->params[p_idx];
            
            
            if (param.type_annotation == tp_name) {
                
                if (is_array_type(arg_types[p_idx])) {
                    out_type_args[tp_idx] = get_array_elem_type(arg_types[p_idx]);
                } else {
                    out_type_args[tp_idx] = arg_types[p_idx];
                }
                break;
            }
            
            
            if (param.type_annotation == tp_name + "*" || param.type_annotation == tp_name + " *") {
                
                BType base = get_pointer_base_type(arg_types[p_idx]);
                if (base != BType::UNKNOWN) {
                    out_type_args[tp_idx] = base;
                    break;
                }
            }
            
            
            if (param.type_annotation == tp_name + "[]" || param.type_annotation == tp_name + " []") {
                
                BType elem = get_array_elem_type(arg_types[p_idx]);
                if (elem != BType::UNKNOWN) {
                    out_type_args[tp_idx] = elem;
                    break;
                }
            }
        }
    }
    
    
    for (BType t : out_type_args) {
        if (t == BType::UNKNOWN) return false;
    }
    
    return true;
}
