#include "network_queue.h"

#include <curl/curl.h>
#include <polymer/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace polymer {

void NetworkResponse::SaveToFile(const char* filename) {
  FILE* f = CreateAndOpenFile(filename, "wb");

  if (!f) {
    fprintf(stderr, "Failed to open file to save NetworkResponse.\n");
    return;
  }

  NetworkChunk* chunk = this->chunks;
  while (chunk) {
    fwrite(chunk->data, 1, chunk->size, f);
    chunk = chunk->next;
  }

  fclose(f);
}

void NetworkResponse::SaveToFile(String filename) {
  char* z_filename = (char*)malloc(filename.size + 1);
  if (!z_filename) {
    fprintf(stderr, "Failed allocate space for saving NetworkResponse.\n");
    return;
  }

  memcpy(z_filename, filename.data, filename.size);
  z_filename[filename.size] = 0;

  SaveToFile(z_filename);
  free(z_filename);
}

static size_t OnCurlWrite(char* data, size_t n, size_t l, void* userp) {
  NetworkActiveRequest* request = (NetworkActiveRequest*)userp;
  size_t recv_size = n * l;

  if (request->chunks == nullptr) {
    NetworkChunk* chunk = request->queue->AllocateChunk();

    request->chunks = chunk;
    request->last_chunk = chunk;
  }

  size_t remaining_chunk_space = sizeof(NetworkChunk::data) - request->last_chunk->size;

  if (remaining_chunk_space >= recv_size) {
    memcpy(request->last_chunk->data + request->last_chunk->size, data, recv_size);
    request->last_chunk->size += recv_size;
  } else {
    // Write to the end of this chunk
    memcpy(request->last_chunk->data + request->last_chunk->size, data, remaining_chunk_space);
    request->last_chunk->size += remaining_chunk_space;

    size_t written = remaining_chunk_space;
    while (written < recv_size) {
      // Allocate a new chunk and stick it into the active request chunk list.
      NetworkChunk* chunk = request->queue->AllocateChunk();

      request->last_chunk->next = chunk;
      request->last_chunk = chunk;

      // Try to write the entire remaining data.
      size_t write_size = recv_size - written;
      // If the write size is more than this chunk, then cap it to this chunk and loop again to allocate a new one.
      if (write_size > sizeof(NetworkChunk::data)) {
        write_size = sizeof(NetworkChunk::data);
      }

      memcpy(request->last_chunk->data, data + written, write_size);
      request->last_chunk->size += write_size;

      written += write_size;
    }
  }

  request->size += recv_size;

  return recv_size;
}

bool NetworkQueue::Initialize() {
  if (curl_global_init(CURL_GLOBAL_ALL)) {
    fprintf(stderr, "network_queue: Failed to initialize curl.\n");
    return false;
  }

  curl_multi = curl_multi_init();
  if (!curl_multi) {
    fprintf(stderr, "network_queue: Failed to create curl_multi.\n");
    return false;
  }

  if (curl_multi_setopt(curl_multi, CURLMOPT_MAXCONNECTS, kParallelRequests)) {
    fprintf(stderr, "network_queue: Failed to setup curl_multi.\n");
    return false;
  }

  return true;
}

bool NetworkQueue::PushRequest(const char* url, void* userp, NetworkCompleteCallback callback) {
  NetworkRequest* request = nullptr;

  if (!free) {
    free = (NetworkRequest*)malloc(sizeof(NetworkRequest));
    if (!free) {
      fprintf(stderr, "network_queue: Failed to allocate NetworkRequest.\n");
      return false;
    }

    free->next = nullptr;
  }

  request = free;
  free = free->next;

  request->next = nullptr;
  strcpy(request->url, url);
  request->userp = userp;
  request->callback = callback;

  if (waiting_queue_end) {
    waiting_queue_end->next = request;
    waiting_queue_end = request;
  } else {
    waiting_queue = request;
    waiting_queue_end = request;
  }

  return true;
}

