#pragma once
#include <string>
namespace CRTZ {
// Load a .crtzc file and execute it using your existing runtime systems.
// You can implement this by extending your interpreter to read bytecode
// instead of parsing source at runtime.
bool runProgramBytecode(const std::string& path);
}