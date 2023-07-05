#include <polymer/unicode.h>

#include <polymer/memory.h>

#include <locale>
#include <codecvt>
#include <string>
#include <string.h>
#include <iostream>

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
  
  memcpy(result.data, utf16.data(), result.length * sizeof(wchar_t));

  return result;
}

String Unicode::ToUTF8(MemoryArena& arena, const WString& wstr) {
  String result = {};

  wchar_t* wide_data = (wchar_t*)memory_arena_push_type_count(&arena, wchar_t, wstr.length + 1);
  size_t wide_data_size = wstr.length * sizeof(wchar_t);

  memcpy((void*)wide_data, (void*)wstr.data, wide_data_size);
  wide_data[wstr.length] = 0;

  std::string utf8 = converter.to_bytes(wide_data);

  result.size = utf8.size();
  result.data = memory_arena_push_type_count(&arena, char, result.size);

  memcpy(result.data, utf8.data(), utf8.size());

  return result;
}

} // namespace polymer
