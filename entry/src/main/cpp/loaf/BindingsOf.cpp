#include "Environment.h"
#include "BindingsFile.h"
#include "BindingsShader.h"
#include "BindingsAudio.h"
#include "BindingsVideo.h"
#include "Bindings3D.h"
#include "BindingsGfx2.h"
#include "BindingsMisc.h"
#include "BindingsGfx2.h"
#include "BindingsAudioStream.h"
#include "BindingsGfx3.h"
#include "LuaVM.h"

#include <lua.hpp>

#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace loaf {

namespace {

//--------------------------------------------------------------
float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

bool isTable(lua_State *L, int index) {
	return lua_istable(L, index);
}

Color colorFromArgs(lua_State *L, int start, int count, Color fallback = Color{1, 1, 1, 1}) {
	if(count <= 0) {
		return fallback;
	}
	if(isTable(L, start)) {
		Color color;
		lua_getfield(L, start, "r");
		color.r = number(L, -1, 255.0f) / 255.0f;
		lua_pop(L, 1);
		lua_getfield(L, start, "g");
		color.g = number(L, -1, 255.0f) / 255.0f;
		lua_pop(L, 1);
		lua_getfield(L, start, "b");
		color.b = number(L, -1, 255.0f) / 255.0f;
		lua_pop(L, 1);
		lua_getfield(L, start, "a");
		color.a = number(L, -1, 255.0f) / 255.0f;
		lua_pop(L, 1);
		return color;
	}
	if(count == 1) {
		float gray = number(L, start, 255.0f) / 255.0f;
		return Color{gray, gray, gray, 1.0f};
	}
	Color color;
	color.r = number(L, start, 255.0f) / 255.0f;
	color.g = number(L, start + 1, 255.0f) / 255.0f;
	color.b = number(L, start + 2, 255.0f) / 255.0f;
	color.a = count >= 4 ? number(L, start + 3, 255.0f) / 255.0f : 1.0f;
	return color;
}

Color hexColor(uint32_t hex, float alpha = 1.0f) {
	return Color{((hex >> 16) & 0xff) / 255.0f,
	             ((hex >> 8) & 0xff) / 255.0f,
	             (hex & 0xff) / 255.0f,
	             alpha};
}

GLRenderer *renderer() {
	return env().renderer;
}


//--------------------------------------------------------------
struct LuaImage {
GLuint texture = 0;
	int width = 0;
	int height = 0;
	int imageType = 0;
	std::vector<unsigned char> pixels;
};

struct LuaFont {
	float size = 12.0f;
	float lineHeight = 0.0f;
	std::string name;
};

/// returns the font file only when it actually exists, otherwise the system font is used
const std::string &fontFile(const LuaFont &font) {
	static const std::string empty;
	if(!font.name.empty() && access(font.name.c_str(), F_OK) == 0) {
		return font.name;
	}
	return empty;
}

const char *kImageMetatable = "loaf.Image";
const char *kFontMetatable = "loaf.Font";
const char *kFboMetatable = "loaf.Fbo";

struct LuaFbo {
	GLuint fbo = 0;
	int width = 0;
	int height = 0;
};

LuaFbo *checkFbo(lua_State *L, int index) {
	return static_cast<LuaFbo *>(luaL_checkudata(L, index, kFboMetatable));
}

LuaImage *checkImage(lua_State *L, int index) {
	return static_cast<LuaImage *>(luaL_checkudata(L, index, kImageMetatable));
}

LuaFont *checkFont(lua_State *L, int index) {
	return static_cast<LuaFont *>(luaL_checkudata(L, index, kFontMetatable));
}

//--------------------------------------------------------------
int l_image_getWidth(lua_State *L) {
	lua_pushinteger(L, checkImage(L, 1)->width);
	return 1;
}

//--------------------------------------------------------------
int l_fbo_new(lua_State *L) {
	LuaFbo *fbo = static_cast<LuaFbo *>(lua_newuserdatauv(L, sizeof(LuaFbo), 0));
	new(fbo) LuaFbo();
	luaL_getmetatable(L, kFboMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int l_fbo_gc(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	if(fbo->fbo != 0) {
		renderer()->destroyFramebuffer(fbo->fbo);
		fbo->fbo = 0;
	}
	return 0;
}

int l_fbo_allocate(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	int width = (int)number(L, 2, 256.0f);
	int height = (int)number(L, 3, 256.0f);
	if(fbo->fbo != 0) {
		renderer()->destroyFramebuffer(fbo->fbo);
	}
	fbo->fbo = renderer()->createFramebuffer(width, height);
	fbo->width = width;
	fbo->height = height;
	lua_pushboolean(L, fbo->fbo != 0);
	return 1;
}

int l_fbo_begin(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	bool clear = lua_gettop(L) < 2 || lua_toboolean(L, 2) != 0;
	if(fbo->fbo != 0) {
		renderer()->beginFramebuffer(fbo->fbo, clear);
	}
	return 0;
}

int l_fbo_end(lua_State *L) {
	renderer()->endFramebuffer();
	return 0;
}

int l_fbo_draw(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	if(fbo->fbo == 0) {
		return 0;
	}
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)fbo->width);
	float h = number(L, 5, (float)fbo->height);
	renderer()->drawFramebuffer(fbo->fbo, x, y, w, h);
	return 0;
}

int l_fbo_clear(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	if(fbo->fbo == 0) {
		return 0;
	}
	Color color = colorFromArgs(L, 2, lua_gettop(L) - 1, Color{0, 0, 0, 0});
	renderer()->beginFramebuffer(fbo->fbo, false);
	renderer()->clear(color);
	renderer()->endFramebuffer();
	return 0;
}

int l_fbo_getWidth(lua_State *L) {
	lua_pushinteger(L, checkFbo(L, 1)->width);
	return 1;
}

int l_fbo_getHeight(lua_State *L) {
	lua_pushinteger(L, checkFbo(L, 1)->height);
	return 1;
}

int l_fbo_isAllocated(lua_State *L) {
	lua_pushboolean(L, checkFbo(L, 1)->fbo != 0);
	return 1;
}

int l_fbo_getTexture(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkFbo(L, 1)->fbo);
	return 1;
}

