#include "GLRenderer.h"

#include <native_drawing/drawing_bitmap.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_canvas.h>
#include <native_drawing/drawing_font.h>
#include <native_drawing/drawing_text_blob.h>
#include <native_drawing/drawing_typeface.h>
#include <native_drawing/drawing_types.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

const char *kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main() {
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vUV = aUV;
	vColor = aColor;
}
)";

const char *kFragmentShader = R"(#version 300 es
precision mediump float;
uniform sampler2D uTexture;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
	fragColor = texture(uTexture, vUV) * vColor;
}
)";

const char *kExternalVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProjection;
uniform mat4 uTexMatrix;
out vec2 vUV;
out vec4 vColor;
void main() {
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vUV = (uTexMatrix * vec4(aUV, 0.0, 1.0)).xy;
	vColor = aColor;
}
)";

const char *kExternalFragmentShader = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTexture;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() {
	fragColor = texture(uTexture, vUV) * vColor;
}
)";

const char *k3DVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec4 vColor;
out vec3 vNormal;
out vec3 vWorld;
void main() {
	gl_Position = uMVP * vec4(aPos, 1.0);
	vColor = aColor;
	vNormal = aNormal;
	vWorld = (uModel * vec4(aPos, 1.0)).xyz;
}
)";

const char *k3DFragmentShader = R"(#version 300 es
precision mediump float;
in vec4 vColor;
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uLightDir;
out vec4 fragColor;
void main() {
	vec3 normal = normalize(vNormal);
	float lambert = clamp(dot(normal, normalize(uLightDir)), 0.0, 1.0);
	float light = 0.45 + 0.55 * lambert;
	fragColor = vec4(vColor.rgb * light, vColor.a);
}
)";

const char *kCubeVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vPos;
void main() {
	gl_Position = uMVP * vec4(aPos, 1.0);
	vPos = aPos;
}
)";

const char *kCubeFragmentShader = R"(#version 300 es
precision mediump float;
uniform samplerCube uCube;
in vec3 vPos;
out vec4 fragColor;
void main() {
	fragColor = texture(uCube, normalize(vPos));
}
)";

GLuint compileShader(GLenum type, const char *source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if(ok != GL_TRUE) {
		char log[1024] = {0};
		glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
		LOGE("shader compile failed: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

} // namespace

//--------------------------------------------------------------
bool GLRenderer::init() {
	if(!createProgram()) {
		return false;
	}
	GLuint externalVertex = compileShader(GL_VERTEX_SHADER, kExternalVertexShader);
	GLuint externalFragment = compileShader(GL_FRAGMENT_SHADER, kExternalFragmentShader);
	if(externalVertex != 0 && externalFragment != 0) {
		externalProgram = glCreateProgram();
		glAttachShader(externalProgram, externalVertex);
		glAttachShader(externalProgram, externalFragment);
		glLinkProgram(externalProgram);
		GLint ok = GL_FALSE;
		glGetProgramiv(externalProgram, GL_LINK_STATUS, &ok);
		if(ok != GL_TRUE) {
			glDeleteProgram(externalProgram);
			externalProgram = 0;
		}
		else {
			externalProjectionLocation = glGetUniformLocation(externalProgram, "uProjection");
			externalTextureMatrixLocation = glGetUniformLocation(externalProgram, "uTexMatrix");
			GLint sampler = glGetUniformLocation(externalProgram, "uTexture");
			glUseProgram(externalProgram);
			glUniform1i(sampler, 0);
			glUseProgram(0);
		}
	}
	if(externalVertex != 0) glDeleteShader(externalVertex);
	if(externalFragment != 0) glDeleteShader(externalFragment);

	GLuint vertex3D = compileShader(GL_VERTEX_SHADER, k3DVertexShader);
	GLuint fragment3D = compileShader(GL_FRAGMENT_SHADER, k3DFragmentShader);
	if(vertex3D != 0 && fragment3D != 0) {
		program3D = glCreateProgram();
		glAttachShader(program3D, vertex3D);
		glAttachShader(program3D, fragment3D);
		glLinkProgram(program3D);
		GLint ok = GL_FALSE;
		glGetProgramiv(program3D, GL_LINK_STATUS, &ok);
		if(ok != GL_TRUE) {
			glDeleteProgram(program3D);
			program3D = 0;
		}
		else {
			projection3DLocation = glGetUniformLocation(program3D, "uMVP");
			GLint modelLocation = glGetUniformLocation(program3D, "uModel");
			glUseProgram(program3D);
			glUniformMatrix4fv(modelLocation, 1, GL_FALSE, Mat4().m);
			glUniform3f(glGetUniformLocation(program3D, "uLightDir"), -0.4f, 0.8f, 0.6f);
			glUseProgram(0);
		}
	}
	if(vertex3D != 0) glDeleteShader(vertex3D);
	if(fragment3D != 0) glDeleteShader(fragment3D);

	GLuint cubeVertex = compileShader(GL_VERTEX_SHADER, kCubeVertexShader);
	GLuint cubeFragment = compileShader(GL_FRAGMENT_SHADER, kCubeFragmentShader);
	if(cubeVertex != 0 && cubeFragment != 0) {
		cubeProgram = glCreateProgram();
		glAttachShader(cubeProgram, cubeVertex);
		glAttachShader(cubeProgram, cubeFragment);
		glLinkProgram(cubeProgram);
		GLint ok = GL_FALSE;
		glGetProgramiv(cubeProgram, GL_LINK_STATUS, &ok);
		if(ok != GL_TRUE) {
			glDeleteProgram(cubeProgram);
			cubeProgram = 0;
		}
		else {
			cubeProjectionLocation = glGetUniformLocation(cubeProgram, "uMVP");
			glUseProgram(cubeProgram);
			glUniform1i(glGetUniformLocation(cubeProgram, "uCube"), 0);
			glUseProgram(0);
		}
	}
	if(cubeVertex != 0) glDeleteShader(cubeVertex);
	if(cubeFragment != 0) glDeleteShader(cubeFragment);
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, r));
	glBindVertexArray(0);

	glGenVertexArrays(1, &vao3D);
	glGenBuffers(1, &vbo3D);
	glGenBuffers(1, &ebo3D);
	glBindVertexArray(vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, vbo3D);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo3D);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void *)(7 * sizeof(float)));
	glBindVertexArray(0);

	const unsigned char white[4] = {255, 255, 255, 255};
	whiteTexture = createTexture(white, 1, 1);
	boundTexture = whiteTexture;
	return whiteTexture != 0;
}

