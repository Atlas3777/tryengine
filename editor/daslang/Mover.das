options gen2

module Mover shared

require math
require daslib/math_boost
require tryengine/Input
require tryengine/Types
require daslib/decs
require daslib/decs_boost

let sens = 0.33f
let cam_speed = 3f

def Move(dt : float) {
    query() <| $(var transform : Transform; camera : Camera) {
        let md = mouse_delta()

        let front_raw = float3(0.0, 1.0, 0.0)
        let right_raw = float3(1.0, 0.0, 0.0)
        let up_raw    = float3(0.0, 0.0, 1.0)

        if (is_mouse_down(Mouse.Right)) {
            // Yaw поворачивает вокруг мировой вертикали +Z
            let qYaw = quat_from_unit_vec_ang(up_raw, radians(-md.x * sens))

            // Pitch поворачивает вокруг локальной оси Right
            let right_dir = quat_mul_vec(transform.rotation, right_raw)
            let qPitch = quat_from_unit_vec_ang(right_dir, radians(-md.y * sens))

            transform.rotation = normalize(quat_mul(quat_mul(qYaw, qPitch), transform.rotation))
        }

        let front = quat_mul_vec(transform.rotation, front_raw)
        let right = quat_mul_vec(transform.rotation, right_raw)
        let up    = quat_mul_vec(transform.rotation, up_raw)

        if (is_mouse_down(Mouse.Right)) {
            let moveSpeed = cam_speed * dt

            if (is_down(Key.W)) { transform.position += front * moveSpeed }
            if (is_down(Key.S)) { transform.position -= front * moveSpeed }
            if (is_down(Key.A)) { transform.position -= right * moveSpeed }
            if (is_down(Key.D)) { transform.position += right * moveSpeed }
            if (is_down(Key.E)) { transform.position += up * moveSpeed }
            if (is_down(Key.Q)) { transform.position -= up * moveSpeed }
        }
    }
}