int l_fbo_getPixel(lua_State *L) {
	LuaFbo *fbo = checkFbo(L, 1);
	int x = (int)number(L, 2, 0.0f);
	int y = (int)number(L, 3, 0.0f);
	Color color = fbo->fbo != 0 ? renderer()->readFramebufferPixel(fbo->fbo, x, y)
	                            : Color{0, 0, 0, 0};
	lua_newtable(L);
	lua_pushnumber(L, color.r * 255.0f);
	lua_setfield(L, -2, "r");
	lua_pushnumber(L, color.g * 255.0f);
	lua_setfield(L, -2, "g");
	lua_pushnumber(L, color.b * 255.0f);
	lua_setfield(L, -2, "b");
	lua_pushnumber(L, color.a * 255.0f);
	lua_setfield(L, -2, "a");
	luaL_getmetatable(L, "loaf.Color");
	lua_setmetatable(L, -2);
	return 1;
}

int l_image_getHeight(lua_State *L) {
	lua_pushinteger(L, checkImage(L, 1)->height);
	return 1;
}

int l_image_isValid(lua_State *L) {
	lua_pushboolean(L, checkImage(L, 1)->texture != 0);
	return 1;
}

int l_image_draw(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)image->width);
	float h = number(L, 5, (float)image->height);
	renderer()->drawTexture(image->texture, x, y, w, h);
	return 0;
}

int l_image_gc(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	if(image->texture != 0) {
		renderer()->deleteTexture(image->texture);
		image->texture = 0;
	}
	return 0;
}

