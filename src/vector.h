#ifndef POLYMER_VECTOR_H_
#define POLYMER_VECTOR_H_

#include "types.h"

#include <cmath>

namespace polymer {

struct Vector2f {
  union {
    struct {
      float x;
      float y;
    };
    float values[2];
  };

  Vector2f() : x(0), y(0) {}
  Vector2f(float x, float y) : x(x), y(y) {}
  Vector2f(const Vector2f& other) : x(other.x), y(other.y) {}

  Vector2f& operator=(const Vector2f& other) {
    x = other.x;
    y = other.y;

    return *this;
  }

  inline bool operator==(const Vector2f& other) {
    return x == other.x && y == other.y;
  }

  inline bool operator!=(const Vector2f& other) {
    return !(x == other.x && y == other.y);
  }

  inline float& operator[](size_t index) {
    return values[index];
  }

  inline float operator[](size_t index) const {
    return values[index];
  }

  inline Vector2f& operator+=(float value) {
    x += value;
    y += value;
    return *this;
  }

  inline Vector2f& operator-=(float value) {
    x -= value;
    y -= value;
    return *this;
  }

  inline Vector2f& operator+=(const Vector2f& other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  inline Vector2f& operator-=(const Vector2f& other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  inline Vector2f& operator*=(float value) {
    x *= value;
    y *= value;
    return *this;
  }

  inline Vector2f& operator/=(float value) {
    x /= value;
    y /= value;
    return *this;
  }

  inline Vector2f operator+(const Vector2f& other) const {
    return Vector2f(x + other.x, y + other.y);
  }

  inline Vector2f operator-(const Vector2f& other) const {
    return Vector2f(x - other.x, y - other.y);
  }

  inline Vector2f operator-() const {
    return Vector2f(-x, -y);
  }

  inline Vector2f operator*(float value) const {
    return Vector2f(x * value, y * value);
  }

  inline Vector2f operator/(float value) const {
    return Vector2f(x / value, y / value);
  }

  inline float Length() const {
    return std::sqrt(x * x + y * y);
  }

  inline float LengthSq() const {
    return x * x + y * y;
  }

  inline float Distance(const Vector2f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;

    return std::sqrt(dx * dx + dy * dy);
  }

  inline float DistanceSq(const Vector2f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;

    return dx * dx + dy * dy;
  }

  inline float Dot(const Vector2f& other) const {
    return x * other.x + y * other.y;
  }

  inline Vector2f& Normalize() {
    float length = Length();
    if (length > 0) {
      x /= length;
      y /= length;
    }
    return *this;
  }

  inline Vector2f& Truncate(float length) {
    if (LengthSq() > length * length) {
      Normalize();
      *this *= length;
    }
    return *this;
  }

  Vector2f Perpendicular() const {
    return Vector2f(-y, x);
  }
};

inline Vector2f operator*(float value, const Vector2f& v) {
  return Vector2f(v.x * value, v.y * value);
}

inline float Dot(const Vector2f& v1, const Vector2f& v2) {
  return v1.x * v2.x + v1.y * v2.y;
}

inline Vector2f Perpendicular(const Vector2f& v) {
  return Vector2f(-v.y, v.x);
}

inline Vector2f Normalize(const Vector2f& v) {
  float length = v.Length();

  if (length > 0) {
    return Vector2f(v.x / length, v.y / length);
  }

  return v;
}

struct Vector3f {
  union {
    struct {
      float x;
      float y;
      float z;
    };
    float values[3];
  };

  Vector3f() : x(0), y(0), z(0) {}
  Vector3f(float x, float y, float z) : x(x), y(y), z(z) {}
  Vector3f(const Vector2f& v2, float z) : x(v2.x), y(v2.y), z(z) {}
  Vector3f(const Vector3f& other) : x(other.x), y(other.y), z(other.z) {}

  Vector3f& operator=(const Vector3f& other) {
    x = other.x;
    y = other.y;
    z = other.z;

    return *this;
  }

  inline bool operator==(const Vector3f& other) {
    return x == other.x && y == other.y && z == other.z;
  }

  inline bool operator!=(const Vector3f& other) {
    return !(x == other.x && y == other.y && z == other.z);
  }

  inline float& operator[](size_t index) {
    return values[index];
  }

  inline float operator[](size_t index) const {
    return values[index];
  }

  inline Vector3f& operator+=(float value) {
    x += value;
    y += value;
    z += value;
    return *this;
  }

  inline Vector3f& operator-=(float value) {
    x -= value;
    y -= value;
    z -= value;
    return *this;
  }

  inline Vector3f& operator+=(const Vector3f& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }

  inline Vector3f& operator-=(const Vector3f& other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
  }

  inline Vector3f& operator*=(float value) {
    x *= value;
    y *= value;
    z *= value;
    return *this;
  }

  inline Vector3f& operator/=(float value) {
    x /= value;
    y /= value;
    z /= value;
    return *this;
  }

  inline Vector3f operator+(const Vector3f& other) const {
    return Vector3f(x + other.x, y + other.y, z + other.z);
  }

  inline Vector3f operator-(const Vector3f& other) const {
    return Vector3f(x - other.x, y - other.y, z - other.z);
  }

  inline Vector3f operator-() const {
    return Vector3f(-x, -y, -z);
  }

  inline Vector3f operator*(float value) const {
    return Vector3f(x * value, y * value, z * value);
  }

  inline Vector3f operator/(float value) const {
    return Vector3f(x / value, y / value, z / value);
  }

  inline float Length() const {
    return std::sqrt(x * x + y * y + z * z);
  }

  inline float LengthSq() const {
    return x * x + y * y + z * z;
  }

  inline float Distance(const Vector3f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;
    float dz = z - other.z;

    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  inline float DistanceSq(const Vector3f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;
    float dz = y - other.z;

    return dx * dx + dy * dy + dz * dz;
  }

  inline float Dot(const Vector3f& other) const {
    return x * other.x + y * other.y + z * other.z;
  }

  inline Vector3f Cross(const Vector3f& other) const {
    return Vector3f(y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x);
  }

  inline Vector3f& Normalize() {
    float length = Length();
    if (length > 0) {
      x /= length;
      y /= length;
      z /= length;
    }
    return *this;
  }

  inline Vector3f& Truncate(float length) {
    if (LengthSq() > length * length) {
      Normalize();
      *this *= length;
    }
    return *this;
  }
};

inline Vector3f operator*(float value, const Vector3f& v) {
  return Vector3f(v.x * value, v.y * value, v.z * value);
}

inline float Dot(const Vector3f& v1, const Vector3f& v2) {
  return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

inline Vector3f Cross(const Vector3f& v1, const Vector3f& v2) {
  return Vector3f(v1.y * v2.z - v1.z * v2.y, v1.z * v2.x - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
}

inline Vector3f Normalize(const Vector3f& v) {
  float length = v.Length();

  if (length > 0) {
    return Vector3f(v.x / length, v.y / length, v.z / length);
  }

  return v;
}

} // namespace polymer

#endif
