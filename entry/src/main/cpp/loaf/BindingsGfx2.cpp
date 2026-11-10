#include "BindingsGfx2.h"

#include "Environment.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace loaf {

namespace {

const char *kPixelsMetatable = "loaf.Pixels";
const char *kTextureMetatable = "loaf.Texture";
const char *kPolylineMetatable = "loaf.Polyline";
const char *kPathMetatable = "loaf.Path";

struct LuaPixels {
	int width = 0;
	int height = 0;
	int channels = 4;
	std::vector<unsigned char> data;
};

struct LuaTexture {
	GLuint texture = 0;
	int width = 0;
	int height = 0;
	std::vector<unsigned char> pixels;
};

struct LuaPolyline {
	std::vector<float> points; // x,y,z
	bool closed = false;
};

struct LuaPath {
	std::vector<std::vector<float>> contours; // x,y
	std::vector<float> current;
	bool filled = true;
	Color fill {1.0f, 1.0f, 1.0f, 1.0f};
	Color stroke {1.0f, 1.0f, 1.0f, 1.0f};
	float strokeWidth = 1.0f;
	bool strokeEnabled = false;
};

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

LuaPixels *checkPixels(lua_State *L, int index) {
	return static_cast<LuaPixels *>(luaL_checkudata(L, index, kPixelsMetatable));
}

LuaTexture *checkTexture(lua_State *L, int index) {
	return static_cast<LuaTexture *>(luaL_checkudata(L, index, kTextureMetatable));
}

LuaPolyline *checkPolyline(lua_State *L, int index) {
	return static_cast<LuaPolyline *>(luaL_checkudata(L, index, kPolylineMetatable));
}

LuaPath *checkPath(lua_State *L, int index) {
	return static_cast<LuaPath *>(luaL_checkudata(L, index, kPathMetatable));
}

void setFunction(lua_State *L, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, -2, name);
}

//--------------------------------------------------------------
int pixels_new(lua_State *L) {
	LuaPixels *pixels = static_cast<LuaPixels *>(lua_newuserdatauv(L, sizeof(LuaPixels), 0));
	new(pixels) LuaPixels();
	luaL_getmetatable(L, kPixelsMetatable);
	lua_setmetatable(L, -2);
	if(lua_gettop(L) >= 4) {
		int width = (int)number(L, 2);
		int height = (int)number(L, 3);
		int channels = (int)number(L, 4, 4.0f);
		if(width > 0 && height > 0) {
			pixels->width = width;
			pixels->height = height;
			pixels->channels = std::max(1, std::min(4, channels));
			pixels->data.assign((size_t)width * height * pixels->channels, 0);
		}
	}
	return 1;
}

int pixels_gc(lua_State *L) {
	checkPixels(L, 1)->~LuaPixels();
	return 0;
}

int pixels_allocate(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	int width = (int)number(L, 2, 1.0f);
	int height = (int)number(L, 3, 1.0f);
	int channels = (int)number(L, 4, 4.0f);
	pixels->width = std::max(1, width);
	pixels->height = std::max(1, height);
	pixels->channels = std::max(1, std::min(4, channels));
	pixels->data.assign((size_t)pixels->width * pixels->height * pixels->channels, 0);
	return 0;
}

int pixels_getWidth(lua_State *L) { lua_pushinteger(L, checkPixels(L, 1)->width); return 1; }
int pixels_getHeight(lua_State *L) { lua_pushinteger(L, checkPixels(L, 1)->height); return 1; }
int pixels_getNumChannels(lua_State *L) { lua_pushinteger(L, checkPixels(L, 1)->channels); return 1; }
int pixels_getTotalBytes(lua_State *L) { lua_pushinteger(L, (lua_Integer)checkPixels(L, 1)->data.size()); return 1; }
int pixels_isAllocated(lua_State *L) { lua_pushboolean(L, !checkPixels(L, 1)->data.empty()); return 1; }

int pixels_getData(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	lua_pushlstring(L, reinterpret_cast<const char *>(pixels->data.data()), pixels->data.size());
	return 1;
}

