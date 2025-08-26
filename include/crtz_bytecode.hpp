// ============================================
// File: include/crtz_bytecode.hpp
// Purpose: Bytecode format + writer/reader
// ============================================
#pragma once
#include <cstdint>
#include <variant>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

using Constant = std::variant<int, bool, std::string, double>;

namespace CRTZ
{
constexpr uint32_t CRTZ_MAGIC = 0x5A545243; // 'CRTZ' little-endian
constexpr uint16_t CRTZ_BC_VERSION = 1;


enum class Op : uint8_t {
HALT = 0,
// literals & stack
PUSH_CONST, // u32 index into const pool
// variables
LOAD_VAR, // u32 id (resolved at compile or by name index)
STORE_VAR, // u32 id
// output/dialogue
LINE, // u32 string index (may contain ${} which VM substitutes)
CHOICE_ADD, // u32 string index; next u32 target label id
CHOICE_FLUSH, // presents choices and jumps to chosen target
// control flow
JUMP, // u32 label id
JUMP_IF_FALSE, // u32 label id (pops condition from stack)
LABEL, // u32 label id (emitted for debugging; VM skips)
// nodes (high-level blocks)
ENTER_NODE, // u32 node id
LEAVE_NODE, // u32 node id
// comparison ops (push bool)
CMP_EQ, CMP_NEQ, CMP_LT, CMP_LTE, CMP_GT, CMP_GTE,
// arithmetic (push result)
ADD, SUB, MUL, DIV,
// glue
PRINT, // print() builtin; consumes 1 stack item (string/int/bool)
SIGNAL, // u32 string index (signal name)
};


struct FileHeader {
uint32_t magic = CRTZ_MAGIC;
uint16_t version = CRTZ_BC_VERSION;
uint16_t reserved = 0; // align to 8 bytes
uint32_t const_count = 0; // number of strings in const pool
uint32_t code_size = 0; // bytes of code
uint32_t node_count = 0; // nodes for debugger/jump table
uint32_t label_count = 0; // labels for jumps
};

// Utility: insert constant if not already present, return its index
inline uint32_t add_const(std::vector<Constant>& pool, const Constant& c) {
    // naive: linear search
    for (size_t i=0; i<pool.size(); ++i) {
        if (pool[i] == c) return (uint32_t)i;
    }
    pool.push_back(c);
    return (uint32_t)(pool.size() - 1);
}


// Simple bytecode blob
struct Bytecode {
FileHeader header{};
std::vector<Constant> const_pool;
std::vector<uint8_t> code; // instruction stream
std::unordered_map<std::string, uint32_t> node_ids; // name -> id
std::unordered_map<std::string, uint32_t> label_ids; // name -> id
};


// --- Encoding helpers --------------------------------------------------
inline void emit_u8 (std::vector<uint8_t>& out, uint8_t v){ out.push_back(v);}
inline void emit_u32(std::vector<uint8_t>& out, uint32_t v){
out.push_back(uint8_t(v));
out.push_back(uint8_t(v>>8));
out.push_back(uint8_t(v>>16));
out.push_back(uint8_t(v>>24));
}


}