// runtime/build/dep_file.cpp -- Make-style dependency file parsing
#include "dep_file.hpp"

#include "ir.hpp"
#include "../cache/key.hpp"
#include "../../platform/interface/fs.hpp"

#include <unordered_set>

namespace rivet::build {

namespace {

// Splice line continuations (`\` followed by `\n`) into single logical
// lines. clang's output is one logical line per .o:
//
//     out/foo.o: src/foo.cpp \
//       include/bar.h
//
// becomes
//
//     out/foo.o: src/foo.cpp   include/bar.h
//
// Returns the joined text. CRLF is normalised to LF first so Windows
// .d files (which clang emits on every platform via libc++ headers)
// parse identically.
std::string join_continuations(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        char c = text[i];
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            out.push_back('\n');
            i += 2;
            continue;
        }
        if (c == '\\' && i + 1 < text.size() && text[i + 1] == '\n') {
            // backslash-newline -> single space (per GNU make)
            out.push_back(' ');
            i += 2;
            continue;
        }
        if (c == '\\' && i + 2 < text.size()
            && text[i + 1] == '\r' && text[i + 2] == '\n') {
            out.push_back(' ');
            i += 3;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// Tokenise a logical-line string into whitespace-separated tokens.
// Handles `\ ` (escaped space inside a path) by emitting a literal space
// in the current token. Empty tokens are dropped.
std::vector<std::string> tokenise(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < line.size() && line[i + 1] == ' ') {
            cur.push_back(' ');
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

} // namespace

void augment_inputs_with_dep_file(TaskNode& node, const Path& dep_file) {
    auto deps = parse_dep_file(dep_file);
    if (deps.empty()) return;

    std::unordered_set<std::string> already_hashed;
    for (const auto& in : node.inputs)
        already_hashed.insert(in.path.generic_string());

    for (const auto& h : deps) {
        std::string norm = h.generic_string();
        if (!already_hashed.insert(norm).second) continue;
        auto sh = cache::sha256_file(h);
        if (!sh) continue;  // header may have moved; tolerate.
        InputFile inp;
        inp.path         = h;
        inp.content_hash = std::move(*sh);
        node.inputs.push_back(std::move(inp));
    }
}

std::vector<Path> parse_dep_file(const Path& dep_file) {
    if (!rivet::fs::exists(dep_file).value_or(false)) return {};
    auto data = rivet::fs::read_file(dep_file);
    if (!data) return {};

    std::string_view raw(
        reinterpret_cast<const char*>(data->data()), data->size());
    std::string joined = join_continuations(raw);

    std::vector<Path> deps;
    // Each remaining `\n` separates one logical rule. clang emits one rule
    // per .d file, but handle the multi-rule case defensively.
    size_t pos = 0;
    while (pos < joined.size()) {
        size_t nl = joined.find('\n', pos);
        std::string_view line(joined.data() + pos,
            (nl == std::string::npos ? joined.size() : nl) - pos);
        pos = (nl == std::string::npos ? joined.size() : nl + 1);

        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;

        // Everything after `:` is the prerequisite list.
        auto preq = line.substr(colon + 1);
        for (auto& tok : tokenise(preq)) {
            if (tok.empty() || tok == "\\") continue;
            deps.emplace_back(std::move(tok));
        }
    }
    return deps;
}

} // namespace rivet::build
