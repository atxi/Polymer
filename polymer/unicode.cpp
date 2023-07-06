#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <polymer/unicode.h>

#include <polymer/memory.h>

#include <codecvt>
#include <iostream>
#include <locale>
#include <string.h>
#include <string>

std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

namespace polymer {

WString Unicode::FromUTF8(MemoryArena& arena, const String& str) {
  WString result = {};

  char* utf8 = memory_arena_push_type_count(&arena, char, str.size + 1);
  memcpy(utf8, str.data, str.size);
  utf8[str.size] = 0;

  std::wstring utf16 = converter.from_bytes(utf8);

  result.length = utf16.length();
  result.data = memory_arena_push_type_count(&arena, wchar, result.length);

#ifdef _WIN32
  for (size_t i = 0; i < result.length; ++i) {
    wchar c = (wchar)utf16[i];
    result.data[i] = c;
  }
#else
  memcpy(result.data, utf16.data(), result.length * sizeof(wchar_t));
#endif

  return result;
}

String Unicode::ToUTF8(MemoryArena& arena, const WString& wstr) {
  String result = {};

  wchar_t* wide_data = (wchar_t*)memory_arena_push_type_count(&arena, wchar_t, wstr.length + 1);
  size_t wide_data_size = wstr.length * sizeof(wchar_t);

#ifdef _WIN32
  for (size_t i = 0; i < wstr.length; ++i) {
    wchar_t c = (wchar_t)wstr.data[i];
    wide_data[i] = c;
  }
#else
  memcpy((void*)wide_data, (void*)wstr.data, wide_data_size);
#endif
  wide_data[wstr.length] = 0;

  std::string utf8 = converter.to_bytes(wide_data);

  result.size = utf8.size();
  result.data = memory_arena_push_type_count(&arena, char, result.size);

  memcpy(result.data, utf8.data(), utf8.size());

  return result;
}

} // namespace polymer
