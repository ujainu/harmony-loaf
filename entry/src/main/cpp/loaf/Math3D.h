#pragma once

#include <cmath>
#include <cstring>

namespace loaf {

/// column-major 4x4 matrix helpers, compatible with glUniformMatrix4fv
struct Mat4 {
	float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	static Mat4 identity() { return Mat4(); }

	static Mat4 multiply(const Mat4 &a, const Mat4 &b) {
		Mat4 out;
		for(int c = 0; c < 4; ++c) {
			for(int r = 0; r < 4; ++r) {
				out.m[c * 4 + r] =
					a.m[0 * 4 + r] * b.m[c * 4 + 0] +
					a.m[1 * 4 + r] * b.m[c * 4 + 1] +
					a.m[2 * 4 + r] * b.m[c * 4 + 2] +
					a.m[3 * 4 + r] * b.m[c * 4 + 3];
			}
		}
		return out;
	}

	static Mat4 translation(float x, float y, float z) {
		Mat4 out;
		out.m[12] = x;
		out.m[13] = y;
		out.m[14] = z;
		return out;
	}

	static Mat4 scaling(float x, float y, float z) {
		Mat4 out;
		out.m[0] = x;
		out.m[5] = y;
		out.m[10] = z;
		return out;
	}

	static Mat4 rotationX(float radians) {
		Mat4 out;
		float c = std::cos(radians);
		float s = std::sin(radians);
		out.m[5] = c;
		out.m[6] = s;
		out.m[9] = -s;
		out.m[10] = c;
		return out;
	}

	static Mat4 rotationY(float radians) {
		Mat4 out;
		float c = std::cos(radians);
		float s = std::sin(radians);
		out.m[0] = c;
		out.m[2] = -s;
		out.m[8] = s;
		out.m[10] = c;
		return out;
	}

	static Mat4 rotationZ(float radians) {
		Mat4 out;
		float c = std::cos(radians);
		float s = std::sin(radians);
		out.m[0] = c;
		out.m[1] = s;
		out.m[4] = -s;
		out.m[5] = c;
		return out;
	}

	static Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
		Mat4 out;
		float f = 1.0f / std::tan(fovRadians * 0.5f);
		std::memset(out.m, 0, sizeof(out.m));
		out.m[0] = f / aspect;
		out.m[5] = f;
		out.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
		out.m[11] = -1.0f;
		out.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
		return out;
	}

	static Mat4 lookAt(float eyeX, float eyeY, float eyeZ,
	                   float centerX, float centerY, float centerZ,
	                   float upX, float upY, float upZ) {
		float fx = centerX - eyeX;
		float fy = centerY - eyeY;
		float fz = centerZ - eyeZ;
		float length = std::sqrt(fx * fx + fy * fy + fz * fz);
		if(length > 0.00001f) {
			fx /= length; fy /= length; fz /= length;
		}
		float sx = fy * upZ - fz * upY;
		float sy = fz * upX - fx * upZ;
		float sz = fx * upY - fy * upX;
		length = std::sqrt(sx * sx + sy * sy + sz * sz);
		if(length > 0.00001f) {
			sx /= length; sy /= length; sz /= length;
		}
		float ux = sy * fz - sz * fy;
		float uy = sz * fx - sx * fz;
		float uz = sx * fy - sy * fx;

		Mat4 out;
		out.m[0] = sx; out.m[4] = sy; out.m[8] = sz;
		out.m[1] = ux; out.m[5] = uy; out.m[9] = uz;
		out.m[2] = -fx; out.m[6] = -fy; out.m[10] = -fz;
		out.m[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
		out.m[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
		out.m[14] = (fx * eyeX + fy * eyeY + fz * eyeZ);
		out.m[15] = 1.0f;
		return out;
	}
};

} // namespace loaf
