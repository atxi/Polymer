#include "buffer.h"

#include <cstdlib>
#include <cstring>

// TODO: Endianness

#ifdef _MSC_VER
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)
#endif

namespace polymer {

RingBuffer::RingBuffer(MemoryArena& arena, size_t size) {
  this->data = arena.Allocate(size);
  this->size = size;
  this->read_offset = this->write_offset = 0;
}

size_t RingBuffer::GetFreeSize() const {
  return size - write_offset;
}

size_t RingBuffer::GetReadAmount() const {
  if (this->write_offset >= this->read_offset) {
    return this->write_offset - this->read_offset;
  }
  return this->size - this->read_offset + this->write_offset;
}

void RingBuffer::WriteU8(u8 value) {
  this->data[this->write_offset] = value;

  this->write_offset = (this->write_offset + sizeof(value)) % this->size;
}

inline void WriteSplitData(RingBuffer& buffer, size_t remaining, void* value, size_t size) {
  // Write out to the end of the buffer
  for (size_t i = 0; i < remaining; ++i) {
    buffer.data[buffer.write_offset++] = ((u8*)value)[i];
  }

  buffer.write_offset = 0;

  // Write the remaining data to the beginning of the buffer
  for (size_t i = 0; i < size - remaining; ++i) {
    buffer.data[buffer.write_offset++] = ((u8*)value)[i + remaining];
  }
}

inline u64 ReadSplitData(RingBuffer& buffer, size_t remaining, size_t size) {
  char read_buf[16];

  for (size_t i = 0; i < remaining; ++i) {
    read_buf[i] = buffer.data[buffer.read_offset + i];
  }

  buffer.read_offset = 0;

  for (size_t i = 0; i < size - remaining; ++i) {
    read_buf[i + remaining] = buffer.data[buffer.read_offset++];
  }

  return *(u64*)(read_buf);
}

void RingBuffer::WriteU16(u16 value) {
  size_t remaining = this->GetFreeSize();

  value = bswap_16(value);

  if (remaining >= sizeof(value)) {
    *(u16*)(this->data + this->write_offset) = value;
    this->write_offset = (this->write_offset + sizeof(value)) % this->size;
  } else {
    WriteSplitData(*this, remaining, &value, sizeof(value));
  }
}

void RingBuffer::WriteU32(u32 value) {
  size_t remaining = this->GetFreeSize();

  value = bswap_32(value);

  if (remaining >= sizeof(value)) {
    *(u32*)(this->data + this->write_offset) = value;
    this->write_offset = (this->write_offset + sizeof(value)) % this->size;
  } else {
    WriteSplitData(*this, remaining, &value, sizeof(value));
  }
}

void RingBuffer::WriteU64(u64 value) {
  size_t remaining = this->GetFreeSize();

  value = bswap_64(value);

  if (remaining >= sizeof(value)) {
    *(u64*)(this->data + this->write_offset) = value;
    this->write_offset = (this->write_offset + sizeof(value)) % this->size;
  } else {
    WriteSplitData(*this, remaining, &value, sizeof(value));
  }
}

void RingBuffer::WriteVarInt(u64 value) {
  char value_buffer[10];
  size_t index = 0;

  do {
    u8 byte = value & 0x7F;
    value >>= 7;
    if (value) {
      byte |= 0x80;
    }
    value_buffer[index++] = byte;
  } while (value);

  size_t remaining = this->GetFreeSize();

  if (remaining >= index) {
    memcpy(this->data + this->write_offset, value_buffer, index);
    this->write_offset = (this->write_offset + index) % this->size;
  } else {
    for (size_t i = 0; i < remaining; ++i) {
      this->data[this->write_offset++] = value_buffer[i];
    }

    this->write_offset = 0;

    for (size_t i = 0; i < index - remaining; ++i) {
      this->data[write_offset++] = value_buffer[i + remaining];
    }
  }
}

void RingBuffer::WriteFloat(float value) {
  size_t remaining = this->GetFreeSize();

  u32 int_rep = 0;
  std::memcpy(&int_rep, &value, sizeof(value));
  int_rep = bswap_32(int_rep);
  std::memcpy(&value, &int_rep, sizeof(value));

  if (remaining >= sizeof(value)) {
    *(float*)(this->data + this->write_offset) = value;
    this->write_offset = (this->write_offset + sizeof(value)) % this->size;
  } else {
    WriteSplitData(*this, remaining, &value, sizeof(value));
  }
}

void RingBuffer::WriteDouble(double value) {
  size_t remaining = this->GetFreeSize();

  u64 int_rep = 0;
  std::memcpy(&int_rep, &value, sizeof(value));
  int_rep = bswap_64(int_rep);
  std::memcpy(&value, &int_rep, sizeof(value));

  if (remaining >= sizeof(value)) {
    *(double*)(this->data + this->write_offset) = value;
    this->write_offset = (this->write_offset + sizeof(value)) % this->size;
  } else {
    WriteSplitData(*this, remaining, &value, sizeof(value));
  }
}

void RingBuffer::WriteString(sized_string& str) {
  size_t remaining = this->GetFreeSize();

  size_t size = str.size + GetVarIntSize(str.size);

  if (remaining >= size) {
    WriteVarInt(str.size);

    memcpy(this->data + this->write_offset, str.str, str.size);
    this->write_offset = (write_offset + str.size) % this->size;
  } else {
    WriteVarInt(str.size);

    remaining = this->GetFreeSize();
    if (remaining >= str.size) {
      memcpy(this->data + this->write_offset, str.str, str.size);
      this->write_offset = (write_offset + str.size) % this->size;
    } else {
      memcpy(this->data + this->write_offset, str.str, remaining);
      memcpy(this->data, str.str + remaining, str.size - remaining);
      this->write_offset = str.size - remaining;
    }
  }
}

u8 RingBuffer::ReadU8() {
  u8 result = this->data[this->write_offset];

  this->write_offset = (this->write_offset + sizeof(u8)) % this->size;

  return result;
}

u16 RingBuffer::ReadU16() {
  size_t read_remaining = this->size - this->read_offset;
  u16 result = 0;

  if (read_remaining >= sizeof(u16)) {
    result = *(u16*)(this->data + this->read_offset);
    this->read_offset += sizeof(u16);
  } else {
    result = (u16)ReadSplitData(*this, read_remaining, sizeof(u16));
  }

  return bswap_16(result);
}

u32 RingBuffer::ReadU32() {
  size_t read_remaining = this->size - this->read_offset;
  u32 result = 0;

  if (read_remaining >= sizeof(u32)) {
    result = *(u32*)(this->data + this->read_offset);
    this->read_offset += sizeof(u32);
  } else {
    result = (u32)ReadSplitData(*this, read_remaining, sizeof(u32));
  }

  return bswap_32(result);
}

u64 RingBuffer::ReadU64() {
  size_t read_remaining = this->size - this->read_offset;
  u64 result = 0;

  if (read_remaining >= sizeof(u64)) {
    result = *(u64*)(this->data + this->read_offset);
    this->read_offset += sizeof(u64);
  } else {
    result = (u64)ReadSplitData(*this, read_remaining, sizeof(u64));
  }

  return bswap_64(result);
}

bool RingBuffer::ReadVarInt(u64* value) {
  size_t previous_offset = this->read_offset;
  int shift = 0;

  *value = 0;

  u8* buf = this->data;

  do {
    // if write cursor was ahead at start, then exit if read cursor is >= write cursor
    if (this->write_offset > previous_offset && this->read_offset >= this->write_offset) {
      *value = 0;
      this->read_offset = previous_offset;
      return false;
      // if write cursor was behind at start, then exit if the read cursor is >= write cursor and has wrapped
    } else if (this->write_offset <= previous_offset &&
               (this->read_offset >= this->write_offset && this->read_offset < previous_offset)) {
      *value = 0;
      this->read_offset = previous_offset;
      return false;
    }

    if (this->read_offset >= this->size) {
      buf = this->data;
      this->read_offset = 0;
    }

    *value |= (s64)(buf[this->read_offset] & 0x7F) << shift;
    shift += 7;
  } while (buf[this->read_offset++] & 0x80);

  return value;
}

float RingBuffer::ReadFloat() {
  size_t read_remaining = this->size - this->read_offset;
  float result = 0;

  if (read_remaining >= sizeof(float)) {
    result = *(float*)(this->data + this->read_offset);
    this->read_offset += sizeof(float);
  } else {
    for (size_t i = 0; i < read_remaining; ++i) {
      ((u8*)&result)[i] = this->data[this->read_offset + i];
    }

    this->read_offset = 0;

    for (size_t i = 0; i < sizeof(float) - read_remaining; ++i) {
      ((u8*)&result)[i + read_remaining] = this->data[this->read_offset++];
    }
  }

  u32 int_rep = 0;
  memcpy(&int_rep, &result, sizeof(result));
  int_rep = bswap_32(int_rep);
  memcpy(&result, &int_rep, sizeof(result));

  return result;
}

double RingBuffer::ReadDouble() {
  size_t read_remaining = this->size - this->read_offset;
  double result = 0;

  if (read_remaining >= sizeof(double)) {
    result = *(double*)(this->data + this->read_offset);
    this->read_offset += sizeof(double);
  } else {
    for (size_t i = 0; i < read_remaining; ++i) {
      ((u8*)&result)[i] = this->data[this->read_offset + i];
    }

    this->read_offset = 0;

    for (size_t i = 0; i < sizeof(double) - read_remaining; ++i) {
      ((u8*)&result)[i + read_remaining] = this->data[this->read_offset++];
    }
  }

  u64 int_rep = 0;
  memcpy(&int_rep, &result, sizeof(result));
  int_rep = bswap_64(int_rep);
  memcpy(&result, &int_rep, sizeof(result));

  return result;
}

size_t RingBuffer::ReadString(sized_string* str) {
  u64 length = 0;
  size_t offset_snapshot = this->read_offset;

  if (!this->ReadVarInt(&length)) {
    this->read_offset = offset_snapshot;
    return 0;
  }

  if (str->str == nullptr) {
    this->read_offset = offset_snapshot;
    return (size_t)length;
  }

  size_t remaining = this->size - this->read_offset;

  if (remaining > length) {
    remaining = (size_t)length;
  }

  for (size_t i = 0; i < remaining; ++i) {
    str->str[i] = this->data[this->read_offset++];
  }

  this->read_offset = 0;

  for (size_t i = 0; i < (size_t)length - remaining; ++i) {
    str->str[i + remaining] = this->data[this->read_offset++];
  }

  return (size_t)length;
}

size_t GetVarIntSize(u64 value) {
  size_t index = 0;

  do {
    value >>= 7;

    ++index;
  } while (value);

  return index;
}

} // namespace polymer
