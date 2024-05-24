#ifndef POLYMER_CAMERA_H_
#define POLYMER_CAMERA_H_

#include <polymer/math.h>

namespace polymer {

struct Camera {
  Vector3f position;
  float yaw;
  float pitch;

  float fov;
  float aspect_ratio;
  float near;
  float far;

  inline Vector3f GetForward() {
    return Vector3f(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));
  }

  inline mat4 GetViewMatrix() {
    static const Vector3f kWorldUp(0, 1, 0);

    Vector3f front(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));

    return LookAt(Vector3f(0, 0, 0), front, kWorldUp);
  }

  inline mat4 GetProjectionMatrix() {
    return Perspective(fov, aspect_ratio, near, far);
  }

  inline Frustum GetViewFrustum() {
    static const Vector3f kWorldUp(0, 1, 0);

    Vector3f front(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));

    Vector3f forward = Normalize(front);
    Vector3f side = Normalize(forward.Cross(kWorldUp));
    Vector3f up = Normalize(side.Cross(forward));

    return Frustum(position, forward, near, far, fov, aspect_ratio, up, side);
  }
};

} // namespace polymer

#endif
