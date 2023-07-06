#pragma once

#include <polymer/types.h>

#include <stdio.h>
#include <stdlib.h>

namespace polymer {

struct ArgPair {
  String name;
  String value;
};

struct ArgParser {
  ArgPair args[16];
  size_t arg_count;

  inline bool HasValue(const String& name) const {
    for (size_t i = 0; i < arg_count; ++i) {
      if (poly_strcmp(name, args[i].name) == 0) {
        return true;
      }
    }

    return false;
  }

  inline bool HasValue(const String* lookups, size_t count) const {
    for (size_t i = 0; i < count; ++i) {
      bool result = HasValue(lookups[i]);

      if (result) {
        return true;
      }
    }

    return false;
  }

  inline String GetValue(const String& name) const {
    for (size_t i = 0; i < arg_count; ++i) {
      if (poly_strcmp(name, args[i].name) == 0) {
        return args[i].value;
      }
    }

    return String();
  }

  inline String GetValue(const String* lookups, size_t count) const {
    for (size_t i = 0; i < count; ++i) {
      String result = GetValue(lookups[i]);

      if (result.size > 0) {
        return result;
      }
    }

    return String();
  }

  static ArgParser Parse(int argc, char* argv[]) {
    ArgParser result = {};

    for (int i = 0; i < argc && i < polymer_array_count(args); ++i) {
      char* current = argv[i];

      if (current[0] == '-') {
        ++current;

        if (*current == '-') ++current;

        ArgPair& pair = result.args[result.arg_count++];

        pair.name = String(current);
        pair.value = String();

        if (i < argc - 1) {
          char* next = argv[i + 1];

          if (*next != '-') {
            pair.value = String(argv[i + 1]);
            ++i;
          }
        }
      }
    }

    return result;
  }
};

struct LaunchArgs {
  String username;
  String server;
  u16 server_port;
  bool help;

  static LaunchArgs Create(ArgParser& args) {
    const String kUsernameArgs[] = {POLY_STR("username"), POLY_STR("user"), POLY_STR("u")};
    const String kServerArgs[] = {POLY_STR("server"), POLY_STR("s")};
    const String kHelpArgs[] = {POLY_STR("help"), POLY_STR("h")};

    constexpr const char* kDefaultServerIp = "127.0.0.1";
    constexpr u16 kDefaultServerPort = 25565;

    constexpr const char* kDefaultUsername = "polymer";
    constexpr size_t kMaxUsernameSize = 16;

    LaunchArgs result = {};

    result.username = args.GetValue(kUsernameArgs, polymer_array_count(kUsernameArgs));
    result.server = args.GetValue(kServerArgs, polymer_array_count(kServerArgs));

    if (result.username.size == 0) {
      result.username = String((char*)kDefaultUsername);
    }

    if (result.username.size > kMaxUsernameSize) {
      result.username.size = kMaxUsernameSize;
    }

    result.server_port = kDefaultServerPort;

    if (result.server.size == 0) {
      result.server = String((char*)kDefaultServerIp);
    } else {
      for (size_t i = 0; i < result.server.size; ++i) {
        if (result.server.data[i] == ':') {
          char* port_str = result.server.data + i + 1;

          result.server_port = (u16)strtol(port_str, nullptr, 10);
          result.server.size = i;
          result.server.data[i] = 0;
          break;
        }
      }
    }

    result.help = args.HasValue(kHelpArgs, polymer_array_count(kHelpArgs));

    return result;
  }
};

inline void PrintUsage() {
  printf("Polymer\n\n");
  printf("Usage:\n\tpolymer.exe [OPTIONS]\n\n");
  printf("OPTIONS:\n");
  printf("\t-u, --user, --username\tOffline username. Default: polymer\n");
  printf("\t-s, --server\t\tDirect server. Default: 127.0.0.1:25565\n");
}

} // namespace polymer