//--------------------------------------------------------------
void GLRenderer::shutdown() {
	for(auto &entry : glyphCache) {
		if(entry.second.texture != 0) {
			glDeleteTextures(1, &entry.second.texture);
		}
	}
	glyphCache.clear();
	if(whiteTexture != 0) {
		glDeleteTextures(1, &whiteTexture);
		whiteTexture = 0;
	}
	if(vbo != 0) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if(vao != 0) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if(vbo3D != 0) {
		glDeleteBuffers(1, &vbo3D);
		vbo3D = 0;
	}
	if(ebo3D != 0) {
		glDeleteBuffers(1, &ebo3D);
		ebo3D = 0;
	}
	if(vao3D != 0) {
		glDeleteVertexArrays(1, &vao3D);
		vao3D = 0;
	}
	if(program != 0) {
		glDeleteProgram(program);
		program = 0;
	}
	if(externalProgram != 0) {
		glDeleteProgram(externalProgram);
		externalProgram = 0;
	}
	if(program3D != 0) {
		glDeleteProgram(program3D);
		program3D = 0;
	}
	if(cubeProgram != 0) {
		glDeleteProgram(cubeProgram);
		cubeProgram = 0;
	}
}

//--------------------------------------------------------------
bool GLRenderer::createProgram() {
	GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
	GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
	if(vertexShader == 0 || fragmentShader == 0) {
		return false;
	}
	program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if(ok != GL_TRUE) {
		char log[1024] = {0};
		glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
		LOGE("program link failed: %s", log);
		glDeleteProgram(program);
		program = 0;
		return false;
	}
	projectionLocation = glGetUniformLocation(program, "uProjection");
	textureLocation = glGetUniformLocation(program, "uTexture");
	return true;
}

//--------------------------------------------------------------
void GLRenderer::resize(int pixelWidth, int pixelHeight, float density) {
	surfaceWidth = std::max(1, pixelWidth);
	surfaceHeight = std::max(1, pixelHeight);
	pixelDensity = density > 0.1f ? density : 1.0f;
	logicalWidth = std::max(1, (int)std::round(surfaceWidth / pixelDensity));
	logicalHeight = std::max(1, (int)std::round(surfaceHeight / pixelDensity));
	applyViewport();
	updateProjection();
	OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "loafcore",
	             "surface %{public}dx%{public}d density %{public}.2f logical %{public}dx%{public}d design %{public}d",
	             surfaceWidth, surfaceHeight, pixelDensity, logicalWidth, logicalHeight,
	             useDesign ? 1 : 0);
}

//--------------------------------------------------------------
void GLRenderer::setDesignResolution(int width, int height) {
	if(width <= 0 || height <= 0) {
		clearDesignResolution();
		return;
	}
	useDesign = true;
	designW = width;
	designH = height;
	resize(surfaceWidth, surfaceHeight, pixelDensity);
}

//--------------------------------------------------------------
void GLRenderer::clearDesignResolution() {
	useDesign = false;
	designW = 0;
	designH = 0;
	resize(surfaceWidth, surfaceHeight, pixelDensity);
}

