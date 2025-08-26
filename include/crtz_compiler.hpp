// ============================================
// File: include/crtz_compiler.hpp
// Purpose: Public compile API (AST-agnostic facade)
// ============================================
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <variant>


namespace CRTZ
{
// Lightweight token/AST bridge so you can wire your existing parser.
// If you already have AST nodes, adapt them in a translation unit.
struct ChoiceIR { std::string text; std::string targetNode; };
struct LineIR { std::string text; };


struct NodeIR {
std::string name;
std::vector<LineIR> lines; // minimal for now
std::vector<ChoiceIR> choices; // optional
std::string gotoNode; // optional tail goto
};


struct ProgramIR {
std::vector<NodeIR> nodes;
std::string entry = "Start"; // default entry
ProgramIR parse_crtz(const std::string& text);
};


// Compile an IR into bytecode & write to file (.crtzc)
// returns true on success; throws on hard errors.
bool compile_to_file(const ProgramIR& ir, const std::string& outPath);


// Convenience: build IR via callback so you can adapt from existing parser quickly.
using NodeBuilder = std::function<void(NodeIR&)>;
void add_node(ProgramIR& ir, const std::string& name, const NodeBuilder& build);
}