int pixels_setData(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	size_t length = 0;
	const char *data = luaL_checklstring(L, 2, &length);
	size_t expected = (size_t)pixels->width * pixels->height * pixels->channels;
	pixels->data.assign((const unsigned char *)data, (const unsigned char *)data + std::min(length, expected));
	pixels->data.resize(expected, 0);
	return 0;
}

int pixels_clear(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	if(lua_gettop(L) >= 4) {
		int r = (int)number(L, 2), g = (int)number(L, 3), b = (int)number(L, 4);
		int a = (int)number(L, 5, 255.0f);
		for(int y = 0; y < pixels->height; ++y) {
			for(int x = 0; x < pixels->width; ++x) {
				size_t index = ((size_t)y * pixels->width + x) * pixels->channels;
				if(pixels->channels == 1) pixels->data[index] = (unsigned char)((r + g + b) / 3);
				else if(pixels->channels == 2) {
					pixels->data[index] = (unsigned char)((r + g + b) / 3);
					pixels->data[index + 1] = (unsigned char)a;
				}
				else {
					pixels->data[index] = (unsigned char)r;
					pixels->data[index + 1] = (unsigned char)g;
					pixels->data[index + 2] = (unsigned char)b;
					if(pixels->channels == 4) pixels->data[index + 3] = (unsigned char)a;
				}
			}
		}
		return 0;
	}
	std::fill(pixels->data.begin(), pixels->data.end(), 0);
	return 0;
}

int pixels_getColor(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	int x = (int)number(L, 2, 0.0f);
	int y = (int)number(L, 3, 0.0f);
	unsigned char rgba[4] = {0, 0, 0, 255};
	if(x >= 0 && y >= 0 && x < pixels->width && y < pixels->height) {
		size_t index = ((size_t)y * pixels->width + x) * pixels->channels;
		if(pixels->channels == 1 || pixels->channels == 2) {
			rgba[0] = rgba[1] = rgba[2] = pixels->data[index];
			if(pixels->channels == 2) rgba[3] = pixels->data[index + 1];
		}
		else if(pixels->channels >= 3) {
			rgba[0] = pixels->data[index];
			rgba[1] = pixels->data[index + 1];
			rgba[2] = pixels->data[index + 2];
			if(pixels->channels == 4) rgba[3] = pixels->data[index + 3];
		}
	}
	lua_newtable(L);
	lua_pushinteger(L, rgba[0]); lua_setfield(L, -2, "r");
	lua_pushinteger(L, rgba[1]); lua_setfield(L, -2, "g");
	lua_pushinteger(L, rgba[2]); lua_setfield(L, -2, "b");
	lua_pushinteger(L, rgba[3]); lua_setfield(L, -2, "a");
	luaL_getmetatable(L, "loaf.Color");
	lua_setmetatable(L, -2);
	return 1;
}

int pixels_setColor(lua_State *L) {
	LuaPixels *pixels = checkPixels(L, 1);
	int x = (int)number(L, 2, 0.0f);
	int y = (int)number(L, 3, 0.0f);
	int offset = 4;
	int r = 0, g = 0, b = 0, a = 255;
	if(lua_istable(L, 4)) {
		lua_getfield(L, 4, "r"); r = (int)number(L, -1); lua_pop(L, 1);
		lua_getfield(L, 4, "g"); g = (int)number(L, -1); lua_pop(L, 1);
		lua_getfield(L, 4, "b"); b = (int)number(L, -1); lua_pop(L, 1);
		lua_getfield(L, 4, "a"); a = (int)number(L, -1, 255.0f); lua_pop(L, 1);
	}
	else if(lua_gettop(L) >= 6) {
		r = (int)number(L, 4); g = (int)number(L, 5); b = (int)number(L, 6);
		a = (int)number(L, 7, 255.0f);
	}
	(void)offset;
	if(x < 0 || y < 0 || x >= pixels->width || y >= pixels->height) {
		return 0;
	}
	size_t index = ((size_t)y * pixels->width + x) * pixels->channels;
	if(pixels->channels == 1) {
		pixels->data[index] = (unsigned char)((r + g + b) / 3);
	}
	else if(pixels->channels == 2) {
		pixels->data[index] = (unsigned char)((r + g + b) / 3);
		pixels->data[index + 1] = (unsigned char)a;
	}
	else {
		pixels->data[index] = (unsigned char)r;
		pixels->data[index + 1] = (unsigned char)g;
		pixels->data[index + 2] = (unsigned char)b;
		if(pixels->channels == 4) pixels->data[index + 3] = (unsigned char)a;
	}
	return 0;
}

