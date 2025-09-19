/**
 * @file fixup.c
 * @brief Implementation of the fixup system for deferred resolution tasks.
 *
 * A "fixup" represents a deferred action (e.g., backpatching addresses or resolving
 * references during compilation). The fixup system stores fixups, allows iteration,
 * resolution, and cleanup of all registered fixups.
 *
 * Each fixup has:
 *  - A `fixup_config` (function pointers for fix/end, plus a private pointer).
 *  - A flags field (e.g., FIXUP_FLAG_RESOLVED).
 *  - A pointer back to its owning fixup_system.
 *
 * The system manages a vector of fixups and provides utilities to resolve and free them.
 */

 #include "compiler.h"
 #include <stdlib.h>
 #include <string.h>
 #include "helpers/vector.h"
 
 /**
  * @brief Create and initialize a new fixup system.
  *
  * Allocates memory for a fixup_system and initializes its internal vector of fixups.
  *
  * @return Newly allocated fixup_system, or NULL on allocation failure.
  */
 struct fixup_system* fixup_sys_new()
 {
     struct fixup_system* system = calloc(1, sizeof(struct fixup_system));
     system->fixups = vector_create(sizeof(struct fixup));
     return system;
 }
 
 /**
  * @brief Access the configuration of a given fixup.
  *
  * @param fixup Pointer to the fixup.
  * @return Pointer to the associated fixup_config.
  */
 struct fixup_config* fixup_config(struct fixup* fixup)
 {
     return &fixup->config;
 }
 
 /**
  * @brief Free a single fixup.
  *
  * Calls the fixup's `end` callback, then frees the fixup itself.
  *
  * @param fixup Fixup to free.
  */
 void fixup_free(struct fixup* fixup)
 {
     fixup->config.end(fixup);
     free(fixup);
 }
 
 /**
  * @brief Reset the iteration pointer for fixup traversal.
  *
  * This prepares the system for iterating through fixups using fixup_next().
  *
  * @param system Fixup system.
  */
 void fixup_start_iteration(struct fixup_system* system)
 {
     vector_set_peek_pointer(system->fixups, 0);
 }
 
 /**
  * @brief Get the next fixup in the iteration.
  *
  * @param system Fixup system.
  * @return Pointer to the next fixup, or NULL if none remain.
  */
 struct fixup* fixup_next(struct fixup_system* system)
 {
     return vector_peek_ptr(system->fixups);
 }
 
 /**
  * @brief Free all fixups in a system.
  *
  * Iterates through all registered fixups, calling fixup_free() on each.
  *
  * @param system Fixup system.
  */
 void fixup_sys_fixups_free(struct fixup_system* system)
 {
     fixup_start_iteration(system);
     struct fixup* fixup = fixup_next(system);
     while(fixup)
     {
         fixup_free(fixup);
         fixup = fixup_next(system);
     }
 }
 
 /**
  * @brief Free a fixup system and all contained fixups.
  *
  * Calls fixup_sys_fixups_free() then frees the system itself.
  *
  * @param system Fixup system to free.
  */
 void fixup_sys_free(struct fixup_system* system)
 {
     fixup_sys_fixups_free(system);
     vector_free(system->fixups);
     free(system);
 }
 
 /**
  * @brief Count unresolved fixups in the system.
  *
  * Iterates through all fixups and counts those not marked FIXUP_FLAG_RESOLVED.
  *
  * @param system Fixup system.
  * @return Number of unresolved fixups.
  */
 int fixup_sys_unresolved_fixups_count(struct fixup_system* system)
 {
     size_t c = 0;
     fixup_start_iteration(system);
     struct fixup* fixup = fixup_next(system);
     while(fixup)
     {
         if (!(fixup->flags & FIXUP_FLAG_RESOLVED))
         {
             c++;
         }
         fixup = fixup_next(system);
     }
     return c;
 }
 
 /**
  * @brief Register a new fixup in the system.
  *
  * Allocates a new fixup, copies its config, associates it with the system,
  * and pushes it onto the system's fixup vector.
  *
  * @param system Fixup system.
  * @param config Configuration describing callbacks and private data.
  * @return Newly registered fixup.
  */
 struct fixup* fixup_register(struct fixup_system* system, struct fixup_config* config)
 {
     struct fixup* fixup = calloc(1, sizeof(struct fixup));
     memcpy(&fixup->config, config, sizeof(struct fixup_config));
     fixup->system = system;
     vector_push(system->fixups, &fixup);
     return fixup;
 }
 
 /**
  * @brief Attempt to resolve a single fixup.
  *
  * Calls the fix callback. If successful, marks the fixup as resolved.
  *
  * @param fixup Fixup to resolve.
  * @return true if resolved successfully, false otherwise.
  */
 bool fixup_resolve(struct fixup* fixup)
 {
     if (fixup_config(fixup)->fix(fixup))
     {
         fixup->flags |= FIXUP_FLAG_RESOLVED;
         return true;
     }
     return false;
 }
 
 /**
  * @brief Access the private pointer of a fixup.
  *
  * @param fixup Fixup.
  * @return The private pointer from its config.
  */
 void* fixup_private(struct fixup* fixup)
 {
     return fixup_config(fixup)->private;
 }
 
 /**
  * @brief Attempt to resolve all fixups in the system.
  *
  * Iterates over each fixup and calls fixup_resolve() unless already resolved.
  *
  * @param system Fixup system.
  * @return true if all fixups are resolved, false otherwise.
  */
 bool fixups_resolve(struct fixup_system* system)
 {
     fixup_start_iteration(system);
     struct fixup* fixup = fixup_next(system);
     while(fixup)
     {
         if (!(fixup->flags & FIXUP_FLAG_RESOLVED))
         {
             fixup_resolve(fixup);
         }
         fixup = fixup_next(system);
     }
     return fixup_sys_unresolved_fixups_count(system) == 0;
 }
 