//--------------------------------------------------------------
void GLRenderer::applyViewport() {
	if(useDesign) {
		float pixelW = designW * pixelDensity;
		float pixelH = designH * pixelDensity;
		float scaleX = surfaceWidth / pixelW;
		float scaleY = surfaceHeight / pixelH;
		contentScale = std::min(scaleX, scaleY);
		viewportW = std::max(1, (int)std::round(pixelW * contentScale));
		viewportH = std::max(1, (int)std::round(pixelH * contentScale));
		viewportX = (surfaceWidth - viewportW) / 2;
		viewportY = (surfaceHeight - viewportH) / 2;
		logicalWidth = designW;
		logicalHeight = designH;
	}
	else {
		contentScale = 1.0f;
		viewportX = 0;
		viewportY = 0;
		viewportW = surfaceWidth;
		viewportH = surfaceHeight;
	}
	glViewport(viewportX, viewportY, viewportW, viewportH);
}

//--------------------------------------------------------------
float GLRenderer::toLogicalX(float pixelX) const {
	if(useDesign) {
		return (pixelX - viewportX) / (pixelDensity * contentScale);
	}
	return pixelX / pixelDensity;
}

//--------------------------------------------------------------
float GLRenderer::toLogicalY(float pixelY) const {
	if(useDesign) {
		return (pixelY - viewportY) / (pixelDensity * contentScale);
	}
	return pixelY / pixelDensity;
}

//--------------------------------------------------------------
void GLRenderer::updateProjection() {
	setProjection(logicalWidth, logicalHeight);
}

//--------------------------------------------------------------
void GLRenderer::setProjection(int width, int height) {
	// orthographic, origin top-left, y down, in logical units
	float l = 0.0f;
	float r = (float)width;
	float t = 0.0f;
	float b = (float)height;
	float projection[16] = {
		2.0f / (r - l), 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f / (t - b), 0.0f, 0.0f,
		0.0f, 0.0f, -1.0f, 0.0f,
		-(r + l) / (r - l), -(t + b) / (t - b), 0.0f, 1.0f
	};
	if(program != 0) {
		glUseProgram(program);
		glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, projection);
		glUniform1i(textureLocation, 0);
	}
	std::memcpy(projectionMatrix, projection, sizeof(projection));
}

//--------------------------------------------------------------
GLuint GLRenderer::createFramebuffer(int width, int height) {
	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	FramebufferInfo info;
	info.width = std::max(1, width);
	info.height = std::max(1, height);
	glGenTextures(1, &info.texture);
	glBindTexture(GL_TEXTURE_2D, info.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, info.texture, 0);

	glGenRenderbuffers(1, &info.depth);
	glBindRenderbuffer(GL_RENDERBUFFER, info.depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, info.width, info.height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, info.depth);

	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		LOGE("framebuffer incomplete");
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	framebuffers[fbo] = info;
	return fbo;
}

//--------------------------------------------------------------
void GLRenderer::destroyFramebuffer(GLuint framebuffer) {
	auto found = framebuffers.find(framebuffer);
	if(found == framebuffers.end()) {
		return;
	}
	if(found->second.texture != 0) {
		glDeleteTextures(1, &found->second.texture);
	}
	if(found->second.depth != 0) {
		glDeleteRenderbuffers(1, &found->second.depth);
	}
	glDeleteFramebuffers(1, &framebuffer);
	framebuffers.erase(found);
}

//--------------------------------------------------------------
void GLRenderer::beginFramebuffer(GLuint framebuffer, bool clearBuffer) {
	auto found = framebuffers.find(framebuffer);
	if(found == framebuffers.end()) {
		return;
	}
	flush();
	boundFramebuffer = framebuffer;
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, found->second.width, found->second.height);
	setProjection(found->second.width, found->second.height);
	if(clearBuffer) {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
}

//--------------------------------------------------------------
void GLRenderer::endFramebuffer() {
	flush();
	boundFramebuffer = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	applyViewport();
	setProjection(logicalWidth, logicalHeight);
}

//--------------------------------------------------------------
void GLRenderer::drawFramebuffer(GLuint framebuffer, float x, float y, float w, float h) {
	auto found = framebuffers.find(framebuffer);
	if(found == framebuffers.end()) {
		return;
	}
	useTexture(found->second.texture);
	// framebuffer textures are stored bottom-up
	emitVertex(x, y, 0.0f, 1.0f, currentColor);
	emitVertex(x + w, y, 1.0f, 1.0f, currentColor);
	emitVertex(x + w, y + h, 1.0f, 0.0f, currentColor);
	emitVertex(x, y, 0.0f, 1.0f, currentColor);
	emitVertex(x + w, y + h, 1.0f, 0.0f, currentColor);
	emitVertex(x, y + h, 0.0f, 0.0f, currentColor);
}

//--------------------------------------------------------------
int GLRenderer::framebufferWidth(GLuint framebuffer) const {
	auto found = framebuffers.find(framebuffer);
	return found != framebuffers.end() ? found->second.width : 0;
}

//--------------------------------------------------------------
int GLRenderer::framebufferHeight(GLuint framebuffer) const {
	auto found = framebuffers.find(framebuffer);
	return found != framebuffers.end() ? found->second.height : 0;
}

