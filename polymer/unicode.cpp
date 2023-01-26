#include <polymer/unicode.h>

#include <polymer/memory.h>

#ifdef _WIN32
#include <Windows.h>
#else
#error Unicode not implemented on this platform.
#endif

namespace polymer {

WString Unicode::FromUTF8(MemoryArena& arena, const String& str) {
  WString result = {};

#ifdef _WIN32
  // Calculate the length of the output wchar string in characters
  result.length = MultiByteToWideChar(CP_UTF8, 0, str.data, (int)str.size, NULL, 0);
  result.data = memory_arena_push_type_count(&arena, wchar, result.length);
  // Write data directly into result wstring
  result.length = MultiByteToWideChar(CP_UTF8, 0, str.data, (int)str.size, (wchar_t*)result.data, (int)result.length);
#endif

  return result;
}

String Unicode::ToUTF8(MemoryArena& arena, const WString& wstr) {
  String result = {};

#ifdef _WIN32
  // Calculate the length of the output utf8 string
  result.size = WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)wstr.data, (int)wstr.length, NULL, 0, NULL, NULL);
  result.data = memory_arena_push_type_count(&arena, char, result.size);
  // Write data directly into result string
  result.size =
      WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)wstr.data, (int)wstr.length, result.data, (int)result.size, NULL, NULL);
#endif

  return result;
}

} // namespace polymer