//--------------------------------------------------------------
int texture_new(lua_State *L) {
	LuaTexture *texture = static_cast<LuaTexture *>(lua_newuserdatauv(L, sizeof(LuaTexture), 0));
	new(texture) LuaTexture();
	luaL_getmetatable(L, kTextureMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int texture_gc(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	if(texture->texture != 0 && env().renderer != nullptr) {
		env().renderer->deleteTexture(texture->texture);
	}
	texture->texture = 0;
	return 0;
}

int texture_allocate(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	int width = (int)number(L, 2, 1.0f);
	int height = (int)number(L, 3, 1.0f);
	if(env().renderer == nullptr || width <= 0 || height <= 0) {
		return 0;
	}
	if(texture->texture != 0) {
		env().renderer->deleteTexture(texture->texture);
	}
	std::vector<unsigned char> blank((size_t)width * height * 4, 0);
	texture->texture = env().renderer->createTexture(blank.data(), width, height);
	texture->width = width;
	texture->height = height;
	return 0;
}

int texture_loadData(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	std::vector<unsigned char> rgba;
	int width = 0;
	int height = 0;
	if(lua_isuserdata(L, 2)) {
		LuaPixels *pixels = checkPixels(L, 2);
		width = pixels->width;
		height = pixels->height;
		rgba.resize((size_t)width * height * 4);
		for(int i = 0; i < width * height; ++i) {
			size_t source = (size_t)i * pixels->channels;
			rgba[i * 4] = pixels->data[source];
			rgba[i * 4 + 1] = pixels->channels >= 3 ? pixels->data[source + 1] : pixels->data[source];
			rgba[i * 4 + 2] = pixels->channels >= 3 ? pixels->data[source + 2] : pixels->data[source];
			rgba[i * 4 + 3] = pixels->channels == 4 ? pixels->data[source + 3] : 255;
		}
	}
	else if(lua_isstring(L, 2)) {
		width = (int)number(L, 3);
		height = (int)number(L, 4);
		size_t length = 0;
		const char *data = lua_tolstring(L, 2, &length);
		rgba.assign((const unsigned char *)data, (const unsigned char *)data + length);
		rgba.resize((size_t)width * height * 4, 0);
	}
	if(width <= 0 || height <= 0) {
		return 0;
	}
	if(texture->texture != 0) {
		env().renderer->deleteTexture(texture->texture);
	}
	texture->texture = env().renderer->createTexture(rgba.data(), width, height);
	texture->width = width;
	texture->height = height;
	texture->pixels = rgba;
	return 0;
}

int texture_readToPixels(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	if(texture->width <= 0 || texture->height <= 0) {
		lua_pushnil(L);
		return 1;
	}
	if(texture->pixels.size() != (size_t)texture->width * texture->height * 4) {
		texture->pixels.assign((size_t)texture->width * texture->height * 4, 0);
	}
	pushPixelsUserdata(L, texture->width, texture->height, 4, texture->pixels);
	return 1;
}

int texture_getWidth(lua_State *L) { lua_pushinteger(L, checkTexture(L, 1)->width); return 1; }
int texture_getHeight(lua_State *L) { lua_pushinteger(L, checkTexture(L, 1)->height); return 1; }
int texture_isAllocated(lua_State *L) { lua_pushboolean(L, checkTexture(L, 1)->texture != 0); return 1; }

int texture_draw(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	if(texture->texture == 0 || env().renderer == nullptr) {
		return 0;
	}
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)texture->width);
	float h = number(L, 5, (float)texture->height);
	env().renderer->drawTexture(texture->texture, x, y, w, h);
	return 0;
}