//--------------------------------------------------------------
GLuint GLRenderer::framebufferTexture(GLuint framebuffer) const {
	auto found = framebuffers.find(framebuffer);
	return found != framebuffers.end() ? found->second.texture : 0;
}

//--------------------------------------------------------------
void GLRenderer::bindExternalProgram(GLuint externalProgram) {
	flush();
	glUseProgram(externalProgram);
	GLint projection = glGetUniformLocation(externalProgram, "uProjection");
	if(projection >= 0) {
		glUniformMatrix4fv(projection, 1, GL_FALSE, projectionMatrix);
	}
	GLint sampler = glGetUniformLocation(externalProgram, "uTexture");
	if(sampler >= 0) {
		glUniform1i(sampler, 0);
	}
}

//--------------------------------------------------------------
void GLRenderer::restoreProgram() {
	flush();
	glUseProgram(program);
	glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, projectionMatrix);
	glUniform1i(textureLocation, 0);
}

//--------------------------------------------------------------
void GLRenderer::readFramebuffer(GLuint framebuffer, std::vector<unsigned char> &pixels) {
	auto found = framebuffers.find(framebuffer);
	if(found == framebuffers.end()) {
		return;
	}
	flush();
	pixels.resize((size_t)found->second.width * found->second.height * 4);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glReadPixels(0, 0, found->second.width, found->second.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindFramebuffer(GL_FRAMEBUFFER, boundFramebuffer);
}

//--------------------------------------------------------------
Color GLRenderer::readFramebufferPixel(GLuint framebuffer, int x, int y) {
	Color color {0, 0, 0, 0};
	auto found = framebuffers.find(framebuffer);
	if(found == framebuffers.end()) {
		return color;
	}
	if(x < 0 || y < 0 || x >= found->second.width || y >= found->second.height) {
		return color;
	}
	flush();
	unsigned char pixel[4] = {0, 0, 0, 0};
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	// scripts use a top-left origin, GL reads bottom-up
	glReadPixels(x, found->second.height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	glBindFramebuffer(GL_FRAMEBUFFER, boundFramebuffer);
	color.r = pixel[0] / 255.0f;
	color.g = pixel[1] / 255.0f;
	color.b = pixel[2] / 255.0f;
	color.a = pixel[3] / 255.0f;
	return color;
}

//--------------------------------------------------------------
void GLRenderer::readPixels(int width, int height, std::vector<unsigned char> &pixels) {
	if(width <= 0 || height <= 0) {
		return;
	}
	flush();
	pixels.resize((size_t)width * height * 4);
	glBindFramebuffer(GL_FRAMEBUFFER, boundFramebuffer);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

//--------------------------------------------------------------
void GLRenderer::beginFrame() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	applyViewport();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(vao);
	vertices.clear();
	matrixStack.clear();
	transform = Affine();
	contours.clear();
	currentContour.clear();
	shapeOpen = false;
	boundTexture = whiteTexture;
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	glClear(GL_DEPTH_BUFFER_BIT);
	mode3D = false;
}

//--------------------------------------------------------------
void GLRenderer::endFrame() {
	flush();
	glBindVertexArray(0);
}

//--------------------------------------------------------------
void GLRenderer::clear(const Color &color) {
	flush();
	glClearColor(color.r, color.g, color.b, color.a);
	glClear(GL_COLOR_BUFFER_BIT);
}

//--------------------------------------------------------------
void GLRenderer::setCircleResolution(int resolution) {
	circleResolution = std::max(8, std::min(512, resolution));
}

//--------------------------------------------------------------
void GLRenderer::setAlphaBlending(bool enable) {
	blending = enable;
	flush();
	if(blending) {
		glEnable(GL_BLEND);
	}
	else {
		glDisable(GL_BLEND);
	}
}

//--------------------------------------------------------------
void GLRenderer::pushMatrix() {
	if(mode3D) {
		stack3D.push_back(model3D);
		return;
	}
	matrixStack.push_back(transform);
}

//--------------------------------------------------------------
void GLRenderer::popMatrix() {
	if(mode3D) {
		if(!stack3D.empty()) {
			model3D = stack3D.back();
			stack3D.pop_back();
		}
		return;
	}
	if(!matrixStack.empty()) {
		transform = matrixStack.back();
		matrixStack.pop_back();
	}
}

//--------------------------------------------------------------
void GLRenderer::translate(float x, float y) {
	translate3D(x, y, 0.0f);
}

void GLRenderer::translate3D(float x, float y, float z) {
	if(mode3D) {
		model3D = Mat4::multiply(model3D, Mat4::translation(x, y, z));
		return;
	}
	Affine t;
	t.e = x;
	t.f = y;
	// transform = transform * t
	Affine result;
	result.a = transform.a * t.a + transform.c * t.b;
	result.b = transform.b * t.a + transform.d * t.b;
	result.c = transform.a * t.c + transform.c * t.d;
	result.d = transform.b * t.c + transform.d * t.d;
	result.e = transform.a * t.e + transform.c * t.f + transform.e;
	result.f = transform.b * t.e + transform.d * t.f + transform.f;
	transform = result;
}

//--------------------------------------------------------------
void GLRenderer::rotate(float radians) {
	if(mode3D) {
		model3D = Mat4::multiply(model3D, Mat4::rotationZ(radians));
		return;
	}
	float cs = std::cos(radians);
	float sn = std::sin(radians);
	Affine t;
	t.a = cs; t.b = sn;
	t.c = -sn; t.d = cs;
	Affine result;
	result.a = transform.a * t.a + transform.c * t.b;
	result.b = transform.b * t.a + transform.d * t.b;
	result.c = transform.a * t.c + transform.c * t.d;
	result.d = transform.b * t.c + transform.d * t.d;
	result.e = transform.e;
	result.f = transform.f;
	transform = result;
}

//--------------------------------------------------------------
void GLRenderer::scale(float x, float y) {
	if(mode3D) {
		model3D = Mat4::multiply(model3D, Mat4::scaling(x, y, 1.0f));
		return;
	}
	transform.a *= x;
	transform.b *= x;
	transform.c *= y;
	transform.d *= y;
}

//--------------------------------------------------------------
void GLRenderer::rotateX(float radians) {
	model3D = Mat4::multiply(model3D, Mat4::rotationX(radians));
}

void GLRenderer::rotateY(float radians) {
	model3D = Mat4::multiply(model3D, Mat4::rotationY(radians));
}

void GLRenderer::rotateZ(float radians) {
	model3D = Mat4::multiply(model3D, Mat4::rotationZ(radians));
}

//--------------------------------------------------------------
void GLRenderer::begin3D(const Mat4 &projection, const Mat4 &view) {
	flush();
	mode3D = true;
	projection3D = projection;
	view3D = view;
	model3D = Mat4::identity();
	stack3D.clear();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glUseProgram(program3D);
	update3DMVP();
}

//--------------------------------------------------------------
void GLRenderer::end3D() {
	flush();
	mode3D = false;
	stack3D.clear();
	glDisable(GL_DEPTH_TEST);
	glUseProgram(program);
	glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, projectionMatrix);
}

//--------------------------------------------------------------
void GLRenderer::update3DMVP() {
	Mat4 mvp = Mat4::multiply(projection3D, Mat4::multiply(view3D, model3D));
	if(projection3DLocation >= 0) {
		glUniformMatrix4fv(projection3DLocation, 1, GL_FALSE, mvp.m);
	}
	GLint modelLocation = glGetUniformLocation(program3D, "uModel");
	if(modelLocation >= 0) {
		glUniformMatrix4fv(modelLocation, 1, GL_FALSE, model3D.m);
	}
}

//--------------------------------------------------------------
void GLRenderer::drawMesh3D(const std::vector<float> &vertices, const std::vector<uint32_t> &indices,
                            bool wireframe) {
	if(!mode3D || vertices.empty() || indices.empty()) {
		return;
	}
	update3DMVP();
	glBindVertexArray(vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, vbo3D);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);

	if(wireframe) {
		std::vector<uint32_t> lines;
		lines.reserve(indices.size() * 2);
		for(size_t i = 0; i + 2 < indices.size(); i += 3) {
			lines.push_back(indices[i]);
			lines.push_back(indices[i + 1]);
			lines.push_back(indices[i + 1]);
			lines.push_back(indices[i + 2]);
			lines.push_back(indices[i + 2]);
			lines.push_back(indices[i]);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo3D);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, lines.size() * sizeof(uint32_t), lines.data(), GL_STREAM_DRAW);
		glDrawElements(GL_LINES, (GLsizei)lines.size(), GL_UNSIGNED_INT, nullptr);
	}
	else {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo3D);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STREAM_DRAW);
		glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
	}
	glBindVertexArray(0);
}

