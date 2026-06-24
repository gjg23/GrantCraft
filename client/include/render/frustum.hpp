#pragma once
// ------------------------------------------------------------------
// client/include/render/frustum.hpp
// Used by ChunkRenderer to skip draw calls for off-screen chunks
// ------------------------------------------------------------------

#include <glm/glm.hpp>
#include <array>

struct Plane {
    glm::vec3 normal;
    float d;

    // Positive means point is on the front side of this plane
    float distanceTo(const glm::vec3& p) const {
        return glm::dot(normal, p) + d;
    }
};

struct Frustum {
    std::array<Plane, 6> planes; // left, right, bottom, top, near, far

    // Build from any combined matrix
    static Frustum fromMatrix(const glm::mat4& m) {
        Frustum f;

        // Extract a row of the column-major GLM matrix
        auto row = [&](int i) {
            return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
        };

        glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        auto makePlane = [](glm::vec4 p) -> Plane {
            float len = glm::length(glm::vec3(p));
            return { glm::vec3(p) / len, p.w / len };
        };

        f.planes[0] = makePlane(r3 + r0); // left
        f.planes[1] = makePlane(r3 - r0); // right
        f.planes[2] = makePlane(r3 + r1); // bottom
        f.planes[3] = makePlane(r3 - r1); // top
        f.planes[4] = makePlane(r3 + r2); // near
        f.planes[5] = makePlane(r3 - r2); // far
        return f;
    }

    // Return false if the AABB is fully outside any plane
    // test the corner most likely to be inside.
    bool intersectsAABB(const glm::vec3& minP, const glm::vec3& maxP) const {
        for (const Plane& p : planes) {
            glm::vec3 pos = {
                p.normal.x >= 0.0f ? maxP.x : minP.x,
                p.normal.y >= 0.0f ? maxP.y : minP.y,
                p.normal.z >= 0.0f ? maxP.z : minP.z
            };
            if (p.distanceTo(pos) < 0.0f) return false;
        }
        return true;
    }
};