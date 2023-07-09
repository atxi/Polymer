#pragma once

#include <polymer/types.h>

namespace polymer {

struct NetworkChunk {
  struct NetworkChunk* next;
  u8 data[2048];
  size_t size;
};

struct NetworkResponse {
  int http_code;
  int transfer_code;

  size_t size;
  NetworkChunk* chunks = nullptr;

  void SaveToFile(const char* filename);
  void SaveToFile(String filename);
};

using NetworkCompleteCallback = void (*)(struct NetworkRequest* request, NetworkResponse* response);

struct NetworkRequest {
  struct NetworkRequest* next;

  char url[2048];
  void* userp;
  NetworkCompleteCallback callback;
};

struct NetworkActiveRequest {
  struct NetworkQueue* queue = nullptr;
  NetworkRequest* request = nullptr;
  bool active = false;
  size_t size = 0;

  NetworkChunk* chunks = nullptr;
  NetworkChunk* last_chunk = nullptr;
};

struct NetworkQueue {
  constexpr static size_t kParallelRequests = 10;

  bool Initialize();

  bool PushRequest(const char* url, void* userp, NetworkCompleteCallback callback);
  void Run();
  void Clear();
  bool IsEmpty() const;

  NetworkChunk* AllocateChunk();
  void FreeChunk(NetworkChunk* chunk);

private:
  void ProcessWaitingQueue();

  void* curl_multi;
  NetworkActiveRequest active_requests[kParallelRequests];

  int active_request_count = 0;
  NetworkRequest* waiting_queue = nullptr;
  NetworkRequest* waiting_queue_end = nullptr;

  NetworkRequest* free = nullptr;
  NetworkChunk* free_chunks = nullptr;
};

} // namespace polymer