//--------------------------------------------------------------
void GLRenderer::useTexture(GLuint texture) {
	if(texture != boundTexture) {
		flush();
		boundTexture = texture;
		glBindTexture(GL_TEXTURE_2D, texture);
	}
}

//--------------------------------------------------------------
void GLRenderer::emitVertex(float x, float y, float u, float v, const Color &color) {
	float tx = transform.a * x + transform.c * y + transform.e;
	float ty = transform.b * x + transform.d * y + transform.f;
	vertices.push_back({tx, ty, u, v, color.r, color.g, color.b, color.a});
}

//--------------------------------------------------------------
void GLRenderer::emitQuad(float x, float y, float w, float h, GLuint texture,
                          float u0, float v0, float u1, float v1) {
	useTexture(texture);
	emitVertex(x, y, u0, v0, currentColor);
	emitVertex(x + w, y, u1, v0, currentColor);
	emitVertex(x + w, y + h, u1, v1, currentColor);
	emitVertex(x, y, u0, v0, currentColor);
	emitVertex(x + w, y + h, u1, v1, currentColor);
	emitVertex(x, y + h, u0, v1, currentColor);
}

//--------------------------------------------------------------
void GLRenderer::emitLineQuad(float x1, float y1, float x2, float y2, float width) {
	float dx = x2 - x1;
	float dy = y2 - y1;
	float len = std::sqrt(dx * dx + dy * dy);
	if(len < 0.0001f) {
		return;
	}
	float nx = -dy / len * width * 0.5f;
	float ny = dx / len * width * 0.5f;
	useTexture(whiteTexture);
	emitVertex(x1 + nx, y1 + ny, 0.5f, 0.5f, currentColor);
	emitVertex(x2 + nx, y2 + ny, 0.5f, 0.5f, currentColor);
	emitVertex(x2 - nx, y2 - ny, 0.5f, 0.5f, currentColor);
	emitVertex(x1 + nx, y1 + ny, 0.5f, 0.5f, currentColor);
	emitVertex(x2 - nx, y2 - ny, 0.5f, 0.5f, currentColor);
	emitVertex(x1 - nx, y1 - ny, 0.5f, 0.5f, currentColor);
}

