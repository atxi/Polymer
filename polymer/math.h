#ifndef POLYMER_MATH_H_
#define POLYMER_MATH_H_

#include <polymer/types.h>

#include <math.h>
#include <string.h>
#include <xmmintrin.h>

constexpr float kPi = 3.14159265f;

namespace polymer {

inline constexpr float Radians(float degrees) {
  constexpr float kDegreeToRadians = kPi / 180.0f;
  return degrees * kDegreeToRadians;
}

inline constexpr float Degrees(float radians) {
  constexpr float kRadianToDegrees = 180.0f / kPi;
  return radians * kRadianToDegrees;
}

template <typename T>
inline constexpr T Clamp(const T& first, const T& top) {
  return first > top ? top : first;
}

template <typename T>
inline constexpr T Clamp(const T& first, const T& bottom, const T& top) {
  T result = first;
  if (result < bottom) result = bottom;
  if (result > top) result = top;
  return result;
}

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
    return sqrtf(x * x + y * y);
  }

  inline float LengthSq() const {
    return x * x + y * y;
  }

  inline float Distance(const Vector2f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;

    return sqrtf(dx * dx + dy * dy);
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
    return sqrtf(x * x + y * y + z * z);
  }

  inline float LengthSq() const {
    return x * x + y * y + z * z;
  }

  inline float Distance(const Vector3f& other) const {
    float dx = x - other.x;
    float dy = y - other.y;
    float dz = z - other.z;

    return sqrtf(dx * dx + dy * dy + dz * dz);
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

struct Vector4f {
  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    float values[4];
  };

  Vector4f() : x(0), y(0), z(0), w(0) {}
  Vector4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
  Vector4f(const Vector2f& v2, float z, float w) : x(v2.x), y(v2.y), z(z), w(w) {}
  Vector4f(const Vector3f& other, float w) : x(other.x), y(other.y), z(other.z), w(w) {}

  Vector4f& operator=(const Vector4f& other) {
    x = other.x;
    y = other.y;
    z = other.z;
    w = other.w;

    return *this;
  }

  inline bool operator==(const Vector4f& other) {
    return x == other.x && y == other.y && z == other.z && w == other.w;
  }

  inline bool operator!=(const Vector4f& other) {
    return !(x == other.x && y == other.y && z == other.z && w == other.w);
  }

  inline float& operator[](size_t index) {
    return values[index];
  }

  inline float operator[](size_t index) const {
    return values[index];
  }

  inline Vector4f operator*(float value) {
    return Vector4f(x * value, y * value, z * value, w * value);
  }

  inline Vector4f operator+(const Vector4f& other) {
    return Vector4f(x + other.x, y + other.y, z + other.z, w + other.w);
  }
};

// Column major matrix4x4
struct mat4 {
  float data[4][4];

  mat4() : data{} {}
  mat4(float v) : data{} {
    for (size_t i = 0; i < 4; ++i) {
      data[i][i] = v;
    }
  }
  mat4(float* data) {
    memcpy(this->data, data, sizeof(mat4));
  }

  float* operator[](size_t index) {
    return data[index];
  }

  const float* operator[](size_t index) const {
    return data[index];
  }

  Vector4f Multiply(const Vector3f& v, float w) {
    Vector4f result;

    for (size_t row = 0; row < 4; ++row) {
      result[row] = v.x * data[0][row] + v.y * data[1][row] + v.z * data[2][row] + w * data[3][row];
    }

    return result;
  }

  // Constructs the matrix from column vectors.
  // The columns are for operational order not for internal order (column-major stored)
  static mat4 FromColumns(const Vector4f& x, const Vector4f& y, const Vector4f& z, const Vector4f& w) {
    mat4 result;

    for (size_t i = 0; i < 4; ++i) {
      result[0][i] = x[i];
      result[1][i] = y[i];
      result[2][i] = z[i];
      result[3][i] = w[i];
    }

    return result;
  }

