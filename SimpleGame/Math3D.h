#pragma once
#include <algorithm>
#include <cmath>

struct Vec3
{
    float x = 0, y = 0, z = 0;

    Vec3 operator+(Vec3 b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }

    Vec3 operator-(Vec3 b) const
    {
        return {x - b.x, y - b.y, z - b.z};
    }

    Vec3 operator*(float s) const
    {
        return {x * s, y * s, z * s};
    }
};

inline float Dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 Normalize(Vec3 v)
{
    return v * (1.f / std::max(.00001f, std::sqrt(Dot(v, v))));
}

// Column-major matrices, column vectors, right-handed world, Y up.
struct Mat4
{
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    Mat4 operator*(const Mat4& b) const
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
        {
            for (int row = 0; row < 4; ++row)
            {
                r.m[c * 4 + row] = 0;
                for (int k = 0; k < 4; ++k)
                {
                    r.m[c * 4 + row] += m[k * 4 + row] * b.m[c * 4 + k];
                }
            }
        }
        return r;
    }

    Vec3 Transform(Vec3 p) const
    {
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        return {(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w,
                (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w,
                (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w};
    }

    static Mat4 Translation(Vec3 p)
    {
        Mat4 r;
        r.m[12] = p.x;
        r.m[13] = p.y;
        r.m[14] = p.z;
        return r;
    }

    static Mat4 Scale(Vec3 p)
    {
        Mat4 r;
        r.m[0] = p.x;
        r.m[5] = p.y;
        r.m[10] = p.z;
        return r;
    }

    static Mat4 Rotation(Vec3 axis, float angle)
    {
        axis = Normalize(axis);
        float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        float x = axis.x, y = axis.y, z = axis.z;
        Mat4 r;
        r.m[0] = c + x * x * t;
        r.m[4] = x * y * t - z * s;
        r.m[8] = x * z * t + y * s;
        r.m[1] = y * x * t + z * s;
        r.m[5] = c + y * y * t;
        r.m[9] = y * z * t - x * s;
        r.m[2] = z * x * t - y * s;
        r.m[6] = z * y * t + x * s;
        r.m[10] = c + z * z * t;
        return r;
    }

    static Mat4 Ortho(float w, float h, float nearPlane, float farPlane)
    {
        Mat4 r;
        r.m[0] = 2 / w;
        r.m[5] = 2 / h;
        r.m[10] = -2 / (farPlane - nearPlane);
        r.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return r;
    }

    static Mat4 LookAt(Vec3 eye, Vec3 target)
    {
        const Vec3 f = Normalize(target - eye), s = Normalize(Cross(f, {0, 1, 0})), u = Cross(s, f);
        Mat4 r;
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = s.z;
        r.m[12] = -Dot(s, eye);
        r.m[1] = u.x;
        r.m[5] = u.y;
        r.m[9] = u.z;
        r.m[13] = -Dot(u, eye);
        r.m[2] = -f.x;
        r.m[6] = -f.y;
        r.m[10] = -f.z;
        r.m[14] = Dot(f, eye);
        return r;
    }
};

struct Camera3D
{
    Vec3 target, eye;
    Mat4 viewProjection;
    float pixelsPerUnit = 42;
    int width = 1, height = 1;

    void Update(Vec3 focus, int w, int h)
    {
        target = focus;
        width = std::max(1, w);
        height = std::max(1, h);
        eye = target + Vec3{32, 36, 32};
        viewProjection = Mat4::Ortho(width / pixelsPerUnit, height / pixelsPerUnit, .1f, 160) *
                         Mat4::LookAt(eye, target);
    }

    Vec3 Project(Vec3 p) const
    {
        Vec3 q = viewProjection.Transform(p);
        return {(q.x + 1) * width * .5f, (1 - q.y) * height * .5f, q.z};
    }

    void ProjectBounds(Vec3 minimum, Vec3 maximum, Vec3& lo, Vec3& hi) const
    {
        const Vec3 center = Project((minimum + maximum) * .5f);
        const Vec3 half = (maximum - minimum) * .5f;
        const auto& m = viewProjection.m;
        const Vec3 extent{
            (std::abs(m[0]) * half.x + std::abs(m[4]) * half.y + std::abs(m[8]) * half.z) * width *
                .5f,
            (std::abs(m[1]) * half.x + std::abs(m[5]) * half.y + std::abs(m[9]) * half.z) * height *
                .5f,
            std::abs(m[2]) * half.x + std::abs(m[6]) * half.y + std::abs(m[10]) * half.z};
        lo = center - extent;
        hi = center + extent;
    }

    bool Visible(Vec3 minimum, Vec3 maximum, float margin = 0) const
    {
        Vec3 lo, hi;
        ProjectBounds(minimum, maximum, lo, hi);
        return hi.x >= -margin && hi.y >= -margin && lo.x <= width + margin &&
               lo.y <= height + margin && hi.z >= -1 && lo.z <= 1;
    }
};