//--------------------------------------------------------------
void GLRenderer::flush() {
	if(vertices.empty()) {
		return;
	}
	// draws can be issued outside beginFrame()/endFrame(), so make sure the
	// vertex state is bound before submitting the batch
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());
	vertices.clear();
}

//--------------------------------------------------------------
void GLRenderer::drawRectangle(float x, float y, float w, float h) {
	if(fillMode) {
		emitQuad(x, y, w, h, whiteTexture);
	}
	else {
		drawRectangleOutline(x, y, w, h);
	}
}

//--------------------------------------------------------------
void GLRenderer::drawRectangleOutline(float x, float y, float w, float h) {
	emitLineQuad(x, y, x + w, y, lineWidth);
	emitLineQuad(x + w, y, x + w, y + h, lineWidth);
	emitLineQuad(x + w, y + h, x, y + h, lineWidth);
	emitLineQuad(x, y + h, x, y, lineWidth);
}

//--------------------------------------------------------------
void GLRenderer::drawCircle(float x, float y, float radius) {
	int segments = circleResolution;
	if(fillMode) {
		useTexture(whiteTexture);
		for(int i = 0; i < segments; ++i) {
			float a0 = (float)i / segments * (float)M_PI * 2.0f;
			float a1 = (float)(i + 1) / segments * (float)M_PI * 2.0f;
			emitVertex(x, y, 0.5f, 0.5f, currentColor);
			emitVertex(x + std::cos(a0) * radius, y + std::sin(a0) * radius, 0.5f, 0.5f, currentColor);
			emitVertex(x + std::cos(a1) * radius, y + std::sin(a1) * radius, 0.5f, 0.5f, currentColor);
		}
	}
	else {
		for(int i = 0; i < segments; ++i) {
			float a0 = (float)i / segments * (float)M_PI * 2.0f;
			float a1 = (float)(i + 1) / segments * (float)M_PI * 2.0f;
			emitLineQuad(x + std::cos(a0) * radius, y + std::sin(a0) * radius,
			             x + std::cos(a1) * radius, y + std::sin(a1) * radius, lineWidth);
		}
	}
}

//--------------------------------------------------------------
void GLRenderer::drawCircleOutline(float x, float y, float radius) {
	bool wasFilled = fillMode;
	fillMode = false;
	drawCircle(x, y, radius);
	fillMode = wasFilled;
}

//--------------------------------------------------------------
void GLRenderer::drawEllipse(float x, float y, float w, float h) {
	pushMatrix();
	translate(x + w * 0.5f, y + h * 0.5f);
	scale(w * 0.5f, h * 0.5f);
	drawCircle(0.0f, 0.0f, 1.0f);
	popMatrix();
}

//--------------------------------------------------------------
void GLRenderer::drawLine(float x1, float y1, float x2, float y2) {
	emitLineQuad(x1, y1, x2, y2, lineWidth);
}

//--------------------------------------------------------------
void GLRenderer::drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
	if(fillMode) {
		useTexture(whiteTexture);
		emitVertex(x1, y1, 0.5f, 0.5f, currentColor);
		emitVertex(x2, y2, 0.5f, 0.5f, currentColor);
		emitVertex(x3, y3, 0.5f, 0.5f, currentColor);
	}
	else {
		emitLineQuad(x1, y1, x2, y2, lineWidth);
		emitLineQuad(x2, y2, x3, y3, lineWidth);
		emitLineQuad(x3, y3, x1, y1, lineWidth);
	}
}

//--------------------------------------------------------------
void GLRenderer::beginShape() {
	contours.clear();
	currentContour.clear();
	shapeOpen = true;
}

//--------------------------------------------------------------
void GLRenderer::vertex(float x, float y) {
	if(!shapeOpen) {
		beginShape();
	}
	currentContour.emplace_back(x, y);
}