  // Constructs the matrix from row vectors.
  // The rows are for operational order not for internal order (column-major stored)
  static mat4 FromRows(const Vector4f& x, const Vector4f& y, const Vector4f& z, const Vector4f& w) {
    mat4 result;

    for (size_t i = 0; i < 4; ++i) {
      result[i][0] = x[i];
      result[i][1] = y[i];
      result[i][2] = z[i];
      result[i][3] = w[i];
    }

    return result;
  }

  static mat4 Identity() {
    mat4 result = {};

    result.data[0][0] = 1;
    result.data[1][1] = 1;
    result.data[2][2] = 1;
    result.data[3][3] = 1;

    return result;
  }
};

inline mat4 LookAt(const Vector3f& eye, const Vector3f& to, Vector3f world_up = Vector3f(0, 1, 0)) {
  // Compute camera axes
  Vector3f forward = Normalize(to - eye);
  Vector3f side = Normalize(forward.Cross(world_up));
  Vector3f up = Normalize(side.Cross(forward));

  // Insert camera axes in column major order and transform eye into the camera space for translation
  float values[] = {side.x, up.x, -forward.x, 0, side.y,          up.y,          -forward.y,        0,
                    side.z, up.z, -forward.z, 0, -Dot(side, eye), -Dot(up, eye), Dot(forward, eye), 1};

  return mat4(values);
}

inline mat4 Translate(const mat4& M, const Vector3f& translation) {
  float values[] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, translation.x, translation.y, translation.z, 1,
  };

  return mat4((float*)values);
}

// fov: field of view for y-axis
// aspect_ratio: width / height
// near: near plane in camera space
// far: far plane in camera space
inline mat4 Perspective(float fov, float aspect_ratio, float near, float far) {
  float half_tan = tanf(fov / 2.0f);

  float values[] = {
      1.0f / (aspect_ratio * half_tan),
      0,
      0,
      0,
      0,
      -1.0f / half_tan,
      0,
      0,
      0,
      0,
      -(far + near) / (far - near),
      -1.0f,
      0,
      0,
      -(2.0f * far * near) / (far - near),
      0,
  };

  return mat4(values);
}

inline mat4 Orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane) {
  float values[] = {
      2.0f / (right - left),
      0,
      0,
      0,
      0,
      2 / (top - bottom),
      0,
      0,
      0,
      0,
      -2.0f / (far_plane - near_plane),
      0,
      -(right + left) / (right - left),
      -(top + bottom) / (top - bottom),
      -(far_plane + near_plane) / (far_plane - near_plane),
      1,
  };

  return mat4(values);
}

inline Vector3f Rotate(const Vector3f& v, float rads, const Vector3f& rotate_axis) {
  float c = cosf(rads);
  float s = sinf(rads);

  Vector3f axis = Normalize(rotate_axis);
  Vector3f t = (1.0f - c) * axis;

  float rotator[3][3];

  rotator[0][0] = c + t[0] * axis[0];
  rotator[0][1] = t[0] * axis[1] + s * axis[2];
  rotator[0][2] = t[0] * axis[2] - s * axis[1];

  rotator[1][0] = t[1] * axis[0] - s * axis[2];
  rotator[1][1] = c + t[1] * axis[1];
  rotator[1][2] = t[1] * axis[2] + s * axis[0];

  rotator[2][0] = t[2] * axis[0] + s * axis[1];
  rotator[2][1] = t[2] * axis[1] - s * axis[0];
  rotator[2][2] = c + t[2] * axis[2];

  float x = v[0] * rotator[0][0] + v[1] * rotator[1][0] + v[2] * rotator[2][0];
  float y = v[0] * rotator[0][1] + v[1] * rotator[1][1] + v[2] * rotator[2][1];
  float z = v[0] * rotator[0][2] + v[1] * rotator[1][2] + v[2] * rotator[2][2];

  return Vector3f(x, y, z);
}

