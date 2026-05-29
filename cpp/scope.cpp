#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdlib>
#include <cassert>

// Allocates and initialises a bare scope with an empty entities vector.
// The vector is configured to iterate newest-first (peek starts at the end and
// decrements) so that name lookups find the most recently pushed declaration
// first.  Returns the newly allocated scope; ownership is managed by the
// compile_process scope stack.
static struct scope *scope_alloc()
{
    struct scope *s = static_cast<struct scope *>(calloc(1, sizeof(struct scope)));
    s->entities = vector_create(sizeof(void *));
    vector_set_peek_pointer_end(s->entities);
    vector_set_flag(s->entities, VECTOR_FLAG_PEEK_DECREMENT);
    return s;
}

// Placeholder cleanup for a scope object.
// Memory is reclaimed at process teardown rather than incrementally, so this
// function intentionally does nothing.  The parameter is unnamed to silence
// unused-parameter warnings.
// scope  — the scope to clean up (currently unused).
static void scope_dealloc(struct scope * /*scope*/)
{
    // placeholder — cleanup done at process level
}

// Creates and installs the root (global) scope for a compile process.
// Asserts that no root or current scope exists yet, ensuring this is called
// exactly once per compilation.  The new scope becomes both the root and the
// active current scope.
// Returns the newly created root scope.
// process  — the compile process that receives the root scope.
struct scope *scope_create_root(struct compile_process *process)
{
    assert(!process->scope.root);
    assert(!process->scope.current);

    struct scope *root = scope_alloc();
    process->scope.root    = root;
    process->scope.current = root;
    return root;
}

// Tears down the root scope of a compile process and clears both the root and
// current scope pointers, leaving the process in a scope-free state.
// process  — the compile process whose root scope is released.
void scope_free_root(struct compile_process *process)
{
    scope_dealloc(process->scope.root);
    process->scope.root    = nullptr;
    process->scope.current = nullptr;
}

// Pushes a new child scope onto the scope stack, making it the active current
// scope.  The new scope's parent pointer is set to the previously current scope.
// Asserts that a root scope already exists (scope_create_root must have been
// called first).
// Returns the newly created child scope.
// process  — the compile process whose scope stack is extended.
// flags    — bitmask of SCOPE_* flags to attach to the new scope (e.g. to mark
//            a function boundary).
struct scope *scope_new(struct compile_process *process, int flags)
{
    assert(process->scope.root);
    assert(process->scope.current);

    struct scope *s = scope_alloc();
    s->flags  = flags;
    s->parent = process->scope.current;
    process->scope.current = s;
    return s;
}

// Resets the iteration cursor of a scope's entities vector so that the next
// call to scope_iterate_back() returns the newest (most recently pushed) entity.
// Because the vector uses VECTOR_FLAG_PEEK_DECREMENT, "start" means the end of
// the underlying array.
// scope  — the scope whose iteration cursor is reset.
void scope_iteration_start(struct scope *scope)
{
    vector_set_peek_pointer(scope->entities, 0);
    if (scope->entities->flags & VECTOR_FLAG_PEEK_DECREMENT)
        vector_set_peek_pointer_end(scope->entities);
}

// Marks the end of an iteration sequence over a scope's entities.
// Currently a no-op; exists as a symmetric counterpart to scope_iteration_start()
// so that callers bracket their loops clearly.
// scope  — the scope whose iteration has finished (parameter unused).
void scope_iteration_end(struct scope * /*scope*/)
{
}

// Returns the next entity when iterating a scope from newest to oldest, and
// advances the cursor.  Returns nullptr when the scope is empty or all entities
// have been visited.  scope_iteration_start() must be called before the first
// call in any iteration loop.
// scope  — the scope being iterated.
void *scope_iterate_back(struct scope *scope)
{
    if (vector_count(scope->entities) == 0)
        return nullptr;
    return vector_peek_ptr(scope->entities);
}

// Returns the most recently pushed entity in the given scope without modifying
// the iteration cursor.  Returns nullptr if the scope has no entities.
// This inspects only the scope itself, not any parent scopes.
// scope  — the scope to query.
void *scope_last_entity_at_scope(struct scope *scope)
{
    if (vector_count(scope->entities) == 0)
        return nullptr;
    return vector_back_ptr(scope->entities);
}

// Walks up the scope parent chain starting from scope looking for the most
// recently pushed entity, stopping before stop_scope (exclusive upper bound).
// Returns nullptr if scope equals stop_scope, if no entity is found before
// stop_scope is reached, or if the chain has no more parents.
// Used to search for the last entity up to but not past a particular scope
// boundary (e.g. a function scope).
// scope       — the innermost scope to start searching from.
// stop_scope  — the scope at which the upward walk halts (not searched).
void *scope_last_entity_from_scope_stop_at(struct scope *scope, struct scope *stop_scope)
{
    if (scope == stop_scope)
        return nullptr;

    void *last = scope_last_entity_at_scope(scope);
    if (last)
        return last;

    struct scope *parent = scope->parent;
    if (parent)
        return scope_last_entity_from_scope_stop_at(parent, stop_scope);

    return nullptr;
}

// Searches from the current innermost scope upward for the most recently pushed
// entity, stopping before stop_scope.  Delegates to
// scope_last_entity_from_scope_stop_at() starting at process->scope.current.
// Returns nullptr if no entity is found before stop_scope.
// process     — the compile process providing the current scope.
// stop_scope  — the scope boundary that terminates the upward search.
void *scope_last_entity_stop_at(struct compile_process *process, struct scope *stop_scope)
{
    return scope_last_entity_from_scope_stop_at(process->scope.current, stop_scope);
}

// Returns the most recently pushed entity visible from the current scope,
// searching all the way up to the root with no stopping boundary.
// Returns nullptr if no entities exist anywhere in the scope chain.
// process  — the compile process whose full scope chain is searched.
void *scope_last_entity(struct compile_process *process)
{
    return scope_last_entity_stop_at(process, nullptr);
}

// Pushes a pointer-sized entity onto the current scope's entities vector and
// adds elem_size to the scope's accumulated byte count.
// The entity is stored as a raw pointer so the scope does not need to know the
// concrete type of the object being tracked.
// process    — the compile process whose current scope receives the entity.
// ptr        — the entity pointer to store (e.g. a struct node *).
// elem_size  — the byte size that this entity contributes to the scope's total
//              (used for stack-frame offset accounting).
void scope_push(struct compile_process *process, void *ptr, size_t elem_size)
{
    vector_push(process->scope.current->entities, &ptr);
    process->scope.current->size += elem_size;
}

// Pops the current scope off the stack, restoring the parent as the active
// scope.  If popping the last child scope causes current to become nullptr while
// root still exists, root is also cleared to keep the two pointers consistent.
// process  — the compile process whose scope stack is popped.
void scope_finish(struct compile_process *process)
{
    struct scope *new_current = process->scope.current->parent;
    scope_dealloc(process->scope.current);
    process->scope.current = new_current;
    if (process->scope.root && !process->scope.current)
        process->scope.root = nullptr;
}

// Returns the currently active (innermost) scope of the compile process.
// process  — the compile process to query.
struct scope *scope_current(struct compile_process *process)
{
    return process->scope.current;
}