//--------------------------------------------------------------
void GLRenderer::nextContour() {
	if(!currentContour.empty()) {
		contours.push_back(currentContour);
		currentContour.clear();
	}
}

//--------------------------------------------------------------
void GLRenderer::endShape(bool close) {
	if(!shapeOpen) {
		return;
	}
	nextContour();
	shapeOpen = false;

	for(auto &contour : contours) {
		if(contour.size() < 2) {
			continue;
		}
		if(fillMode && contour.size() >= 3) {
			useTexture(whiteTexture);
			for(size_t i = 1; i + 1 < contour.size(); ++i) {
				emitVertex(contour[0].first, contour[0].second, 0.5f, 0.5f, currentColor);
				emitVertex(contour[i].first, contour[i].second, 0.5f, 0.5f, currentColor);
				emitVertex(contour[i + 1].first, contour[i + 1].second, 0.5f, 0.5f, currentColor);
			}
		}
		else {
			for(size_t i = 0; i + 1 < contour.size(); ++i) {
				emitLineQuad(contour[i].first, contour[i].second,
				             contour[i + 1].first, contour[i + 1].second, lineWidth);
			}
			if(close && contour.size() > 2) {
				emitLineQuad(contour.back().first, contour.back().second,
				             contour.front().first, contour.front().second, lineWidth);
			}
		}
	}
	contours.clear();
	currentContour.clear();
}

//--------------------------------------------------------------
GLuint GLRenderer::createTexture(const void *rgba, int width, int height) {
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	return texture;
}

//--------------------------------------------------------------
GLuint GLRenderer::createExternalTexture() {
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	return texture;
}