int texture_drawSubsection(lua_State *L) {
	LuaTexture *texture = checkTexture(L, 1);
	if(texture->texture == 0 || env().renderer == nullptr || texture->width <= 0 || texture->height <= 0) {
		return 0;
	}
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4);
	float h = number(L, 5);
	float sx = number(L, 6);
	float sy = number(L, 7);
	float sw = number(L, 8, (float)texture->width);
	float sh = number(L, 9, (float)texture->height);
	float u0 = sx / texture->width;
	float v0 = sy / texture->height;
	float u1 = (sx + sw) / texture->width;
	float v1 = (sy + sh) / texture->height;
	env().renderer->drawTextureRegion(texture->texture, x, y, w, h, u0, v0, u1, v1);
	return 0;
}

//--------------------------------------------------------------
int polyline_new(lua_State *L) {
	LuaPolyline *line = static_cast<LuaPolyline *>(lua_newuserdatauv(L, sizeof(LuaPolyline), 0));
	new(line) LuaPolyline();
	luaL_getmetatable(L, kPolylineMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int polyline_gc(lua_State *L) { checkPolyline(L, 1)->~LuaPolyline(); return 0; }

int polyline_addVertex(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	line->points.push_back(number(L, 2));
	line->points.push_back(number(L, 3));
	line->points.push_back(number(L, 4, 0.0f));
	return 0;
}

int polyline_addVertices(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	int count = (int)lua_rawlen(L, 2);
	for(int i = 1; i <= count; ++i) {
		lua_rawgeti(L, 2, i);
		if(lua_istable(L, -1)) {
			lua_getfield(L, -1, "x"); float x = number(L, -1); lua_pop(L, 1);
			lua_getfield(L, -1, "y"); float y = number(L, -1); lua_pop(L, 1);
			lua_getfield(L, -1, "z"); float z = number(L, -1, 0.0f); lua_pop(L, 1);
			line->points.push_back(x);
			line->points.push_back(y);
			line->points.push_back(z);
		}
		lua_pop(L, 1);
	}
	return 0;
}

int polyline_clear(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	line->points.clear();
	line->closed = false;
	return 0;
}

int polyline_size(lua_State *L) { lua_pushinteger(L, (lua_Integer)(checkPolyline(L, 1)->points.size() / 3)); return 1; }

int polyline_close(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	if(line->points.size() >= 6) {
		line->points.push_back(line->points[0]);
		line->points.push_back(line->points[1]);
		line->points.push_back(line->points[2]);
		line->closed = true;
	}
	return 0;
}

int polyline_getLength(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	float length = 0.0f;
	for(size_t i = 3; i + 2 < line->points.size(); i += 3) {
		float dx = line->points[i] - line->points[i - 3];
		float dy = line->points[i + 1] - line->points[i - 2];
		length += std::sqrt(dx * dx + dy * dy);
	}
	lua_pushnumber(L, length);
	return 1;
}

int polyline_draw(lua_State *L) {
	LuaPolyline *line = checkPolyline(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	for(size_t i = 3; i + 2 < line->points.size(); i += 3) {
		env().renderer->drawLine(line->points[i - 3], line->points[i - 2],
		                         line->points[i], line->points[i + 1]);
	}
	return 0;
}

//--------------------------------------------------------------
int path_new(lua_State *L) {
	LuaPath *path = static_cast<LuaPath *>(lua_newuserdatauv(L, sizeof(LuaPath), 0));
	new(path) LuaPath();
	luaL_getmetatable(L, kPathMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int path_gc(lua_State *L) { checkPath(L, 1)->~LuaPath(); return 0; }

int path_clear(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	path->contours.clear();
	path->current.clear();
	return 0;
}

int path_moveTo(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(!path->current.empty()) {
		path->contours.push_back(path->current);
	}
	path->current.clear();
	path->current.push_back(number(L, 2));
	path->current.push_back(number(L, 3));
	return 0;
}

int path_lineTo(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	path->current.push_back(number(L, 2));
	path->current.push_back(number(L, 3));
	return 0;
}

int path_curveTo(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(path->current.size() < 2) {
		return 0;
	}
	float x0 = path->current[path->current.size() - 2];
	float y0 = path->current[path->current.size() - 1];
	float cx = number(L, 2), cy = number(L, 3);
	float x1 = number(L, 4), y1 = number(L, 5);
	for(int i = 1; i <= 12; ++i) {
		float t = (float)i / 12.0f;
		float mt = 1.0f - t;
		path->current.push_back(mt * mt * x0 + 2 * mt * t * cx + t * t * x1);
		path->current.push_back(mt * mt * y0 + 2 * mt * t * cy + t * t * y1);
	}
	return 0;
}

int path_bezierTo(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(path->current.size() < 2) {
		return 0;
	}
	float x0 = path->current[path->current.size() - 2];
	float y0 = path->current[path->current.size() - 1];
	float c1x = number(L, 2), c1y = number(L, 3);
	float c2x = number(L, 4), c2y = number(L, 5);
	float x1 = number(L, 6), y1 = number(L, 7);
	for(int i = 1; i <= 16; ++i) {
		float t = (float)i / 16.0f;
		float mt = 1.0f - t;
		path->current.push_back(mt * mt * mt * x0 + 3 * mt * mt * t * c1x +
		                        3 * mt * t * t * c2x + t * t * t * x1);
		path->current.push_back(mt * mt * mt * y0 + 3 * mt * mt * t * c1y +
		                        3 * mt * t * t * c2y + t * t * t * y1);
	}
	return 0;
}

int path_close(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(path->current.size() >= 4) {
		path->current.push_back(path->current[0]);
		path->current.push_back(path->current[1]);
		path->contours.push_back(path->current);
		path->current.clear();
	}
	return 0;
}

int path_setFilled(lua_State *L) {
	checkPath(L, 1)->filled = lua_toboolean(L, 2) != 0;
	return 0;
}

int path_setFillColor(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(lua_istable(L, 2)) {
		lua_getfield(L, 2, "r"); path->fill.r = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "g"); path->fill.g = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "b"); path->fill.b = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "a"); path->fill.a = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
	}
	else {
		path->fill.r = number(L, 2, 255.0f) / 255.0f;
		path->fill.g = number(L, 3, 255.0f) / 255.0f;
		path->fill.b = number(L, 4, 255.0f) / 255.0f;
		path->fill.a = number(L, 5, 255.0f) / 255.0f;
	}
	return 0;
}

int path_setStrokeColor(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(lua_istable(L, 2)) {
		lua_getfield(L, 2, "r"); path->stroke.r = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "g"); path->stroke.g = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "b"); path->stroke.b = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, 2, "a"); path->stroke.a = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
	}
	else {
		path->stroke.r = number(L, 2, 255.0f) / 255.0f;
		path->stroke.g = number(L, 3, 255.0f) / 255.0f;
		path->stroke.b = number(L, 4, 255.0f) / 255.0f;
		path->stroke.a = number(L, 5, 255.0f) / 255.0f;
	}
	path->strokeEnabled = true;
	return 0;
}

