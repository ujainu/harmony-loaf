#pragma once

#include "GLRenderer.h"

#include <GLES3/gl3.h>

#include <string>

namespace loaf {

class Shader {
public:

	~Shader();

	bool loadFiles(const std::string &vertexPath, const std::string &fragmentPath);
	bool loadSources(const std::string &vertexSource, const std::string &fragmentSource);
	bool loadFragmentOnly(const std::string &fragmentSource);
	void release();

	void begin(GLRenderer &renderer);
	void end(GLRenderer &renderer);

	void setUniform1f(const char *name, float value);
	void setUniform2f(const char *name, float x, float y);
	void setUniform3f(const char *name, float x, float y, float z);
	void setUniform4f(const char *name, float x, float y, float z, float w);
	void setUniform1i(const char *name, int value);
	void setUniformTexture(const char *name, GLuint texture, int slot);

	bool valid() const { return program != 0; }
	GLuint id() const { return program; }

	static const char *defaultVertexSource();

private:

	static GLuint compile(GLenum type, const std::string &source, std::string &error);

	GLuint program = 0;
};

} // namespace loaf