//--------------------------------------------------------------
GLuint GLRenderer::createCubeMapTexture(const std::vector<std::vector<unsigned char>> &faces,
                                        int size) {
	if(faces.size() != 6 || size <= 0) {
		return 0;
	}
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	static const GLenum targets[6] = {
		GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
	};
	for(int i = 0; i < 6; ++i) {
		glTexImage2D(targets[i], 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		             faces[i].data());
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	return texture;
}

//--------------------------------------------------------------
void GLRenderer::drawCubeMap(GLuint texture, float size) {
	if(texture == 0 || cubeProgram == 0 || !mode3D) {
		return;
	}
	flush();
	glUseProgram(cubeProgram);
	Mat4 mvp = Mat4::multiply(projection3D, view3D);
	glUniformMatrix4fv(cubeProjectionLocation, 1, GL_FALSE, mvp.m);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
	glDepthMask(GL_FALSE);

	std::vector<float> vertices;
	const float s = size;
	const float box[8][3] = {
		{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},
		{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}
	};
	const int faces[6][4] = {
		{1,0,3,2},{4,5,6,7},{5,1,2,6},{0,4,7,3},{4,0,1,5},{3,2,6,7}
	};
	for(int f = 0; f < 6; ++f) {
		const int *q = faces[f];
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for(int i = 0; i < 6; ++i) {
			const float *p = box[q[order[i]]];
			vertices.insert(vertices.end(), {p[0], p[1], p[2]});
		}
	}
	glBindVertexArray(vao3D);
	glBindBuffer(GL_ARRAY_BUFFER, vbo3D);
	// pack as pos(3) + 7 unused floats to match the 3D layout
	std::vector<float> packed;
	packed.reserve(vertices.size() / 3 * 10);
	for(size_t i = 0; i < vertices.size(); i += 3) {
		packed.insert(packed.end(), {vertices[i], vertices[i + 1], vertices[i + 2],
		                             1, 1, 1, 1, 0, 0, 0});
	}
	glBufferData(GL_ARRAY_BUFFER, packed.size() * sizeof(float), packed.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(packed.size() / 10));
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glBindTexture(GL_TEXTURE_2D, boundTexture);
	glUseProgram(program);
}

//--------------------------------------------------------------
void GLRenderer::drawExternalTexture(GLuint texture, const float textureMatrix[16],
                                     float x, float y, float w, float h) {
	if(texture == 0 || externalProgram == 0) {
		return;
	}
	flush();
	glUseProgram(externalProgram);
	if(externalProjectionLocation >= 0) {
		glUniformMatrix4fv(externalProjectionLocation, 1, GL_FALSE, projectionMatrix);
	}
	if(externalTextureMatrixLocation >= 0) {
		glUniformMatrix4fv(externalTextureMatrixLocation, 1, GL_FALSE, textureMatrix);
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);

	emitVertex(x, y, 0.0f, 0.0f, currentColor);
	emitVertex(x + w, y, 1.0f, 0.0f, currentColor);
	emitVertex(x + w, y + h, 1.0f, 1.0f, currentColor);
	emitVertex(x, y, 0.0f, 0.0f, currentColor);
	emitVertex(x + w, y + h, 1.0f, 1.0f, currentColor);
	emitVertex(x, y + h, 0.0f, 1.0f, currentColor);
	flush();

	glBindTexture(GL_TEXTURE_2D, boundTexture);
	glUseProgram(program);
}

//--------------------------------------------------------------
void GLRenderer::deleteTexture(GLuint texture) {
	if(texture != 0) {
		glDeleteTextures(1, &texture);
	}
}

//--------------------------------------------------------------
void GLRenderer::drawTexture(GLuint texture, float x, float y, float w, float h) {
	emitQuad(x, y, w, h, texture);
}

//--------------------------------------------------------------
void GLRenderer::drawTextureRegion(GLuint texture, float x, float y, float w, float h,
                                   float u0, float v0, float u1, float v1) {
	emitQuad(x, y, w, h, texture, u0, v0, u1, v1);
}

//--------------------------------------------------------------
GLRenderer::GlyphTexture *GLRenderer::glyphFor(const std::string &text, float size,
                                               const std::string &fontPath) {
	std::string key = fontPath + "|" + std::to_string(size) + "|" + text;
	auto found = glyphCache.find(key);
	if(found != glyphCache.end()) {
		return &found->second;
	}

	float pixelSize = std::max(4.0f, size * pixelDensity);
	OH_Drawing_Font *font = OH_Drawing_FontCreate();
	if(font == nullptr) {
		return nullptr;
	}
	OH_Drawing_FontSetTextSize(font, pixelSize);
	if(!fontPath.empty()) {
		OH_Drawing_Typeface *typeface = OH_Drawing_TypefaceCreateFromFile(fontPath.c_str(), 0);
		if(typeface != nullptr) {
			OH_Drawing_FontSetTypeface(font, typeface);
			OH_Drawing_TypefaceDestroy(typeface);
		}
		else {
			LOGW("could not load font: %{public}s", fontPath.c_str());
		}
	}

	float textWidth = 0.0f;
	if(!text.empty()) {
		OH_Drawing_FontMeasureText(font, text.c_str(), text.size(), TEXT_ENCODING_UTF8, nullptr, &textWidth);
	}
	float ascent = pixelSize * 1.0f;
	int bitmapWidth = std::max(1, (int)std::ceil(textWidth) + 4);
	int bitmapHeight = std::max(1, (int)std::ceil(pixelSize * 1.45f) + 4);

	OH_Drawing_Bitmap *bitmap = OH_Drawing_BitmapCreate();
	OH_Drawing_BitmapFormat format {COLOR_FORMAT_RGBA_8888, ALPHA_FORMAT_UNPREMUL};
	OH_Drawing_BitmapBuild(bitmap, bitmapWidth, bitmapHeight, &format);

	OH_Drawing_Canvas *canvas = OH_Drawing_CanvasCreate();
	OH_Drawing_CanvasBind(canvas, bitmap);
	OH_Drawing_CanvasClear(canvas, 0x00000000);

	OH_Drawing_Brush *brush = OH_Drawing_BrushCreate();
	OH_Drawing_BrushSetColor(brush, 0xFFFFFFFF);
	OH_Drawing_CanvasAttachBrush(canvas, brush);
	OH_Drawing_TextBlob *blob = OH_Drawing_TextBlobCreateFromString(text.c_str(), font, TEXT_ENCODING_UTF8);
	if(blob != nullptr) {
		OH_Drawing_CanvasDrawTextBlob(canvas, blob, 2.0f, ascent + 2.0f);
		OH_Drawing_TextBlobDestroy(blob);
	}
	OH_Drawing_CanvasDetachBrush(canvas);

	GlyphTexture glyph;
	glyph.width = bitmapWidth / pixelDensity;
	glyph.height = bitmapHeight / pixelDensity;
	glyph.baseline = (ascent + 2.0f) / pixelDensity;
	glyph.texture = createTexture(OH_Drawing_BitmapGetPixels(bitmap), bitmapWidth, bitmapHeight);

	OH_Drawing_BrushDestroy(brush);
	OH_Drawing_CanvasDestroy(canvas);
	OH_Drawing_BitmapDestroy(bitmap);
	OH_Drawing_FontDestroy(font);

	auto inserted = glyphCache.emplace(key, glyph);
	return &inserted.first->second;
}

//--------------------------------------------------------------
float GLRenderer::drawText(const std::string &text, float x, float y, float size,
                           const std::string &fontPath) {
	if(text.empty()) {
		return 0.0f;
	}
	GlyphTexture *glyph = glyphFor(text, size, fontPath);
	if(glyph == nullptr || glyph->texture == 0) {
		return 0.0f;
	}
	emitQuad(x, y - glyph->baseline, glyph->width, glyph->height, glyph->texture);
	return glyph->width;
}

//--------------------------------------------------------------
float GLRenderer::measureText(const std::string &text, float size,
                              const std::string &fontPath) {
	if(text.empty()) {
		return 0.0f;
	}
	GlyphTexture *glyph = glyphFor(text, size, fontPath);
	return glyph != nullptr ? glyph->width : 0.0f;
}

} // namespace loaf