int l_image_load(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	const char *path = luaL_checkstring(L, 2);
	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
	if(pixels == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if(image->texture != 0) {
		renderer()->deleteTexture(image->texture);
	}
	image->texture = renderer()->createTexture(pixels, width, height);
	image->width = width;
	image->height = height;
	image->pixels.assign(pixels, pixels + (size_t)width * height * 4);
	stbi_image_free(pixels);
	lua_pushboolean(L, 1);
	return 1;
}

int l_image_isLoaded(lua_State *L) {
	lua_pushboolean(L, checkImage(L, 1)->texture != 0);
	return 1;
}

int l_image_setImageType(lua_State *L) {
	checkImage(L, 1)->imageType = (int)number(L, 2);
	return 0;
}

int l_image_clear(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	if(image->texture != 0) {
		renderer()->deleteTexture(image->texture);
		image->texture = 0;
	}
	image->width = 0;
	image->height = 0;
	image->pixels.clear();
	return 0;
}

int l_image_allocate(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	int width = (int)number(L, 2, 1.0f);
	int height = (int)number(L, 3, 1.0f);
	if(width <= 0 || height <= 0 || renderer() == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if(image->texture != 0) {
		renderer()->deleteTexture(image->texture);
	}
	image->width = width;
	image->height = height;
	image->pixels.assign((size_t)width * height * 4, 0);
	image->texture = renderer()->createTexture(image->pixels.data(), width, height);
	lua_pushboolean(L, image->texture != 0);
	return 1;
}

int l_image_setFromPixels(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	int width = 0, height = 0, channels = 0;
	std::vector<unsigned char> data;
	if(!readPixelsUserdata(L, 2, width, height, channels, data) ||
	   width <= 0 || height <= 0 || renderer() == nullptr) {
		lua_pushboolean(L, 0);
		return 1;
	}
	std::vector<unsigned char> rgba((size_t)width * height * 4, 0);
	for(int i = 0; i < width * height; ++i) {
		size_t source = (size_t)i * channels;
		rgba[i * 4] = data[source];
		rgba[i * 4 + 1] = channels >= 3 ? data[source + 1] : data[source];
		rgba[i * 4 + 2] = channels >= 3 ? data[source + 2] : data[source];
		rgba[i * 4 + 3] = channels == 4 ? data[source + 3] : 255;
	}
	if(image->texture != 0) {
		renderer()->deleteTexture(image->texture);
	}
	image->width = width;
	image->height = height;
	image->pixels = rgba;
	image->texture = renderer()->createTexture(rgba.data(), width, height);
	lua_pushboolean(L, image->texture != 0);
	return 1;
}

int l_image_getPixels(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	if(image->width <= 0 || image->height <= 0) {
		lua_pushnil(L);
		return 1;
	}
	std::vector<unsigned char> data = image->pixels;
	data.resize((size_t)image->width * image->height * 4, 0);
	pushPixelsUserdata(L, image->width, image->height, 4, data);
	return 1;
}

int l_image_update(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	if(image->texture != 0 && !image->pixels.empty() && renderer() != nullptr) {
		renderer()->deleteTexture(image->texture);
		image->texture = renderer()->createTexture(image->pixels.data(), image->width, image->height);
	}
	return 0;
}

int l_image_resize(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	image->width = (int)number(L, 2, (float)image->width);
	image->height = (int)number(L, 3, (float)image->height);
	return 0;
}

int l_image_getColor(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	int x = (int)number(L, 2, 0.0f);
	int y = (int)number(L, 3, 0.0f);
	Color color {0, 0, 0, 0};
	if(x >= 0 && y >= 0 && x < image->width && y < image->height &&
	   image->pixels.size() >= (size_t)((y * image->width + x) * 4 + 4)) {
		size_t index = (size_t)(y * image->width + x) * 4;
		color.r = image->pixels[index] / 255.0f;
		color.g = image->pixels[index + 1] / 255.0f;
		color.b = image->pixels[index + 2] / 255.0f;
		color.a = image->pixels[index + 3] / 255.0f;
	}
	lua_newtable(L);
	lua_pushnumber(L, color.r * 255.0f);
	lua_setfield(L, -2, "r");
	lua_pushnumber(L, color.g * 255.0f);
	lua_setfield(L, -2, "g");
	lua_pushnumber(L, color.b * 255.0f);
	lua_setfield(L, -2, "b");
	lua_pushnumber(L, color.a * 255.0f);
	lua_setfield(L, -2, "a");
	luaL_getmetatable(L, "loaf.Color");
	lua_setmetatable(L, -2);
	return 1;
}

float tableNumber(lua_State *L, const char *key) {
	lua_getfield(L, 1, key);
	float value = number(L, -1);
	lua_pop(L, 1);
	return value;
}

int l_color_getBrightness(lua_State *L) {
	float r = tableNumber(L, "r");
	float g = tableNumber(L, "g");
	float b = tableNumber(L, "b");
	lua_pushnumber(L, std::max(r, std::max(g, b)) / 255.0);
	return 1;
}

int l_color_getLightness(lua_State *L) {
	float r = tableNumber(L, "r");
	float g = tableNumber(L, "g");
	float b = tableNumber(L, "b");
	lua_pushnumber(L, (std::max(r, std::max(g, b)) + std::min(r, std::min(g, b))) / 2.0 / 255.0);
	return 1;
}

int l_color_getHex(lua_State *L) {
	int r = (int)tableNumber(L, "r");
	int g = (int)tableNumber(L, "g");
	int b = (int)tableNumber(L, "b");
	lua_pushinteger(L, (r << 16) | (g << 8) | b);
	return 1;
}

int l_color_getSaturation(lua_State *L) {
	float r = tableNumber(L, "r") / 255.0f;
	float g = tableNumber(L, "g") / 255.0f;
	float b = tableNumber(L, "b") / 255.0f;
	float maximum = std::max(r, std::max(g, b));
	float minimum = std::min(r, std::min(g, b));
	float delta = maximum - minimum;
	lua_pushnumber(L, maximum <= 0.0f ? 0.0f : delta / maximum);
	return 1;
}

int l_color_getHue(lua_State *L) {
	float r = tableNumber(L, "r") / 255.0f;
	float g = tableNumber(L, "g") / 255.0f;
	float b = tableNumber(L, "b") / 255.0f;
	float maximum = std::max(r, std::max(g, b));
	float minimum = std::min(r, std::min(g, b));
	float delta = maximum - minimum;
	float hue = 0.0f;
	if(delta > 0.00001f) {
		if(maximum == r) {
			hue = std::fmod((g - b) / delta, 6.0f);
		}
		else if(maximum == g) {
			hue = (b - r) / delta + 2.0f;
		}
		else {
			hue = (r - g) / delta + 4.0f;
		}
		hue /= 6.0f;
		if(hue < 0.0f) {
			hue += 1.0f;
		}
	}
	lua_pushnumber(L, hue * 255.0f);
	return 1;
}

int l_image_new(lua_State *L) {
	std::string path = luaL_optstring(L, 1, "");
	LuaImage *image = static_cast<LuaImage *>(lua_newuserdatauv(L, sizeof(LuaImage), 0));
	new(image) LuaImage();
	luaL_getmetatable(L, kImageMetatable);
	lua_setmetatable(L, -2);
	if(!path.empty()) {
		lua_pushvalue(L, -1);
		lua_pushstring(L, path.c_str());
		l_image_load(L);
		lua_pop(L, 1);
	}
	return 1;
}

int l_font_load(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	font->name = luaL_optstring(L, 2, "");
	font->size = number(L, 3, font->size > 0 ? font->size : 12.0f);
	return 0;
}

int l_font_drawString(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	const char *text = luaL_checkstring(L, 2);
	float x = number(L, 3);
	float y = number(L, 4);
	float lineHeight = font->lineHeight > 0.0f ? font->lineHeight : font->size * 1.4f;
	std::string content(text);
	size_t start = 0;
	while(true) {
		size_t end = content.find('\n', start);
		std::string line = content.substr(start, end == std::string::npos ? std::string::npos : end - start);
		renderer()->drawText(line, x, y, font->size, fontFile(*font));
		if(end == std::string::npos) {
			break;
		}
		start = end + 1;
		y += lineHeight;
	}
	return 0;
}

int l_font_getStringWidth(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	lua_pushnumber(L, renderer()->measureText(luaL_checkstring(L, 2), font->size));
	return 1;
}

int l_font_size(lua_State *L) {
	lua_pushnumber(L, checkFont(L, 1)->size);
	return 1;
}

int l_font_setSize(lua_State *L) {
	checkFont(L, 1)->size = number(L, 2, 12.0f);
	return 0;
}

int l_font_setLineHeight(lua_State *L) {
	checkFont(L, 1)->lineHeight = number(L, 2, 0.0f);
	return 0;
}

int l_font_getLineHeight(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	lua_pushnumber(L, font->lineHeight > 0.0f ? font->lineHeight : font->size * 1.4f);
	return 1;
}

int l_font_getStringBoundingBox(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	const char *text = luaL_checkstring(L, 2);
	float x = number(L, 3);
	float y = number(L, 4);
	float width = renderer()->measureText(text, font->size, fontFile(*font));
	float height = font->size;
	lua_newtable(L);
	lua_pushnumber(L, x);
	lua_setfield(L, -2, "x");
	lua_pushnumber(L, y);
	lua_setfield(L, -2, "y");
	lua_pushnumber(L, width);
	lua_setfield(L, -2, "width");
	lua_pushnumber(L, height);
	lua_setfield(L, -2, "height");
	lua_pushnumber(L, x);
	lua_setfield(L, -2, "left");
	lua_pushnumber(L, y);
	lua_setfield(L, -2, "top");
	lua_pushnumber(L, x + width);
	lua_setfield(L, -2, "right");
	lua_pushnumber(L, y + height);
	lua_setfield(L, -2, "bottom");
	return 1;
}

//--------------------------------------------------------------
int l_font_index(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	if(std::string(key) == "size") {
		lua_pushnumber(L, font->size);
		return 1;
	}
	if(std::string(key) == "name") {
		lua_pushstring(L, font->name.c_str());
		return 1;
	}
	luaL_getmetatable(L, kFontMetatable);
	lua_getfield(L, -1, "__methods");
	lua_getfield(L, -1, key);
	lua_remove(L, -2);
	lua_remove(L, -2);
	return 1;
}

int l_font_newindex(lua_State *L) {
	LuaFont *font = checkFont(L, 1);
	const char *key = lua_tostring(L, 2);
	if(key == nullptr) {
		return 0;
	}
	if(std::string(key) == "size") {
		font->size = number(L, 3, font->size);
	}
	else if(std::string(key) == "name") {
		font->name = lua_tostring(L, 3) ? lua_tostring(L, 3) : "";
	}
	return 0;
}

//--------------------------------------------------------------
int l_background(lua_State *L) {
	int count = lua_gettop(L);
	Color color {0, 0, 0, 1};
	if(count >= 1) {
		color = colorFromArgs(L, 1, count, color);
	}
	env().background = color;
	renderer()->clear(color);
	return 0;
}

int l_clear(lua_State *L) {
	env().background = renderer()->color();
	renderer()->clear(env().background);
	return 0;
}

int l_setColor(lua_State *L) {
	int count = lua_gettop(L);
	renderer()->setColor(colorFromArgs(L, 1, count));
	return 0;
}

int l_setHexColor(lua_State *L) {
	uint32_t hex = (uint32_t)luaL_checkinteger(L, 1);
	float alpha = number(L, 2, 255.0f) / 255.0f;
	renderer()->setColor(hexColor(hex, alpha));
	return 0;
}

int l_fill(lua_State *L) {
	renderer()->setFill(true);
	return 0;
}

int l_noFill(lua_State *L) {
	renderer()->setFill(false);
	return 0;
}

int l_setLineWidth(lua_State *L) {
	renderer()->setLineWidth(number(L, 1, 1.0f));
	return 0;
}

int l_setCircleResolution(lua_State *L) {
	renderer()->setCircleResolution((int)number(L, 1, 24));
	return 0;
}

int l_enableAlphaBlending(lua_State *L) {
	renderer()->setAlphaBlending(true);
	return 0;
}

int l_disableAlphaBlending(lua_State *L) {
	renderer()->setAlphaBlending(false);
	return 0;
}

int l_enableSmoothing(lua_State *L) {
	renderer()->setSmoothing(true);
	return 0;
}

int l_disableSmoothing(lua_State *L) {
	renderer()->setSmoothing(false);
	return 0;
}

int l_drawRectangle(lua_State *L) {
	renderer()->drawRectangle(number(L, 1), number(L, 2), number(L, 3), number(L, 4));
	return 0;
}

int l_drawRectangleOutline(lua_State *L) {
	renderer()->drawRectangleOutline(number(L, 1), number(L, 2), number(L, 3), number(L, 4));
	return 0;
}

int l_drawCircle(lua_State *L) {
	renderer()->drawCircle(number(L, 1), number(L, 2), number(L, 3));
	return 0;
}

int l_drawEllipse(lua_State *L) {
	renderer()->drawEllipse(number(L, 1), number(L, 2), number(L, 3), number(L, 4));
	return 0;
}

int l_drawLine(lua_State *L) {
	renderer()->drawLine(number(L, 1), number(L, 2), number(L, 3), number(L, 4));
	return 0;
}

int l_drawTriangle(lua_State *L) {
	renderer()->drawTriangle(number(L, 1), number(L, 2), number(L, 3), number(L, 4),
	                         number(L, 5), number(L, 6));
	return 0;
}

int l_pushMatrix(lua_State *L) {
	renderer()->pushMatrix();
	return 0;
}

int l_popMatrix(lua_State *L) {
	renderer()->popMatrix();
	return 0;
}

int l_translate(lua_State *L) {
	renderer()->translate3D(number(L, 1), number(L, 2), number(L, 3, 0.0f));
	return 0;
}

int l_rotate(lua_State *L) {
	renderer()->rotate(number(L, 1));
	return 0;
}

int l_rotateDeg(lua_State *L) {
	renderer()->rotate(number(L, 1) * (float)M_PI / 180.0f);
	return 0;
}

int l_rotateXDeg(lua_State *L) {
	renderer()->rotateX(number(L, 1) * (float)M_PI / 180.0f);
	return 0;
}

int l_rotateYDeg(lua_State *L) {
	renderer()->rotateY(number(L, 1) * (float)M_PI / 180.0f);
	return 0;
}

int l_rotateZDeg(lua_State *L) {
	renderer()->rotateZ(number(L, 1) * (float)M_PI / 180.0f);
	return 0;
}

int l_scale(lua_State *L) {
	renderer()->scale(number(L, 1, 1.0f), number(L, 2, number(L, 1, 1.0f)));
	return 0;
}

int l_beginShape(lua_State *L) {
	renderer()->beginShape();
	return 0;
}

int l_vertex(lua_State *L) {
	renderer()->vertex(number(L, 1), number(L, 2));
	return 0;
}

int l_nextContour(lua_State *L) {
	renderer()->nextContour();
	return 0;
}

int l_endShape(lua_State *L) {
	bool close = true;
	if(lua_gettop(L) >= 1) {
		if(lua_isboolean(L, 1)) {
			close = lua_toboolean(L, 1) != 0;
		}
		else if(lua_isnumber(L, 1)) {
			close = lua_tonumber(L, 1) != 0;
		}
	}
	renderer()->endShape(close);
	return 0;
}

int l_drawBitmapString(lua_State *L) {
	const char *text = luaL_checkstring(L, 1);
	renderer()->drawText(text, number(L, 2), number(L, 3), number(L, 4, 13.0f));
	return 0;
}

int l_drawString(lua_State *L) {
	float size = 13.0f;
	float x = 0.0f;
	float y = 0.0f;
	std::string fontPath;
	const char *text = nullptr;
	if(lua_gettop(L) >= 4 && lua_isuserdata(L, 1)) {
		LuaFont *font = checkFont(L, 1);
		text = luaL_checkstring(L, 2);
		x = number(L, 3);
		y = number(L, 4);
		size = font->size;
		fontPath = fontFile(*font);
	}
	else {
		text = luaL_checkstring(L, 1);
		x = number(L, 2);
		y = number(L, 3);
		size = number(L, 4, 13.0f);
	}
	std::string content(text);
	size_t start = 0;
	while(true) {
		size_t end = content.find('\n', start);
		std::string line = content.substr(start, end == std::string::npos ? std::string::npos : end - start);
		renderer()->drawText(line, x, y, size, fontPath);
		if(end == std::string::npos) {
			break;
		}
		start = end + 1;
		y += size * 1.4f;
	}
	return 0;
}

int l_stringWidth(lua_State *L) {
	if(lua_isuserdata(L, 1)) {
		LuaFont *font = checkFont(L, 1);
		const char *text = luaL_checkstring(L, 2);
		lua_pushnumber(L, renderer()->measureText(text, font->size, fontFile(*font)));
	}
	else {
		const char *text = luaL_checkstring(L, 1);
		lua_pushnumber(L, renderer()->measureText(text, number(L, 2, 13.0f)));
	}
	return 1;
}

//--------------------------------------------------------------
int l_loadImage(lua_State *L) {
	const char *path = luaL_checkstring(L, 1);
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_set_flip_vertically_on_load(0);
	unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
	if(pixels == nullptr) {
		lua_pushnil(L);
		return 1;
	}
	LuaImage *image = static_cast<LuaImage *>(lua_newuserdatauv(L, sizeof(LuaImage), 0));
	new(image) LuaImage();
	image->width = width;
	image->height = height;
	image->texture = renderer()->createTexture(pixels, width, height);
	stbi_image_free(pixels);
	luaL_getmetatable(L, kImageMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int l_drawImage(lua_State *L) {
	LuaImage *image = checkImage(L, 1);
	float x = number(L, 2);
	float y = number(L, 3);
	float w = number(L, 4, (float)image->width);
	float h = number(L, 5, (float)image->height);
	if(image->texture != 0) {
		renderer()->drawTexture(image->texture, x, y, w, h);
	}
	return 0;
}

int l_loadFont(lua_State *L) {
	const char *path = luaL_optstring(L, 1, "");
	float size = number(L, 2, 12.0f);
	LuaFont *font = static_cast<LuaFont *>(lua_newuserdatauv(L, sizeof(LuaFont), 0));
	new(font) LuaFont();
	font->size = size;
	font->name = path;
	luaL_getmetatable(L, kFontMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int l_newFont(lua_State *L) {
	LuaFont *font = static_cast<LuaFont *>(lua_newuserdatauv(L, sizeof(LuaFont), 0));
	new(font) LuaFont();
	font->size = number(L, 1, 12.0f);
	luaL_getmetatable(L, kFontMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int l_point(lua_State *L) {
	lua_newtable(L);
	lua_pushnumber(L, number(L, 1));
	lua_setfield(L, -2, "x");
	lua_pushnumber(L, number(L, 2));
	lua_setfield(L, -2, "y");
	return 1;
}

//--------------------------------------------------------------
int l_getWidth(lua_State *L) {
	lua_pushnumber(L, renderer()->width());
	return 1;
}

int l_getHeight(lua_State *L) {
	lua_pushnumber(L, renderer()->height());
	return 1;
}

int l_getElapsedTimef(lua_State *L) {
	lua_pushnumber(L, env().time);
	return 1;
}

int l_getElapsedTimeMillis(lua_State *L) {
	lua_pushnumber(L, env().time * 1000.0);
	return 1;
}

int l_getFrameNum(lua_State *L) {
	lua_pushinteger(L, env().frameNum);
	return 1;
}

int l_getLastFrameTime(lua_State *L) {
	lua_pushnumber(L, env().deltaTime);
	return 1;
}

int l_getFrameRate(lua_State *L) {
	lua_pushnumber(L, env().targetFrameRate);
	return 1;
}

int l_setFrameRate(lua_State *L) {
	int rate = (int)number(L, 1, 60.0f);
	env().targetFrameRate = std::max(1, std::min(240, rate));
	return 0;
}

int l_getMouseX(lua_State *L) {
	lua_pushnumber(L, env().input.mouseX);
	return 1;
}

int l_getMouseY(lua_State *L) {
	lua_pushnumber(L, env().input.mouseY);
	return 1;
}

int l_setWindowTitle(lua_State *L) {
	const char *title = luaL_checkstring(L, 1);
	env().windowTitle = title;
	if(env().emit) {
		env().emit("{\"type\":\"info\",\"message\":\"" + std::string(title) + "\"}");
	}
	return 0;
}

int l_noop(lua_State *L) {
	return 0;
}

int l_sendMessage(lua_State *L) {
	size_t length = 0;
	const char *text = luaL_checklstring(L, 1, &length);
	env().pendingMessages.emplace_back(text, length);
	return 0;
}

int l_drawBitmapStringHighlight(lua_State *L) {
	const char *text = luaL_checkstring(L, 1);
	float x = number(L, 2);
	float y = number(L, 3);
	Color background = colorFromArgs(L, 4, 3, Color{0.0f, 0.0f, 0.0f, 1.0f});
	Color foreground = colorFromArgs(L, 5, 3, Color{1.0f, 1.0f, 1.0f, 1.0f});
	float width = renderer()->measureText(text, 14.0f);
	Color previous = renderer()->color();
	renderer()->setColor(background);
	renderer()->drawRectangle(x - 4.0f, y - 14.0f, width + 8.0f, 20.0f);
	renderer()->setColor(foreground);
	renderer()->drawText(text, x, y, 14.0f);
	renderer()->setColor(previous);
	return 0;
}

int l_drawPolyline(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int count = (int)lua_rawlen(L, 1);
	if(count < 2) {
		return 0;
	}
	float firstX = 0.0f, firstY = 0.0f;
	float previousX = 0.0f, previousY = 0.0f;
	for(int i = 1; i <= count; ++i) {
		lua_rawgeti(L, 1, i);
		float x = 0.0f, y = 0.0f;
		if(lua_istable(L, -1)) {
			lua_getfield(L, -1, "x");
			x = number(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "y");
			y = number(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		if(i == 1) {
			firstX = x;
			firstY = y;
		}
		else {
			renderer()->drawLine(previousX, previousY, x, y);
		}
		previousX = x;
		previousY = y;
	}
	bool close = lua_gettop(L) >= 2 && lua_toboolean(L, 2) != 0;
	if(close) {
		renderer()->drawLine(previousX, previousY, firstX, firstY);
	}
	return 0;
}

int l_drawPolygon(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int count = (int)lua_rawlen(L, 1);
	if(count < 3) {
		return 0;
	}
	renderer()->beginShape();
	for(int i = 1; i <= count; ++i) {
		lua_rawgeti(L, 1, i);
		if(lua_istable(L, -1)) {
			lua_getfield(L, -1, "x");
			float x = number(L, -1);
			lua_pop(L, 1);
			lua_getfield(L, -1, "y");
			float y = number(L, -1);
			lua_pop(L, 1);
			renderer()->vertex(x, y);
		}
		lua_pop(L, 1);
	}
	renderer()->endShape(true);
	return 0;
}

int l_pushStyle(lua_State *L) {
	return 0;
}

int l_popStyle(lua_State *L) {
	return 0;
}

int l_pushView(lua_State *L) {
	renderer()->pushMatrix();
	return 0;
}

int l_popView(lua_State *L) {
	renderer()->popMatrix();
	return 0;
}

int l_getMousePressed(lua_State *L) {
	lua_pushboolean(L, env().input.mouseButton[0]);
	return 1;
}

int l_getKeyPressed(lua_State *L) {
	int key = (int)number(L, 1, 0.0f);
	lua_pushboolean(L, env().input.lastKey == key);
	return 1;
}

int l_clamp(lua_State *L) {
	float value = number(L, 1);
	float low = number(L, 2);
	float high = number(L, 3);
	lua_pushnumber(L, value < low ? low : (value > high ? high : value));
	return 1;
}

int l_lerp(lua_State *L) {
	float start = number(L, 1);
	float stop = number(L, 2);
	float amount = number(L, 3);
	lua_pushnumber(L, start + (stop - start) * amount);
	return 1;
}

int l_dist(lua_State *L) {
	float dx = number(L, 1) - number(L, 3);
	float dy = number(L, 2) - number(L, 4);
	lua_pushnumber(L, std::sqrt(dx * dx + dy * dy));
	return 1;
}

int l_distSquared(lua_State *L) {
	float dx = number(L, 1) - number(L, 3);
	float dy = number(L, 2) - number(L, 4);
	lua_pushnumber(L, dx * dx + dy * dy);
	return 1;
}

int l_toString(lua_State *L) {
	size_t length = 0;
	const char *text = luaL_tolstring(L, 1, &length);
	lua_pushstring(L, text != nullptr ? text : "");
	return 1;
}

int l_toInt(lua_State *L) {
	const char *text = luaL_checkstring(L, 1);
	lua_pushinteger(L, (lua_Integer)std::strtol(text, nullptr, 10));
	return 1;
}

int l_toFloat(lua_State *L) {
	const char *text = luaL_checkstring(L, 1);
	lua_pushnumber(L, std::strtod(text, nullptr));
	return 1;
}

int l_degToRad(lua_State *L) {
	lua_pushnumber(L, number(L, 1) * M_PI / 180.0);
	return 1;
}

int l_radToDeg(lua_State *L) {
	lua_pushnumber(L, number(L, 1) * 180.0 / M_PI);
	return 1;
}

//--------------------------------------------------------------
float randomFloat() {
	static std::mt19937 generator(std::random_device{}());
	static std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
	return distribution(generator);
}

int l_random(lua_State *L) {
	int count = lua_gettop(L);
	if(count == 0) {
		lua_pushnumber(L, randomFloat());
	}
	else if(count == 1) {
		lua_pushnumber(L, randomFloat() * number(L, 1, 1.0f));
	}
	else {
		float low = number(L, 1);
		float high = number(L, 2);
		lua_pushnumber(L, low + randomFloat() * (high - low));
	}
	return 1;
}

int l_randomSeed(lua_State *L) {
	return 0;
}

float hash2(int x, int y) {
	uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h = h ^ (h >> 16);
	return (float)(h & 0xffffff) / (float)0xffffff;
}

float smoothNoise(float x, float y) {
	int xi = (int)std::floor(x);
	int yi = (int)std::floor(y);
	float xf = x - xi;
	float yf = y - yi;
	float u = xf * xf * (3.0f - 2.0f * xf);
	float v = yf * yf * (3.0f - 2.0f * yf);
	float a = hash2(xi, yi);
	float b = hash2(xi + 1, yi);
	float c = hash2(xi, yi + 1);
	float d = hash2(xi + 1, yi + 1);
	return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

int l_noise(lua_State *L) {
	float x = number(L, 1);
	float y = number(L, 2);
	lua_pushnumber(L, smoothNoise(x, y));
	return 1;
}

int l_signedNoise(lua_State *L) {
	float x = number(L, 1);
	float y = number(L, 2);
	lua_pushnumber(L, smoothNoise(x, y) * 2.0f - 1.0f);
	return 1;
}

int l_map(lua_State *L) {
	float value = number(L, 1);
	float inMin = number(L, 2);
	float inMax = number(L, 3);
	float outMin = number(L, 4);
	float outMax = number(L, 5);
	float normalized = (value - inMin) / (inMax - inMin);
	lua_pushnumber(L, outMin + normalized * (outMax - outMin));
	return 1;
}

//--------------------------------------------------------------
void registerImageMetatable(lua_State *L) {
	luaL_newmetatable(L, kImageMetatable);
	lua_pushcfunction(L, l_image_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, l_image_getWidth);
	lua_setfield(L, -2, "getWidth");
	lua_pushcfunction(L, l_image_getHeight);
	lua_setfield(L, -2, "getHeight");
	lua_pushcfunction(L, l_image_isValid);
	lua_setfield(L, -2, "isValid");
	lua_pushcfunction(L, l_image_draw);
	lua_setfield(L, -2, "draw");
	lua_pushcfunction(L, l_image_load);
	lua_setfield(L, -2, "load");
	lua_pushcfunction(L, l_image_isLoaded);
	lua_setfield(L, -2, "isLoaded");
	lua_pushcfunction(L, l_image_setImageType);
	lua_setfield(L, -2, "setImageType");
	lua_pushcfunction(L, l_image_clear);
	lua_setfield(L, -2, "clear");
	lua_pushcfunction(L, l_image_resize);
	lua_setfield(L, -2, "resize");
	lua_pushcfunction(L, l_image_getColor);
	lua_setfield(L, -2, "getColor");
	lua_pushcfunction(L, l_image_allocate);
	lua_setfield(L, -2, "allocate");
	lua_pushcfunction(L, l_image_setFromPixels);
	lua_setfield(L, -2, "setFromPixels");
	lua_pushcfunction(L, l_image_getPixels);
	lua_setfield(L, -2, "getPixels");
	lua_pushcfunction(L, l_image_update);
	lua_setfield(L, -2, "update");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerFontMetatable(lua_State *L) {
	luaL_newmetatable(L, kFontMetatable);
	lua_pushcfunction(L, l_font_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, l_font_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_newtable(L);
	lua_pushcfunction(L, l_font_load);
	lua_setfield(L, -2, "load");
	lua_pushcfunction(L, l_font_drawString);
	lua_setfield(L, -2, "drawString");
	lua_pushcfunction(L, l_font_getStringWidth);
	lua_setfield(L, -2, "getStringWidth");
	lua_pushcfunction(L, l_font_size);
	lua_setfield(L, -2, "getSize");
	lua_pushcfunction(L, l_font_setSize);
	lua_setfield(L, -2, "setSize");
	lua_pushcfunction(L, l_font_setLineHeight);
	lua_setfield(L, -2, "setLineHeight");
	lua_pushcfunction(L, l_font_getLineHeight);
	lua_setfield(L, -2, "getLineHeight");
	lua_pushcfunction(L, l_font_getStringBoundingBox);
	lua_setfield(L, -2, "getStringBoundingBox");
	lua_setfield(L, -2, "__methods");
	lua_pop(L, 1);
}

void registerColorMetatable(lua_State *L) {
	luaL_newmetatable(L, "loaf.Color");
	lua_newtable(L);
	lua_pushcfunction(L, l_color_getBrightness);
	lua_setfield(L, -2, "getBrightness");
	lua_pushcfunction(L, l_color_getLightness);
	lua_setfield(L, -2, "getLightness");
	lua_pushcfunction(L, l_color_getHue);
	lua_setfield(L, -2, "getHue");
	lua_pushcfunction(L, l_color_getSaturation);
	lua_setfield(L, -2, "getSaturation");
	lua_pushcfunction(L, l_color_getHex);
	lua_setfield(L, -2, "getHex");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void registerFboMetatable(lua_State *L) {
	luaL_newmetatable(L, kFboMetatable);
	lua_pushcfunction(L, l_fbo_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, l_fbo_allocate);
	lua_setfield(L, -2, "allocate");
	lua_pushcfunction(L, l_fbo_begin);
	lua_setfield(L, -2, "begin");
	lua_pushcfunction(L, l_fbo_end);
	lua_setfield(L, -2, "end");
	lua_pushcfunction(L, l_fbo_end);
	lua_setfield(L, -2, "endFbo");
	lua_pushcfunction(L, l_fbo_end);
	lua_setfield(L, -2, "finish");
	lua_pushcfunction(L, l_fbo_end);
	lua_setfield(L, -2, "end_");
	lua_pushcfunction(L, l_fbo_draw);
	lua_setfield(L, -2, "draw");
	lua_pushcfunction(L, l_fbo_clear);
	lua_setfield(L, -2, "clear");
	lua_pushcfunction(L, l_fbo_getWidth);
	lua_setfield(L, -2, "getWidth");
	lua_pushcfunction(L, l_fbo_getHeight);
	lua_setfield(L, -2, "getHeight");
	lua_pushcfunction(L, l_fbo_isAllocated);
	lua_setfield(L, -2, "isAllocated");
	lua_pushcfunction(L, l_fbo_getTexture);
	lua_setfield(L, -2, "getTexture");
	lua_pushcfunction(L, l_fbo_getPixel);
	lua_setfield(L, -2, "getPixel");
	lua_pushcfunction(L, l_fbo_getPixel);
	lua_setfield(L, -2, "readToPixels");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
}

void setFunction(lua_State *L, const char *name, lua_CFunction function, int table = -1) {
	lua_pushcfunction(L, function);
	lua_setfield(L, table, name);
}

void setNumber(lua_State *L, const char *name, double value, int table = -1) {
	lua_pushnumber(L, value);
	lua_setfield(L, table, name);
}

} // namespace

//--------------------------------------------------------------
void registerOfBindings(lua_State *L) {
	registerImageMetatable(L);
	registerFontMetatable(L);
	registerColorMetatable(L);
	registerFboMetatable(L);

	lua_newtable(L);
	int table = lua_gettop(L);

	setFunction(L, "background", l_background, table);
	setFunction(L, "clear", l_clear, table);
	setFunction(L, "setColor", l_setColor, table);
	setFunction(L, "setHexColor", l_setHexColor, table);
	setFunction(L, "fill", l_fill, table);
	setFunction(L, "noFill", l_noFill, table);
	setFunction(L, "setLineWidth", l_setLineWidth, table);
	setFunction(L, "setCircleResolution", l_setCircleResolution, table);
	setFunction(L, "setCurveResolution", l_noop, table);
	setFunction(L, "setPolyMode", l_noop, table);
	setFunction(L, "setRectMode", l_noop, table);
	setFunction(L, "enableAlphaBlending", l_enableAlphaBlending, table);
	setFunction(L, "disableAlphaBlending", l_disableAlphaBlending, table);
	setFunction(L, "enableSmoothing", l_enableSmoothing, table);
	setFunction(L, "disableSmoothing", l_disableSmoothing, table);
	setFunction(L, "drawRectangle", l_drawRectangle, table);
	setFunction(L, "drawRect", l_drawRectangle, table);
	setFunction(L, "drawRectangleOutline", l_drawRectangleOutline, table);
	setFunction(L, "drawCircle", l_drawCircle, table);
	setFunction(L, "drawEllipse", l_drawEllipse, table);
	setFunction(L, "drawLine", l_drawLine, table);
	setFunction(L, "drawTriangle", l_drawTriangle, table);
	setFunction(L, "pushMatrix", l_pushMatrix, table);
	setFunction(L, "popMatrix", l_popMatrix, table);
	setFunction(L, "translate", l_translate, table);
	setFunction(L, "rotate", l_rotate, table);
	setFunction(L, "rotateDeg", l_rotateDeg, table);
	setFunction(L, "rotateXDeg", l_rotateXDeg, table);
	setFunction(L, "rotateYDeg", l_rotateYDeg, table);
	setFunction(L, "rotateZDeg", l_rotateZDeg, table);
	setFunction(L, "scale", l_scale, table);
	setFunction(L, "beginShape", l_beginShape, table);
	setFunction(L, "vertex", l_vertex, table);
	setFunction(L, "curveVertex", l_vertex, table);
	setFunction(L, "bezierVertex", l_vertex, table);
	setFunction(L, "nextContour", l_nextContour, table);
	setFunction(L, "endShape", l_endShape, table);
	setFunction(L, "drawBitmapString", l_drawBitmapString, table);
	setFunction(L, "drawBitmapStringHighlight", l_drawBitmapStringHighlight, table);
	setFunction(L, "drawString", l_drawString, table);
	setFunction(L, "stringWidth", l_stringWidth, table);
	setFunction(L, "loadImage", l_loadImage, table);
	setFunction(L, "drawImage", l_drawImage, table);
	setFunction(L, "loadFont", l_loadFont, table);
	setFunction(L, "TrueTypeFont", l_newFont, table);
	setFunction(L, "Image", l_image_new, table);
	setFunction(L, "Fbo", l_fbo_new, table);
	setFunction(L, "FBO", l_fbo_new, table);
	setFunction(L, "Point", l_point, table);
	setFunction(L, "getWidth", l_getWidth, table);
	setFunction(L, "getHeight", l_getHeight, table);
	setFunction(L, "getWindowWidth", l_getWidth, table);
	setFunction(L, "getWindowHeight", l_getHeight, table);
	setFunction(L, "getScreenWidth", l_getWidth, table);
	setFunction(L, "getScreenHeight", l_getHeight, table);
	setFunction(L, "getElapsedTimef", l_getElapsedTimef, table);
	setFunction(L, "getElapsedTime", l_getElapsedTimeMillis, table);
	setFunction(L, "getElapsedTimeMillis", l_getElapsedTimeMillis, table);
	setFunction(L, "getFrameNum", l_getFrameNum, table);
	setFunction(L, "getLastFrameTime", l_getLastFrameTime, table);
	setFunction(L, "getFrameRate", l_getFrameRate, table);
	setFunction(L, "setFrameRate", l_setFrameRate, table);
	setFunction(L, "getMouseX", l_getMouseX, table);
	setFunction(L, "getMouseY", l_getMouseY, table);
	setFunction(L, "setWindowTitle", l_setWindowTitle, table);
	setFunction(L, "setWindowShape", l_noop, table);
	setFunction(L, "setWindowPosition", l_noop, table);
	setFunction(L, "setFullscreen", l_noop, table);
	setFunction(L, "toggleFullscreen", l_noop, table);
	setFunction(L, "setBackgroundAuto", l_noop, table);
	setFunction(L, "setVerticalSync", l_noop, table);
	setFunction(L, "random", l_random, table);
	setFunction(L, "randomSeed", l_randomSeed, table);
	setFunction(L, "noise", l_noise, table);
	setFunction(L, "signedNoise", l_signedNoise, table);
	setFunction(L, "map", l_map, table);
	setFunction(L, "drawPolyline", l_drawPolyline, table);
	setFunction(L, "drawPolygon", l_drawPolygon, table);
	setFunction(L, "drawRectRounded", l_drawRectangle, table);
	setFunction(L, "drawArc", l_drawEllipse, table);
	setFunction(L, "pushStyle", l_pushStyle, table);
	setFunction(L, "popStyle", l_popStyle, table);
	setFunction(L, "pushView", l_pushView, table);
	setFunction(L, "popView", l_popView, table);
	setFunction(L, "getMousePressed", l_getMousePressed, table);
	setFunction(L, "getKeyPressed", l_getKeyPressed, table);
	setFunction(L, "clamp", l_clamp, table);
	setFunction(L, "lerp", l_lerp, table);
	setFunction(L, "dist", l_dist, table);
	setFunction(L, "distSquared", l_distSquared, table);
	setFunction(L, "toString", l_toString, table);
	setFunction(L, "toInt", l_toInt, table);
	setFunction(L, "toFloat", l_toFloat, table);
	setFunction(L, "degToRad", l_degToRad, table);
	setFunction(L, "radToDeg", l_radToDeg, table);
	setFunction(L, "backgroundAuto", l_noop, table);
	setFunction(L, "setEscapeQuitsApp", l_noop, table);
	setFunction(L, "setWindowMode", l_noop, table);
	setFunction(L, "getWindowMode", l_noop, table);
	setFunction(L, "sendMessage", l_sendMessage, table);

	setNumber(L, "PI", M_PI, table);
	setNumber(L, "TWO_PI", M_PI * 2.0, table);
	setNumber(L, "HALF_PI", M_PI * 0.5, table);
	setNumber(L, "DEG_TO_RAD", M_PI / 180.0, table);
	setNumber(L, "RAD_TO_DEG", 180.0 / M_PI, table);
	setNumber(L, "POLY_WINDING_ODD", 1, table);
	setNumber(L, "POLY_WINDING_NONZERO", 2, table);
	setNumber(L, "POLY_WINDING_ABS_GEQ_TWO", 3, table);
	setNumber(L, "IMAGE_GRAYSCALE", 1, table);
	lua_pushboolean(L, 1);
	lua_setfield(L, table, "CLOSE");

	registerFileBindings(L, table);
	registerShaderBindings(L, table);
	registerAudioBindings(L, table);
	registerVideoBindings(L, table);
	register3DBindings(L, table);
	registerGfx2Bindings(L, table);
	registerMiscBindings(L, table);
	registerAudioStreamBindings(L, table);
	registerGfx3Bindings(L, table);
	lua_setglobal(L, "of");
}

} // namespace loaf
