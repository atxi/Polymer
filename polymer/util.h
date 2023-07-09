#pragma once

#include <polymer/types.h>
#include <stdio.h>

namespace polymer {

struct MemoryArena;

String ReadEntireFile(const char* filename, MemoryArena& arena);

// Creates all the necessary folders and opens a FILE handle.
FILE* CreateAndOpenFile(String filename, const char* mode);
// Creates all the necessary folders and opens a FILE handle.
FILE* CreateAndOpenFile(const char* filename, const char* mode);

} // namespace polymer
