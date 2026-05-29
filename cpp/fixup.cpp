#include "compiler.hpp"
#include "helpers/vector.hpp"
#include <cstdlib>
#include <cstring>

// Allocates and initialises a fresh fixup_system with an empty fixup vector.
// Returns the new system; the caller owns it and must eventually call
// fixup_sys_free() to release it.
struct fixup_system *fixup_sys_new()
{
    struct fixup_system *system =
        static_cast<struct fixup_system *>(calloc(1, sizeof(struct fixup_system)));
    system->fixups = vector_create(sizeof(struct fixup));
    return system;
}

// Returns a pointer to the fixup_config embedded inside the given fixup.
// Provides a stable accessor so callers do not reach into the struct directly.
// fixup  — the fixup whose configuration is requested.
struct fixup_config *fixup_config(struct fixup *fixup)
{
    return &fixup->config;
}

// Runs the fixup's end() cleanup callback, then frees the fixup allocation.
// The end() callback is responsible for releasing any private data stored in
// the config before the fixup itself is freed.
// fixup  — the fixup to destroy; must not be used after this call.
void fixup_free(struct fixup *fixup)
{
    fixup->config.end(fixup);
    free(fixup);
}

// Resets the iteration cursor of the fixup vector to the beginning so that a
// subsequent sequence of fixup_next() calls visits every fixup from the first.
// system  — the fixup system whose iteration state is reset.
void fixup_start_iteration(struct fixup_system *system)
{
    vector_set_peek_pointer(system->fixups, 0);
}

// Returns the next fixup in the current iteration sequence, advancing the
// cursor by one position.  Returns nullptr when all fixups have been visited.
// Must be preceded by a call to fixup_start_iteration() before the first call
// in any iteration loop.
// system  — the fixup system being iterated.
struct fixup *fixup_next(struct fixup_system *system)
{
    return static_cast<struct fixup *>(vector_peek_ptr(system->fixups));
}

// Iterates every fixup in the system and frees each one via fixup_free().
// After this call the fixups vector is empty but the system itself still exists.
// system  — the fixup system whose individual fixup objects are freed.
void fixup_sys_fixups_free(struct fixup_system *system)
{
    fixup_start_iteration(system);
    struct fixup *fixup = fixup_next(system);
    while (fixup)
    {
        fixup_free(fixup);
        fixup = fixup_next(system);
    }
}

// Frees all fixups stored in the system, then frees the fixups vector and the
// system allocation itself.  After this call the pointer must not be used.
// system  — the fixup system to fully tear down.
void fixup_sys_free(struct fixup_system *system)
{
    fixup_sys_fixups_free(system);
    vector_free(system->fixups);
    free(system);
}

// Counts the number of fixups that have not yet been resolved (i.e. that do not
// carry the FIXUP_FLAG_RESOLVED flag).
// Returns the count as an int; a return value of 0 means all fixups are done.
// system  — the fixup system to inspect.
int fixup_sys_unresolved_fixups_count(struct fixup_system *system)
{
    size_t c = 0;
    fixup_start_iteration(system);
    struct fixup *fixup = fixup_next(system);
    while (fixup)
    {
        if (!(fixup->flags & FIXUP_FLAG_RESOLVED))
            c++;
        fixup = fixup_next(system);
    }
    return c;
}

// Allocates a new fixup, copies the supplied config into it, links it back to
// its owning system, and appends it to the system's fixups vector.
// Returns a pointer to the newly registered fixup.
// system  — the fixup system that will own this fixup.
// config  — configuration struct containing the fix and end callbacks together
//           with any private data the callbacks need.
struct fixup *fixup_register(struct fixup_system *system, struct fixup_config *config)
{
    struct fixup *fixup =
        static_cast<struct fixup *>(calloc(1, sizeof(struct fixup)));
    memcpy(&fixup->config, config, sizeof(struct fixup_config));
    fixup->system = system;
    vector_push(system->fixups, &fixup);
    return fixup;
}

// Attempts to resolve a single fixup by invoking its fix() callback.
// If the callback returns true the fixup is marked FIXUP_FLAG_RESOLVED.
// Returns true if the fixup was successfully resolved, false otherwise.
// fixup  — the fixup to attempt to resolve.
bool fixup_resolve(struct fixup *fixup)
{
    if (fixup_config(fixup)->fix(fixup))
    {
        fixup->flags |= FIXUP_FLAG_RESOLVED;
        return true;
    }
    return false;
}

// Returns the private data pointer stored in the fixup's config.
// This is a convenience wrapper so callbacks can retrieve their context without
// reaching into the config struct manually.
// fixup  — the fixup whose private data is requested.
void *fixup_private(struct fixup *fixup)
{
    return fixup_config(fixup)->private_data;
}

// Iterates every fixup in the system and attempts to resolve each one that has
// not yet been resolved by calling fixup_resolve().
// Called after the full file has been parsed so that all forward references that
// accumulated during parsing can be patched in a single sweep.
// Returns true if every fixup in the system is now resolved (i.e. none remain
// outstanding), false if at least one fixup could still not be resolved.
// system  — the fixup system to sweep.
bool fixups_resolve(struct fixup_system *system)
{
    fixup_start_iteration(system);
    struct fixup *fixup = fixup_next(system);
    while (fixup)
    {
        if (!(fixup->flags & FIXUP_FLAG_RESOLVED))
            fixup_resolve(fixup);
        fixup = fixup_next(system);
    }
    return fixup_sys_unresolved_fixups_count(system) == 0;
}
