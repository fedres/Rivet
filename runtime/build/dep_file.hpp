// runtime/build/dep_file.hpp -- Make-style dependency file parsing
//
// clang/gcc emit a .d file alongside each .o when invoked with -MD -MF:
//
//     out/foo.o: src/foo.cpp \
//       include/bar.h \
//       include/baz.h
//
// Rivet uses these to fold transitive header dependencies into compile-
// cache keys (D1 -- input hashing). Without this, a header edit doesn't
// invalidate the .o cache and stale objects survive across builds.
//
// Same format ninja consumes via its built-in `deps = gcc` mode.
#pragma once

#include "../../platform/interface/types.hpp"

#include <vector>

namespace rivet::build {

/// Parse a make-style dependency file. Returns the list of prerequisite
/// paths (the target itself -- before `:` -- is dropped). Line continuations
/// (`\` at EOL) are handled. Escaped spaces (`\ `) inside paths are decoded.
///
/// On a missing or unreadable file, returns an empty list -- the caller
/// treats "no dep file yet" the same as "no transitive deps known," which
/// is the right behaviour for first-build compile cache misses.
[[nodiscard]] std::vector<Path> parse_dep_file(const Path& dep_file);

struct TaskNode;  // build/ir.hpp

/// D1 graph-construction helper. If `dep_file` exists (i.e. a previous
/// build's compile already populated it), parse it, hash every prerequisite
/// not already in `node.inputs`, and append to `node.inputs` so a subsequent
/// `derive_key()` factors transitive headers into the cache key.
///
/// No-op when the file is missing -- first builds don't have a .d yet.
void augment_inputs_with_dep_file(TaskNode& node, const Path& dep_file);

} // namespace rivet::build