inline mat4 Rotate(const mat4& M, float angle, const Vector3f& rotate_axis) {
  float c = cosf(angle);
  float s = sinf(angle);

  Vector3f axis = Normalize(rotate_axis);
  Vector3f t = (1.0f - c) * axis;

  float rotator[3][3];

  rotator[0][0] = c + t[0] * axis[0];
  rotator[0][1] = t[0] * axis[1] + s * axis[2];
  rotator[0][2] = t[0] * axis[2] - s * axis[1];

  rotator[1][0] = t[1] * axis[0] - s * axis[2];
  rotator[1][1] = c + t[1] * axis[1];
  rotator[1][2] = t[1] * axis[2] + s * axis[0];

  rotator[2][0] = t[2] * axis[0] + s * axis[1];
  rotator[2][1] = t[2] * axis[1] - s * axis[0];
  rotator[2][2] = c + t[2] * axis[2];

  Vector4f M0(M[0][0], M[0][1], M[0][2], M[0][3]);
  Vector4f M1(M[1][0], M[1][1], M[1][2], M[1][3]);
  Vector4f M2(M[2][0], M[2][1], M[2][2], M[2][3]);

  Vector4f R0 = M0 * rotator[0][0] + M1 * rotator[0][1] + M2 * rotator[0][2];
  Vector4f R1 = M0 * rotator[1][0] + M1 * rotator[1][1] + M2 * rotator[1][2];
  Vector4f R2 = M0 * rotator[2][0] + M1 * rotator[2][1] + M2 * rotator[2][2];

  float values[] = {R0[0], R0[1], R0[2], R0[3], R1[0],   R1[1],   R1[2],   R2[3],
                    R2[0], R2[1], R2[2], R1[3], M[3][0], M[3][1], M[3][2], M[3][3]};

  return mat4((float*)values);
}

inline Vector4f operator*(const mat4& M, const Vector4f& v) {
  Vector4f result;

  for (size_t row = 0; row < 4; ++row) {
    result[row] = v.x * M.data[0][row] + v.y * M.data[1][row] + v.z * M.data[2][row] + v.w * M.data[3][row];
  }

  return result;
}

inline mat4 operator*(const mat4& M1, const mat4& M2) {
  mat4 result = {};

  for (size_t col = 0; col < 4; ++col) {
    for (size_t row = 0; row < 4; ++row) {
      for (size_t i = 0; i < 4; ++i) {
        result.data[col][row] += M1.data[i][row] * M2.data[col][i];
      }
    }
  }

  return result;
}

struct Plane {
  Vector3f normal;
  float distance;

  Plane() {}
  Plane(const Vector3f normal, float distance) : normal(normal), distance(distance) {}
  Plane(const Vector3f& p1, const Vector3f& p2, const Vector3f& p3) {
    normal = Normalize(Cross(p2 - p1, p3 - p1));
    distance = normal.Dot(p1);
  }

  inline float PointDistance(const Vector3f& v) {
    return (normal.Dot(v) - distance) / normal.Dot(normal);
  }
};

struct Frustum {
  Vector3f position;
  Vector3f forward;

  float near;
  float near_width;
  float near_height;

  float far;
  float far_width;
  float far_height;

  Plane planes[6];

  Frustum(const Vector3f& position, const Vector3f& forward, float near, float far, float fov, float ratio,
          const Vector3f& up, const Vector3f& right)
      : position(position), forward(forward), near(near), far(far) {
    near_height = 2 * tanf(fov / 2.0f) * near;
    near_width = near_height * ratio;

    far_height = 2 * tanf(fov / 2.0f) * far;
    far_width = far_height * ratio;

    Vector3f nc = position + forward * near;
    Vector3f fc = position + forward * far;

    float hnh = near_height / 2.0f;
    float hnw = near_width / 2.0f;
    float hfh = far_height / 2.0f;
    float hfw = far_width / 2.0f;

    Vector3f ntl = nc + up * hnh - right * hnw;
    Vector3f ntr = nc + up * hnh + right * hnw;
    Vector3f nbl = nc - up * hnh - right * hnw;
    Vector3f nbr = nc - up * hnh + right * hnw;

    Vector3f ftl = fc + up * hfh - right * hfw;
    Vector3f ftr = fc + up * hfh + right * hfw;
    Vector3f fbl = fc - up * hfh - right * hfw;
    Vector3f fbr = fc - up * hfh + right * hfw;

    planes[0] = Plane(ntr, ntl, ftl);
    planes[1] = Plane(nbl, nbr, fbr);
    planes[2] = Plane(ntl, nbl, fbl);
    planes[3] = Plane(nbr, ntr, fbr);
    planes[4] = Plane(ntl, ntr, nbr);
    planes[5] = Plane(ftr, ftl, fbl);
  }