int path_setStrokeWidth(lua_State *L) {
	checkPath(L, 1)->strokeWidth = number(L, 2, 1.0f);
	return 0;
}

int path_draw(lua_State *L) {
	LuaPath *path = checkPath(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	std::vector<std::vector<float>> contours = path->contours;
	if(!path->current.empty()) {
		contours.push_back(path->current);
	}
	Color previous = env().renderer->color();
	for(const auto &contour : contours) {
		if(contour.size() < 4) continue;
		if(path->filled) {
			env().renderer->setColor(path->fill);
			env().renderer->beginShape();
			for(size_t i = 0; i + 1 < contour.size(); i += 2) {
				env().renderer->vertex(contour[i], contour[i + 1]);
			}
			env().renderer->endShape(true);
		}
		if(path->strokeEnabled) {
			env().renderer->setColor(path->stroke);
			env().renderer->setLineWidth(path->strokeWidth);
			for(size_t i = 2; i + 1 < contour.size(); i += 2) {
				env().renderer->drawLine(contour[i - 2], contour[i - 1], contour[i], contour[i + 1]);
			}
		}
	}
	env().renderer->setColor(previous);
	return 0;
}

//--------------------------------------------------------------
void registerPixels(lua_State *L) {
	luaL_newmetatable(L, kPixelsMetatable);
	lua_pushcfunction(L, pixels_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "allocate", pixels_allocate);
	setFunction(L, "getWidth", pixels_getWidth);
	setFunction(L, "getHeight", pixels_getHeight);
	setFunction(L, "getNumChannels", pixels_getNumChannels);
	setFunction(L, "getTotalBytes", pixels_getTotalBytes);
	setFunction(L, "isAllocated", pixels_isAllocated);
	setFunction(L, "getData", pixels_getData);
	setFunction(L, "setData", pixels_setData);
	setFunction(L, "getColor", pixels_getColor);
	setFunction(L, "setColor", pixels_setColor);
	setFunction(L, "clear", pixels_clear);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerTexture(lua_State *L) {
	luaL_newmetatable(L, kTextureMetatable);
	lua_pushcfunction(L, texture_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "allocate", texture_allocate);
	setFunction(L, "loadData", texture_loadData);
	setFunction(L, "getWidth", texture_getWidth);
	setFunction(L, "getHeight", texture_getHeight);
	setFunction(L, "isAllocated", texture_isAllocated);
	setFunction(L, "draw", texture_draw);
	setFunction(L, "drawSubsection", texture_drawSubsection);
	setFunction(L, "readToPixels", texture_readToPixels);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerPolyline(lua_State *L) {
	luaL_newmetatable(L, kPolylineMetatable);
	lua_pushcfunction(L, polyline_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "addVertex", polyline_addVertex);
	setFunction(L, "addVertices", polyline_addVertices);
	setFunction(L, "clear", polyline_clear);
	setFunction(L, "size", polyline_size);
	setFunction(L, "close", polyline_close);
	setFunction(L, "getLength", polyline_getLength);
	setFunction(L, "draw", polyline_draw);
	setFunction(L, "drawWireframe", polyline_draw);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerPath(lua_State *L) {
	luaL_newmetatable(L, kPathMetatable);
	lua_pushcfunction(L, path_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "moveTo", path_moveTo);
	setFunction(L, "lineTo", path_lineTo);
	setFunction(L, "curveTo", path_curveTo);
	setFunction(L, "bezierTo", path_bezierTo);
	setFunction(L, "close", path_close);
	setFunction(L, "clear", path_clear);
	setFunction(L, "setFilled", path_setFilled);
	setFunction(L, "setFillColor", path_setFillColor);
	setFunction(L, "setStrokeColor", path_setStrokeColor);
	setFunction(L, "setStrokeWidth", path_setStrokeWidth);
	setFunction(L, "draw", path_draw);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

} // namespace

//--------------------------------------------------------------
void registerGfx2Bindings(lua_State *L, int ofTable) {
	registerPixels(L);
	registerTexture(L);
	registerPolyline(L);
	registerPath(L);
	lua_pushcfunction(L, pixels_new);
	lua_setfield(L, ofTable, "Pixels");
	lua_pushcfunction(L, texture_new);
	lua_setfield(L, ofTable, "Texture");
	lua_pushcfunction(L, polyline_new);
	lua_setfield(L, ofTable, "Polyline");
	lua_pushcfunction(L, path_new);
	lua_setfield(L, ofTable, "Path");
}

//--------------------------------------------------------------
int pushPixelsUserdata(lua_State *L, int width, int height, int channels,
                       const std::vector<unsigned char> &data) {
	LuaPixels *pixels = static_cast<LuaPixels *>(lua_newuserdatauv(L, sizeof(LuaPixels), 0));
	new(pixels) LuaPixels();
	pixels->width = width;
	pixels->height = height;
	pixels->channels = channels;
	pixels->data = data;
	luaL_getmetatable(L, kPixelsMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

//--------------------------------------------------------------
bool readPixelsUserdata(lua_State *L, int index, int &width, int &height,
                        int &channels, std::vector<unsigned char> &data) {
	LuaPixels *pixels = static_cast<LuaPixels *>(luaL_testudata(L, index, kPixelsMetatable));
	if(pixels == nullptr) {
		return false;
	}
	width = pixels->width;
	height = pixels->height;
	channels = pixels->channels;
	data = pixels->data;
	return true;
}

} // namespace loaf
