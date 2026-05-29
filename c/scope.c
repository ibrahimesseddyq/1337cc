/**
 * @file scope.c
 * @brief Scope management utilities for the compiler frontend.
 *
 * This module implements a simple hierarchical scope system used by the parser
 * to register entities (variables, labels, etc.) and to compute per-scope sizes.
 *
 * Design notes:
 *  - Each scope holds a vector of entity pointers (`entities`) that represents
 *    the items declared in that scope.
 *  - The vector used for `entities` is configured to support peek-from-end
 *    iteration semantics by setting VECTOR_FLAG_PEEK_DECREMENT and using
 *    vector_set_peek_pointer_end() at allocation time.
 *  - `scope->size` accumulates the byte-size of entities pushed into the scope.
 *
 * The module intentionally keeps deallocation logic minimal (scope_dealloc is
 * currently a no-op) — free/cleanup responsibilities may be handled elsewhere
 * (e.g., process teardown) depending on the larger project's memory model.
 */

 #include "compiler.h"
 #include "helpers/vector.h"
 #include <memory.h>
 #include <stdlib.h>
 #include <assert.h>
 
 /**
  * @brief Allocate and initialize an empty scope object.
  *
  * The created scope has an `entities` vector prepared for end-to-front iteration:
  *  - peek pointer is set to the vector end
  *  - VECTOR_FLAG_PEEK_DECREMENT is set so successive peeks decrement the index
  *
  * @return pointer to a newly allocated struct scope (zero-initialized).
  */
 struct scope* scope_alloc()
 {
     struct scope* scope = calloc(1, sizeof(struct scope));
     scope->entities = vector_create(sizeof(void*));
     vector_set_peek_pointer_end(scope->entities);
     vector_set_flag(scope->entities, VECTOR_FLAG_PEEK_DECREMENT);
     return scope;
 }
 
 /**
  * @brief Deallocate resources held by a scope.
  *
  * Currently a placeholder (no-op). If scope-owned memory (e.g., entity objects)
  * later needs freeing here, implement it in this function.
  *
  * @param scope Scope to deallocate (may be NULL).
  */
 void scope_dealloc(struct scope* scope)
 {
     // Do nothing for now.
 }
 
 /**
  * @brief Create the root scope for a compilation process.
  *
  * Asserts that the process has no existing root/current scope, allocates a root
  * scope and sets it as both root and current scope.
  *
  * @param process Compile process for which to create the root scope.
  * @return pointer to the created root scope.
  */
 struct scope* scope_create_root(struct compile_process* process)
 {
     assert(!process->scope.root);
     assert(!process->scope.current);
 
     struct scope* root_scope = scope_alloc();
     process->scope.root = root_scope;
     process->scope.current = root_scope;
     return root_scope;
 }
 
 /**
  * @brief Free the root scope of the given compile process.
  *
  * Calls scope_dealloc on the root and clears process references to the root
  * and current scope.
  *
  * @param process Compile process whose root scope will be freed.
  */
 void scope_free_root(struct compile_process* process)
 {
     scope_dealloc(process->scope.root);
     process->scope.root = NULL;
     process->scope.current = NULL;
 }
 
 /**
  * @brief Create a new child scope and make it the current scope.
  *
  * The new scope's parent is set to the previous current scope and the provided
  * flags are stored on the scope.
  *
  * @param process Compile process owning the scope hierarchy.
  * @param flags   Flags to assign to the new scope (semantic meaning is project-specific).
  * @return pointer to the newly created scope.
  */
 struct scope* scope_new(struct compile_process* process, int flags)
 {
     assert(process->scope.root);
     assert(process->scope.current);
 
     struct scope* new_scope = scope_alloc();
     new_scope->flags = flags;
     new_scope->parent = process->scope.current;
     process->scope.current = new_scope;
     return new_scope;
 }
 
 /**
  * @brief Prepare for iterating entities in a scope.
  *
  * Resets the peek pointer for the scope->entities vector to the iteration start.
  * If the vector was configured for decrementing peek (end-to-front), the peek
  * pointer is set to the end.
  *
  * @param scope Scope whose entity vector iteration is being started.
  */
 void scope_iteration_start(struct scope* scope)
 {
     vector_set_peek_pointer(scope->entities, 0);
     if (scope->entities->flags & VECTOR_FLAG_PEEK_DECREMENT)
     {
         vector_set_peek_pointer_end(scope->entities);
     }
 }
 
 /**
  * @brief End an iteration session for the scope.
  *
  * Currently a placeholder. Exists for API symmetry with scope_iteration_start()
  * and for future needs (e.g., restore peek pointer, checks).
  *
  * @param scope Scope whose iteration is ending.
  */
 void scope_iteration_end(struct scope* scope)
 {
 
 }
 
 /**
  * @brief Peek the next entity when iterating the scope from the back.
  *
  * Returns NULL when the scope has no entities.
  *
  * @param scope Scope to query.
  * @return Pointer to the entity at the current peek position or NULL.
  */
 void* scope_iterate_back(struct scope* scope)
 {
     if (vector_count(scope->entities) == 0)
         return NULL;
 
     return vector_peek_ptr(scope->entities);
 }
 
 /**
  * @brief Get the last (most recently pushed) entity in this scope only.
  *
  * Does not walk parent scopes. Returns NULL if the scope contains no entities.
  *
  * @param scope Scope to query.
  * @return Pointer to the last entity or NULL.
  */
 void* scope_last_entity_at_scope(struct scope* scope)
 {
      if (vector_count(scope->entities) == 0)
         return NULL;
 
     return vector_back_ptr(scope->entities); 
 }
 
 /**
  * @brief Find the last entity from `scope` but stop searching at `stop_scope`.
  *
  * Walks up parent scopes until either an entity is found or `stop_scope` is reached.
  * If `scope == stop_scope`, search stops immediately and NULL is returned.
  *
  * @param scope     Starting scope for the search.
  * @param stop_scope Scope at which to stop searching (may be NULL to search to top).
  * @return Pointer to the found entity or NULL if none found before stop_scope.
  */
 void* scope_last_entity_from_scope_stop_at(struct scope* scope, struct scope* stop_scope)
 {
     if (scope == stop_scope)
     {
         return NULL;
     }
 
     void* last = scope_last_entity_at_scope(scope);
     if (last)
     {
         return last;
     }
 
     struct scope* parent = scope->parent;
     if (parent)
     {
         return scope_last_entity_from_scope_stop_at(parent, stop_scope);
     }
 
     return NULL;
 }
 
 /**
  * @brief Find the last entity in the current process scope chain, stopping at stop_scope.
  *
  * Convenience wrapper that begins searching from process->scope.current.
  *
  * @param process   Compile process whose current scope is used.
  * @param stop_scope Scope to stop at (may be NULL).
  * @return Pointer to the found entity or NULL.
  */
 void* scope_last_entity_stop_at(struct compile_process* process, struct scope* stop_scope)
 {
     return scope_last_entity_from_scope_stop_at(process->scope.current, stop_scope);
 }
 
 /**
  * @brief Return the last entity visible in the current scope chain.
  *
  * Wrapper around scope_last_entity_stop_at with stop_scope == NULL (search to root).
  *
  * @param process Compile process.
  * @return Pointer to the last found entity or NULL.
  */
 void* scope_last_entity(struct compile_process* process)
 {
    return scope_last_entity_stop_at(process, NULL);
 }
 
 /**
  * @brief Push a new entity pointer into the current scope and account its size.
  *
  * The entity pointer is pushed into the current scope's entities vector and the
  * scope's `size` field is incremented by elem_size (used to compute stack or
  * aggregate sizes).
  *
  * @param process   Compile process containing the current scope.
  * @param ptr       Pointer to the entity to register (stored by address in the vector).
  * @param elem_size Size in bytes contributed by this entity (added to scope->size).
  */
 void scope_push(struct compile_process* process, void* ptr, size_t elem_size)
 {
     vector_push(process->scope.current->entities, &ptr);
     process->scope.current->size += elem_size;
 }
 
 /**
  * @brief Finish the current scope and return to its parent.
  *
  * Deallocates the current scope (placeholder in scope_dealloc) and sets the
  * process current scope to the parent. If root was cleared, also clears the root.
  *
  * @param process Compile process whose current scope will be finished.
  */
 void scope_finish(struct compile_process* process)
 {
     struct scope* new_current_scope = process->scope.current->parent;
     scope_dealloc(process->scope.current);
     process->scope.current = new_current_scope;
     if (process->scope.root && !process->scope.current)
     {
         process->scope.root = NULL;
     }
 }
 
 /**
  * @brief Return the current scope of the given compile process.
  *
  * @param process Compile process.
  * @return Pointer to the current scope (may be NULL).
  */
 struct scope* scope_current(struct compile_process* process)
 {
     return process->scope.current;
 }
 