  inline bool Intersects(const Vector3f& min, const Vector3f& max) {
    Vector3f diff = max - min;
    Vector3f vertices[8] = {min,
                            min + Vector3f(diff.x, 0, 0),
                            min + Vector3f(diff.x, diff.y, 0),
                            min + Vector3f(0, diff.y, 0),
                            min + Vector3f(0, diff.y, diff.z),
                            min + Vector3f(0, 0, diff.z),
                            min + Vector3f(diff.x, 0, diff.z),
                            max};

#if 0
    for (size_t i = 0; i < 6; ++i) {
      bool in = false;

      for (size_t k = 0; k < 8; ++k) {
        if (planes[i].normal.Dot(vertices[k]) - planes[i].distance >= 0) {
          in = true;
          break;
        }
      }

      if (!in) {
        return false;
      }
    }

    return true;
#else
    __m128 zero4x = _mm_set_ps1(0.0f);

    __m128 vxs0 = _mm_setr_ps(vertices[0].x, vertices[1].x, vertices[2].x, vertices[3].x);
    __m128 vxs1 = _mm_setr_ps(vertices[4].x, vertices[5].x, vertices[6].x, vertices[7].x);

    __m128 vys0 = _mm_setr_ps(vertices[0].y, vertices[1].y, vertices[2].y, vertices[3].y);
    __m128 vys1 = _mm_setr_ps(vertices[4].y, vertices[5].y, vertices[6].y, vertices[7].y);

    __m128 vzs0 = _mm_setr_ps(vertices[0].z, vertices[1].z, vertices[2].z, vertices[3].z);
    __m128 vzs1 = _mm_setr_ps(vertices[4].z, vertices[5].z, vertices[6].z, vertices[7].z);

    for (size_t i = 0; i < 6; ++i) {
      __m128 nxs = _mm_set_ps1(planes[i].normal.x);
      __m128 nys = _mm_set_ps1(planes[i].normal.y);
      __m128 nzs = _mm_set_ps1(planes[i].normal.z);

      __m128 dist = _mm_set_ps1(planes[i].distance);

      __m128 xmul0 = _mm_mul_ps(nxs, vxs0);
      __m128 xmul1 = _mm_mul_ps(nxs, vxs1);

      __m128 ymul0 = _mm_mul_ps(nys, vys0);
      __m128 ymul1 = _mm_mul_ps(nys, vys1);

      __m128 zmul0 = _mm_mul_ps(nzs, vzs0);
      __m128 zmul1 = _mm_mul_ps(nzs, vzs1);

      __m128 dot0 = _mm_add_ps(_mm_add_ps(xmul0, ymul0), zmul0);
      __m128 dot1 = _mm_add_ps(_mm_add_ps(xmul1, ymul1), zmul1);
      __m128 final0 = _mm_sub_ps(dot0, dist);
      __m128 final1 = _mm_sub_ps(dot1, dist);

      __m128 cmp0 = _mm_cmplt_ps(final0, zero4x);
      __m128 cmp1 = _mm_cmplt_ps(final1, zero4x);

      if (_mm_movemask_ps(cmp0) == 0x0F && _mm_movemask_ps(cmp1) == 0x0F) {
        return false;
      }
    }

    return true;
#endif
  }
};

} // namespace polymer

#endif
