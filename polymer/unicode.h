#ifndef POLYMER_UNICODE_H_
#define POLYMER_UNICODE_H_

#include <polymer/types.h>

namespace polymer {

struct MemoryArena;

struct Unicode {
  static WString FromUTF8(MemoryArena& arena, const String& str);
  static String ToUTF8(MemoryArena& arena, const WString& wstr);
};

} // namespace polymer

#endif
