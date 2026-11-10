#include "Shader.h"

#include <fstream>
#include <sstream>

#define LOG_TAG "loafcore"
#include "../Log.h"

namespace loaf {

namespace {

std::string readFile(const std::string &path) {
	std::ifstream stream(path, std::ios::binary);
	if(!stream) {
		return {};
	}
	std::ostringstream text;
	text << stream.rdbuf();
	return text.str();
}

/// binds the program for the duration of a call, restoring the previous one
struct ScopedProgram {
	GLuint program = 0;
	GLint previous = 0;

	explicit ScopedProgram(GLuint value) : program(value) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
		if(program != 0 && (GLuint)previous != program) {
			glUseProgram(program);
		}
	}

	~ScopedProgram() {
		if(program != 0 && (GLuint)previous != program) {
			glUseProgram((GLuint)previous);
		}
	}
};

} // namespace

//--------------------------------------------------------------
const char *Shader::defaultVertexSource() {
	return R"(#version 300 es
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
}

//--------------------------------------------------------------
Shader::~Shader() {
	release();
}

//--------------------------------------------------------------
GLuint Shader::compile(GLenum type, const std::string &source, std::string &error) {
	GLuint shader = glCreateShader(type);
	const char *text = source.c_str();
	glShaderSource(shader, 1, &text, nullptr);
	glCompileShader(shader);
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if(ok != GL_TRUE) {
		char log[2048] = {0};
		glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
		error = log;
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

//--------------------------------------------------------------
bool Shader::loadSources(const std::string &vertexSource, const std::string &fragmentSource) {
	release();
	std::string error;
	GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, error);
	if(vertex == 0) {
		LOGE("vertex shader: %{public}s", error.c_str());
		return false;
	}
	GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, error);
	if(fragment == 0) {
		LOGE("fragment shader: %{public}s", error.c_str());
		glDeleteShader(vertex);
		return false;
	}
	GLuint linked = glCreateProgram();
	glAttachShader(linked, vertex);
	glAttachShader(linked, fragment);

	// allow both of's naming and the shader-toy style naming
	glBindAttribLocation(linked, 0, "position");
	glBindAttribLocation(linked, 1, "texcoord");
	glBindAttribLocation(linked, 2, "color");
	glLinkProgram(linked);
	glDeleteShader(vertex);
	glDeleteShader(fragment);

	GLint ok = GL_FALSE;
	glGetProgramiv(linked, GL_LINK_STATUS, &ok);
	if(ok != GL_TRUE) {
		char log[2048] = {0};
		glGetProgramInfoLog(linked, sizeof(log) - 1, nullptr, log);
		LOGE("shader link: %{public}s", log);
		glDeleteProgram(linked);
		return false;
	}
	program = linked;
	return true;
}

//--------------------------------------------------------------
bool Shader::loadFiles(const std::string &vertexPath, const std::string &fragmentPath) {
	std::string vertex = readFile(vertexPath);
	std::string fragment = readFile(fragmentPath);
	if(vertex.empty()) {
		LOGE("could not read %{public}s", vertexPath.c_str());
		return false;
	}
	if(fragment.empty()) {
		LOGE("could not read %{public}s", fragmentPath.c_str());
		return false;
	}
	return loadSources(vertex, fragment);
}

//--------------------------------------------------------------
bool Shader::loadFragmentOnly(const std::string &fragmentSource) {
	return loadSources(defaultVertexSource(), fragmentSource);
}

//--------------------------------------------------------------
void Shader::release() {
	if(program != 0) {
		glDeleteProgram(program);
		program = 0;
	}
}

//--------------------------------------------------------------
void Shader::begin(GLRenderer &renderer) {
	if(program == 0) {
		return;
	}
	renderer.bindExternalProgram(program);
}

//--------------------------------------------------------------
void Shader::end(GLRenderer &renderer) {
	renderer.restoreProgram();
}

//--------------------------------------------------------------
void Shader::setUniform1f(const char *name, float value) {
	if(program == 0) return;
	ScopedProgram scope(program);
	glUniform1f(glGetUniformLocation(program, name), value);
}

void Shader::setUniform2f(const char *name, float x, float y) {
	if(program == 0) return;
	ScopedProgram scope(program);
	glUniform2f(glGetUniformLocation(program, name), x, y);
}

void Shader::setUniform3f(const char *name, float x, float y, float z) {
	if(program == 0) return;
	ScopedProgram scope(program);
	glUniform3f(glGetUniformLocation(program, name), x, y, z);
}

void Shader::setUniform4f(const char *name, float x, float y, float z, float w) {
	if(program == 0) return;
	ScopedProgram scope(program);
	glUniform4f(glGetUniformLocation(program, name), x, y, z, w);
}

void Shader::setUniform1i(const char *name, int value) {
	if(program == 0) return;
	ScopedProgram scope(program);
	glUniform1i(glGetUniformLocation(program, name), value);
}

//--------------------------------------------------------------
void Shader::setUniformTexture(const char *name, GLuint texture, int slot) {
	if(program == 0) {
		return;
	}
	ScopedProgram scope(program);
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(glGetUniformLocation(program, name), slot);
}

} // namespace loaf