void NetworkQueue::Run() {
  ProcessWaitingQueue();

  if (active_request_count == 0) return;

  int still_alive = 1;
  curl_multi_perform(curl_multi, &still_alive);

  CURLMsg* msg = nullptr;

  int msg_count = 0;

  while ((msg = curl_multi_info_read(curl_multi, &msg_count))) {
    if (msg->msg == CURLMSG_DONE) {
      NetworkActiveRequest* active_request = nullptr;

      CURL* e = msg->easy_handle;
      long http_code = 0;

      curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &active_request);
      curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);

      if (active_request) {
        NetworkResponse response;

        response.http_code = (int)http_code;
        response.transfer_code = msg->data.result;
        response.size = active_request->size;
        response.chunks = active_request->chunks;

        active_request->request->callback(active_request->request, &response);

        NetworkChunk* chunk = response.chunks;
        while (chunk) {
          NetworkChunk* current = chunk;
          chunk = chunk->next;

          FreeChunk(current);
        }
      }

      curl_multi_remove_handle(curl_multi, e);
      curl_easy_cleanup(e);
      --active_request_count;
    } else {
      fprintf(stderr, "E: CURLMsg (%d)\n", msg->msg);
    }
  }
}

void NetworkQueue::ProcessWaitingQueue() {
  // Loop over active requests looking for an empty spot for the waiting request.
  // It will stop looping when the waiting queue is empty.
  for (size_t i = 0; waiting_queue && i < polymer_array_count(active_requests); ++i) {
    if (!active_requests[i].active) {
      active_requests[i].queue = this;
      active_requests[i].request = waiting_queue;
      active_requests[i].active = true;
      active_requests[i].size = 0;
      active_requests[i].chunks = active_requests[i].last_chunk = nullptr;

      CURL* eh = curl_easy_init();

      long enable = 1;

      curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, OnCurlWrite);
      curl_easy_setopt(eh, CURLOPT_URL, waiting_queue->url);
      curl_easy_setopt(eh, CURLOPT_PRIVATE, active_requests + i);
      curl_easy_setopt(eh, CURLOPT_WRITEDATA, active_requests + i);
      curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, &enable);

      curl_multi_add_handle(curl_multi, eh);

      if (waiting_queue == waiting_queue_end) {
        waiting_queue_end = nullptr;
      }

      waiting_queue = waiting_queue->next;
      ++active_request_count;
    }
  }
}

inline void FreeRequests(NetworkRequest* request) {
  while (request) {
    NetworkRequest* current = request;

    request = request->next;

    ::free(current);
  }
}

void NetworkQueue::Clear() {
  FreeRequests(free);
  FreeRequests(waiting_queue);

  free = nullptr;
  waiting_queue = nullptr;
  waiting_queue_end = nullptr;

  NetworkChunk* chunk = free_chunks;
  while (chunk) {
    NetworkChunk* current = chunk;

    chunk = chunk->next;

    ::free(current);
  }

  free_chunks = nullptr;
}

bool NetworkQueue::IsEmpty() const {
  return active_request_count == 0 && !waiting_queue;
}

NetworkChunk* NetworkQueue::AllocateChunk() {
  if (!free_chunks) {
    NetworkChunk* chunk = (NetworkChunk*)malloc(sizeof(NetworkChunk));
    if (chunk) {
      chunk->size = 0;
      chunk->next = nullptr;
    } else {
      fprintf(stderr, "network_queue: Failed to allocate NetworkChunk.\n");
      exit(1);
    }

    free_chunks = chunk;
  }

  NetworkChunk* result = free_chunks;

  free_chunks = free_chunks->next;

  return result;
}

void NetworkQueue::FreeChunk(NetworkChunk* chunk) {
  chunk->next = free_chunks;
  free_chunks = chunk;
}

} // namespace polymer
