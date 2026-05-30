// codegen_toplevel.cpp — Top-level AST node code generation (globals, functions).

#include "codegen.hpp"

void LLVMCodegen::gen_top(struct node *n)
{
    if (!n) return;
    switch (n->type)
    {
    case NODE_TYPE_VARIABLE:
        gen_global(n);
        break;
    case NODE_TYPE_VARIABLE_LIST:
    {
        vector_set_peek_pointer(n->var_list.list, 0);
        for (;;)
        {
            auto *vn = static_cast<struct node *>(
                vector_peek_ptr(n->var_list.list));
            if (!vn) break;
            gen_global(vn);
        }
        break;
    }
    case NODE_TYPE_FUNCTION:
        gen_func(n);
        break;
    default:
        break;
    }
}

void LLVMCodegen::gen_global(struct node *n)
{
    if (!n || n->type != NODE_TYPE_VARIABLE || !n->var.name) return;
    if (m_module->getGlobalVariable(n->var.name)) return;

    llvm::Type *ty = dtype_to_llvm(&n->var.type);
    if (!ty || ty->isVoidTy()) return;

    llvm::Constant *init = global_init(n->var.val, ty);
    new llvm::GlobalVariable(*m_module, ty, /*isConst=*/false,
                              llvm::GlobalValue::ExternalLinkage, init,
                              n->var.name);
}

void LLVMCodegen::gen_func(struct node *n)
{
    if (!n || n->type != NODE_TYPE_FUNCTION || !n->func.name) return;

    llvm::Type *ret_ty = dtype_to_llvm(&n->func.rtype);
    std::vector<llvm::Type *> params;
    bool variadic = false;

    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a) break;
            if (a->type == NODE_TYPE_VARIABLE)
                params.push_back(dtype_to_llvm(&a->var.type));
        }
    }

    if (!n->func.body_n) variadic = true;

    llvm::FunctionType *ft = llvm::FunctionType::get(ret_ty, params, variadic);
    llvm::Function *fn = m_module->getFunction(n->func.name);
    if (!fn)
        fn = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage,
                                    n->func.name, m_module.get());

    if (!n->func.body_n) return;

    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        auto it = fn->arg_begin();
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a || it == fn->arg_end()) break;
            if (a->type == NODE_TYPE_VARIABLE && a->var.name)
                it->setName(a->var.name);
            ++it;
        }
    }

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(*m_ctx, "entry", fn);
    m_builder->SetInsertPoint(entry);

    push_scope();
    m_labels.clear();
    m_pending_gotos.clear();

    if (n->func.args.vector)
    {
        vector_set_peek_pointer(n->func.args.vector, 0);
        auto it = fn->arg_begin();
        for (;;)
        {
            auto *a = static_cast<struct node *>(
                vector_peek_ptr(n->func.args.vector));
            if (!a || it == fn->arg_end()) break;
            if (a->type == NODE_TYPE_VARIABLE && a->var.name)
            {
                llvm::Type *pty = it->getType();
                llvm::AllocaInst *slot = entry_alloca(fn, a->var.name, pty);
                m_builder->CreateStore(&*it, slot);
                define_var(a->var.name, slot, pty);
            }
            ++it;
        }
    }

    gen_body(n->func.body_n);
    resolve_gotos();
    pop_scope();

    if (!is_terminated())
    {
        if (ret_ty->isVoidTy())
            m_builder->CreateRetVoid();
        else
            m_builder->CreateRet(llvm::Constant::getNullValue(ret_ty));
    }
}
