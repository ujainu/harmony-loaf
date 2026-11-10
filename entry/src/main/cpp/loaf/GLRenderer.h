#pragma once

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include "Math3D.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace loaf {

struct Color {
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;
};

/// minimal 2D affine transform: x' = a*x + c*y + e, y' = b*x + d*y + f
struct Affine {
	float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;
};

class GLRenderer {
public:

	bool init();
	void shutdown();

	/// physical surface size in px, logical size in vp (px / density)
	void resize(int pixelWidth, int pixelHeight, float density);
	void setDesignResolution(int width, int height);
	void clearDesignResolution();
	int designWidth() const { return designW; }
	int designHeight() const { return designH; }
	float toLogicalX(float pixelX) const;
	float toLogicalY(float pixelY) const;

	void beginFrame();
	void endFrame();
	void clear(const Color &color);

	/// state
	void setColor(const Color &color) { currentColor = color; }
	const Color &color() const { return currentColor; }
	void setFill(bool enable) { fillMode = enable; }
	bool isFilled() const { return fillMode; }
	void setLineWidth(float width) { lineWidth = width; }
	float getLineWidth() const { return lineWidth; }
	void setCircleResolution(int resolution);
	int getCircleResolution() const { return circleResolution; }
	void setAlphaBlending(bool enable);
	void setSmoothing(bool enable) { smooth = enable; }

	/// transform stack
	void pushMatrix();
	void popMatrix();
	void translate(float x, float y);
	void translate3D(float x, float y, float z);
	void rotate(float radians);
	void rotateX(float radians);
	void rotateY(float radians);
	void rotateZ(float radians);
	void scale(float x, float y);

	/// 2D drawing, logical (vp) coordinates
	void drawRectangle(float x, float y, float w, float h);
	void drawRectangleOutline(float x, float y, float w, float h);
	void drawCircle(float x, float y, float radius);
	void drawCircleOutline(float x, float y, float radius);
	void drawEllipse(float x, float y, float w, float h);
	void drawLine(float x1, float y1, float x2, float y2);
	void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3);

	/// polygon path
	void beginShape();
	void vertex(float x, float y);
	void nextContour();
	void endShape(bool close);

	/// text, y is the baseline
	float drawText(const std::string &text, float x, float y, float size,
	               const std::string &fontPath = "");
	float measureText(const std::string &text, float size,
	                  const std::string &fontPath = "");

	/// images/textures
	GLuint createTexture(const void *rgba, int width, int height);
	GLuint createExternalTexture();
	GLuint createCubeMapTexture(const std::vector<std::vector<unsigned char>> &faces, int size);
	void drawCubeMap(GLuint texture, float size);
	void deleteTexture(GLuint texture);
	void drawTexture(GLuint texture, float x, float y, float w, float h);
	void drawTextureRegion(GLuint texture, float x, float y, float w, float h,
	                       float u0, float v0, float u1, float v1);
	void drawExternalTexture(GLuint texture, const float textureMatrix[16],
	                         float x, float y, float w, float h);

	/// offscreen framebuffers
	GLuint createFramebuffer(int width, int height);
	void destroyFramebuffer(GLuint framebuffer);
	void beginFramebuffer(GLuint framebuffer, bool clear = true);
	void endFramebuffer();
	void drawFramebuffer(GLuint framebuffer, float x, float y, float w, float h);
	int framebufferWidth(GLuint framebuffer) const;
	int framebufferHeight(GLuint framebuffer) const;
	void readFramebuffer(GLuint framebuffer, std::vector<unsigned char> &pixels);
	Color readFramebufferPixel(GLuint framebuffer, int x, int y);
	void readPixels(int width, int height, std::vector<unsigned char> &pixels);
	GLuint framebufferTexture(GLuint framebuffer) const;

	/// custom shader support
	void bindExternalProgram(GLuint externalProgram);
	void restoreProgram();

	/// 3D pipeline
	void begin3D(const Mat4 &projection, const Mat4 &view);
	void end3D();
	bool is3D() const { return mode3D; }
	void drawMesh3D(const std::vector<float> &vertices, const std::vector<uint32_t> &indices,
	                bool wireframe);
	void update3DMVP();

	int width() const { return logicalWidth; }
	int height() const { return logicalHeight; }
	int pixelWidth() const { return surfaceWidth; }
	int pixelHeight() const { return surfaceHeight; }
	float density() const { return pixelDensity; }

private:

	struct Vertex {
		float x, y, u, v, r, g, b, a;
	};

	struct GlyphTexture {
		GLuint texture = 0;
		float width = 0;
		float height = 0;
		float baseline = 0;
	};

	void flush();
	void emitVertex(float x, float y, float u, float v, const Color &color);
	void emitQuad(float x, float y, float w, float h, GLuint texture,
	              float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
	void emitLineQuad(float x1, float y1, float x2, float y2, float width);
	void useTexture(GLuint texture);
	void updateProjection();
	void setProjection(int width, int height);
	void applyViewport();
	bool createProgram();
	GlyphTexture *glyphFor(const std::string &text, float size, const std::string &fontPath);

	GLuint program = 0;
	GLuint externalProgram = 0;
	GLuint program3D = 0;
	GLuint cubeProgram = 0;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint vao3D = 0;
	GLuint vbo3D = 0;
	GLuint ebo3D = 0;
	GLuint whiteTexture = 0;
	GLint projectionLocation = -1;
	GLint textureLocation = -1;
	GLint externalProjectionLocation = -1;
	GLint externalTextureMatrixLocation = -1;
	GLint projection3DLocation = -1;
	GLint cubeProjectionLocation = -1;
	float projectionMatrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	std::vector<Vertex> vertices;
	std::vector<Affine> matrixStack;
	std::unordered_map<std::string, GlyphTexture> glyphCache;

	Affine transform;
	Color currentColor;
	GLuint boundTexture = 0;

	int surfaceWidth = 0;
	int surfaceHeight = 0;
	int logicalWidth = 0;
	int logicalHeight = 0;
	float pixelDensity = 2.0f;

	bool useDesign = false;
	int designW = 0;
	int designH = 0;
	float contentScale = 1.0f;
	int viewportX = 0;
	int viewportY = 0;
	int viewportW = 0;
	int viewportH = 0;

	bool fillMode = true;
	bool smooth = true;
	bool blending = true;
	float lineWidth = 1.0f;
	int circleResolution = 24;

	std::vector<std::vector<std::pair<float, float>>> contours;
	std::vector<std::pair<float, float>> currentContour;
	bool shapeOpen = false;

	struct FramebufferInfo {
		GLuint texture = 0;
		GLuint depth = 0;
		int width = 0;
		int height = 0;
	};
	std::unordered_map<GLuint, FramebufferInfo> framebuffers;
	GLuint boundFramebuffer = 0;

	bool mode3D = false;
	Mat4 projection3D;
	Mat4 view3D;
	Mat4 model3D;
	std::vector<Mat4> stack3D;
};

} // namespace loaf
