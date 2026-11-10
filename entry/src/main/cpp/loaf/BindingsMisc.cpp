#include "BindingsMisc.h"

#include "Environment.h"
#include "Math3D.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <map>

namespace loaf {

namespace {

const char *kRectMeta = "loaf.Rectangle";
const char *kFpsMeta = "loaf.FpsCounter";
const char *kParamMeta = "loaf.Parameter";
const char *kNodeMeta = "loaf.Node";
const char *kMaterialMeta = "loaf.Material";
const char *kLightMeta = "loaf.Light";
const char *kPrimitiveMeta = "loaf.Primitive";

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

void setFunction(lua_State *L, const char *name, lua_CFunction function) {
	lua_pushcfunction(L, function);
	lua_setfield(L, -2, name);
}

Color readColor(lua_State *L, int index, Color fallback = Color{1, 1, 1, 1}) {
	if(lua_istable(L, index)) {
		Color color;
		lua_getfield(L, index, "r"); color.r = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, index, "g"); color.g = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, index, "b"); color.b = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		lua_getfield(L, index, "a"); color.a = number(L, -1, 255.0f) / 255.0f; lua_pop(L, 1);
		return color;
	}
	return fallback;
}

void pushColor(lua_State *L, const Color &color) {
	lua_newtable(L);
	lua_pushnumber(L, color.r * 255.0f); lua_setfield(L, -2, "r");
	lua_pushnumber(L, color.g * 255.0f); lua_setfield(L, -2, "g");
	lua_pushnumber(L, color.b * 255.0f); lua_setfield(L, -2, "b");
	lua_pushnumber(L, color.a * 255.0f); lua_setfield(L, -2, "a");
	luaL_getmetatable(L, "loaf.Color");
	lua_setmetatable(L, -2);
}

//--------------------------------------------------------------
struct LuaRect { float x, y, w, h; };
struct LuaFps { double last; double fps; int frames; };
struct LuaParam { float value; float minimum; float maximum; std::string name; };
struct LuaNode {
	float x = 0, y = 0, z = 0;
	float rx = 0, ry = 0, rz = 0;
	float sx = 1, sy = 1, sz = 1;
};
struct LuaMaterial { Color ambient{0.2f,0.2f,0.2f,1}, diffuse{0.8f,0.8f,0.8f,1}, specular{0.1f,0.1f,0.1f,1}; };
struct LuaLight { bool enabled = true; float x = 0, y = 500, z = 500; Color diffuse{1,1,1,1}; };
struct LuaPrimitive { int type = 0; float a = 100, b = 100, c = 100; };

LuaRect *checkRect(lua_State *L, int i) { return static_cast<LuaRect *>(luaL_checkudata(L, i, kRectMeta)); }
LuaFps *checkFps(lua_State *L, int i) { return static_cast<LuaFps *>(luaL_checkudata(L, i, kFpsMeta)); }
LuaParam *checkParam(lua_State *L, int i) { return static_cast<LuaParam *>(luaL_checkudata(L, i, kParamMeta)); }
LuaNode *checkNode(lua_State *L, int i) { return static_cast<LuaNode *>(luaL_checkudata(L, i, kNodeMeta)); }
LuaMaterial *checkMaterial(lua_State *L, int i) { return static_cast<LuaMaterial *>(luaL_checkudata(L, i, kMaterialMeta)); }
LuaLight *checkLight(lua_State *L, int i) { return static_cast<LuaLight *>(luaL_checkudata(L, i, kLightMeta)); }
LuaPrimitive *checkPrimitive(lua_State *L, int i) { return static_cast<LuaPrimitive *>(luaL_checkudata(L, i, kPrimitiveMeta)); }

//--------------------------------------------------------------
int rect_new(lua_State *L) {
	LuaRect *r = static_cast<LuaRect *>(lua_newuserdatauv(L, sizeof(LuaRect), 0));
	new(r) LuaRect{number(L, 1), number(L, 2), number(L, 3), number(L, 4)};
	luaL_getmetatable(L, kRectMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int rect_gc(lua_State *L) { checkRect(L, 1)->~LuaRect(); return 0; }
int rect_set(lua_State *L) { LuaRect *r = checkRect(L, 1); r->x = number(L, 2); r->y = number(L, 3); r->w = number(L, 4); r->h = number(L, 5); return 0; }
int rect_getX(lua_State *L) { lua_pushnumber(L, checkRect(L, 1)->x); return 1; }
int rect_getY(lua_State *L) { lua_pushnumber(L, checkRect(L, 1)->y); return 1; }
int rect_getWidth(lua_State *L) { lua_pushnumber(L, checkRect(L, 1)->w); return 1; }
int rect_getHeight(lua_State *L) { lua_pushnumber(L, checkRect(L, 1)->h); return 1; }
int rect_getArea(lua_State *L) { LuaRect *r = checkRect(L, 1); lua_pushnumber(L, r->w * r->h); return 1; }
int rect_getCenter(lua_State *L) { LuaRect *r = checkRect(L, 1); lua_newtable(L); lua_pushnumber(L, r->x + r->w * 0.5f); lua_setfield(L, -2, "x"); lua_pushnumber(L, r->y + r->h * 0.5f); lua_setfield(L, -2, "y"); return 1; }
int rect_inside(lua_State *L) {
	LuaRect *r = checkRect(L, 1);
	float x = number(L, 2), y = number(L, 3);
	lua_pushboolean(L, x >= r->x && y >= r->y && x <= r->x + r->w && y <= r->y + r->h);
	return 1;
}
int rect_intersects(lua_State *L) {
	LuaRect *a = checkRect(L, 1);
	LuaRect *b = checkRect(L, 2);
	bool hit = a->x < b->x + b->w && a->x + a->w > b->x && a->y < b->y + b->h && a->y + a->h > b->y;
	lua_pushboolean(L, hit);
	return 1;
}

//--------------------------------------------------------------
int fps_new(lua_State *L) {
	LuaFps *f = static_cast<LuaFps *>(lua_newuserdatauv(L, sizeof(LuaFps), 0));
	new(f) LuaFps();
	luaL_getmetatable(L, kFpsMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int fps_gc(lua_State *L) { checkFps(L, 1)->~LuaFps(); return 0; }
int fps_update(lua_State *L) {
	LuaFps *f = checkFps(L, 1);
	double now = env().time;
	f->frames++;
	if(now - f->last >= 1.0) {
		f->fps = f->frames / (now - f->last);
		f->frames = 0;
		f->last = now;
	}
	return 0;
}
int fps_getFps(lua_State *L) { lua_pushnumber(L, checkFps(L, 1)->fps); return 1; }
int fps_getFrameRate(lua_State *L) { return fps_getFps(L); }

//--------------------------------------------------------------
int param_new(lua_State *L) {
	LuaParam *p = static_cast<LuaParam *>(lua_newuserdatauv(L, sizeof(LuaParam), 0));
	new(p) LuaParam{number(L, 1), 0.0f, 1.0f, ""};
	luaL_getmetatable(L, kParamMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int param_gc(lua_State *L) { checkParam(L, 1)->~LuaParam(); return 0; }
int param_set(lua_State *L) {
	LuaParam *p = checkParam(L, 1);
	float value = number(L, 2);
	p->value = std::max(p->minimum, std::min(p->maximum, value));
	return 0;
}
int param_get(lua_State *L) { lua_pushnumber(L, checkParam(L, 1)->value); return 1; }
int param_getName(lua_State *L) { lua_pushstring(L, checkParam(L, 1)->name.c_str()); return 1; }
int param_setName(lua_State *L) { checkParam(L, 1)->name = luaL_checkstring(L, 2); return 0; }
int param_setMin(lua_State *L) { LuaParam *p = checkParam(L, 1); p->minimum = number(L, 2); return 0; }
int param_setMax(lua_State *L) { LuaParam *p = checkParam(L, 1); p->maximum = number(L, 2); return 0; }
int param_getMin(lua_State *L) { lua_pushnumber(L, checkParam(L, 1)->minimum); return 1; }
int param_getMax(lua_State *L) { lua_pushnumber(L, checkParam(L, 1)->maximum); return 1; }

//--------------------------------------------------------------
int node_new(lua_State *L) {
	LuaNode *n = static_cast<LuaNode *>(lua_newuserdatauv(L, sizeof(LuaNode), 0));
	new(n) LuaNode();
	luaL_getmetatable(L, kNodeMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int node_gc(lua_State *L) { checkNode(L, 1)->~LuaNode(); return 0; }
int node_setPosition(lua_State *L) { LuaNode *n = checkNode(L, 1); n->x = number(L, 2); n->y = number(L, 3); n->z = number(L, 4); return 0; }
int node_move(lua_State *L) { LuaNode *n = checkNode(L, 1); n->x += number(L, 2); n->y += number(L, 3); n->z += number(L, 4); return 0; }
int node_getPosition(lua_State *L) {
	LuaNode *n = checkNode(L, 1);
	lua_newtable(L);
	lua_pushnumber(L, n->x); lua_setfield(L, -2, "x");
	lua_pushnumber(L, n->y); lua_setfield(L, -2, "y");
	lua_pushnumber(L, n->z); lua_setfield(L, -2, "z");
	return 1;
}
int node_getX(lua_State *L) { lua_pushnumber(L, checkNode(L, 1)->x); return 1; }
int node_getY(lua_State *L) { lua_pushnumber(L, checkNode(L, 1)->y); return 1; }
int node_getZ(lua_State *L) { lua_pushnumber(L, checkNode(L, 1)->z); return 1; }
int node_setScale(lua_State *L) { LuaNode *n = checkNode(L, 1); n->sx = number(L, 2, 1); n->sy = number(L, 3, n->sx); n->sz = number(L, 4, n->sx); return 0; }
int node_rotateDeg(lua_State *L) { LuaNode *n = checkNode(L, 1); n->rz += number(L, 2); return 0; }
int node_rotateX(lua_State *L) { checkNode(L, 1)->rx += number(L, 2); return 0; }
int node_rotateY(lua_State *L) { checkNode(L, 1)->ry += number(L, 2); return 0; }
int node_reset(lua_State *L) { *checkNode(L, 1) = LuaNode(); return 0; }
int node_transform(lua_State *L) {
	LuaNode *n = checkNode(L, 1);
	if(env().renderer == nullptr) return 0;
	env().renderer->pushMatrix();
	env().renderer->translate3D(n->x, n->y, n->z);
	if(n->rx) env().renderer->rotateX(n->rx * (float)M_PI / 180.0f);
	if(n->ry) env().renderer->rotateY(n->ry * (float)M_PI / 180.0f);
	if(n->rz) env().renderer->rotateZ(n->rz * (float)M_PI / 180.0f);
	if(n->sx != 1 || n->sy != 1 || n->sz != 1) {
		env().renderer->scale(n->sx, n->sy);
	}
	return 0;
}
int node_restore(lua_State *L) { if(env().renderer) env().renderer->popMatrix(); return 0; }

//--------------------------------------------------------------
int material_new(lua_State *L) {
	LuaMaterial *m = static_cast<LuaMaterial *>(lua_newuserdatauv(L, sizeof(LuaMaterial), 0));
	new(m) LuaMaterial();
	luaL_getmetatable(L, kMaterialMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int material_gc(lua_State *L) { checkMaterial(L, 1)->~LuaMaterial(); return 0; }
int material_setDiffuse(lua_State *L) { checkMaterial(L, 1)->diffuse = readColor(L, 2); return 0; }
int material_setAmbient(lua_State *L) { checkMaterial(L, 1)->ambient = readColor(L, 2); return 0; }
int material_setSpecular(lua_State *L) { checkMaterial(L, 1)->specular = readColor(L, 2); return 0; }
int material_begin(lua_State *L) {
	LuaMaterial *m = checkMaterial(L, 1);
	if(env().renderer) env().renderer->setColor(m->diffuse);
	return 0;
}
int material_end(lua_State *L) { return 0; }

//--------------------------------------------------------------
int light_new(lua_State *L) {
	LuaLight *l = static_cast<LuaLight *>(lua_newuserdatauv(L, sizeof(LuaLight), 0));
	new(l) LuaLight();
	luaL_getmetatable(L, kLightMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int light_gc(lua_State *L) { checkLight(L, 1)->~LuaLight(); return 0; }
int light_enable(lua_State *L) { checkLight(L, 1)->enabled = true; return 0; }
int light_disable(lua_State *L) { checkLight(L, 1)->enabled = false; return 0; }
int light_isEnabled(lua_State *L) { lua_pushboolean(L, checkLight(L, 1)->enabled); return 1; }
int light_setPosition(lua_State *L) { LuaLight *l = checkLight(L, 1); l->x = number(L, 2); l->y = number(L, 3); l->z = number(L, 4); return 0; }
int light_setDiffuse(lua_State *L) { checkLight(L, 1)->diffuse = readColor(L, 2); return 0; }

//--------------------------------------------------------------
int primitive_new(lua_State *L, int type) {
	LuaPrimitive *p = static_cast<LuaPrimitive *>(lua_newuserdatauv(L, sizeof(LuaPrimitive), 0));
	new(p) LuaPrimitive();
	p->type = type;
	luaL_getmetatable(L, kPrimitiveMeta);
	lua_setmetatable(L, -2);
	return 1;
}
int box_new(lua_State *L) { int r = primitive_new(L, 0); checkPrimitive(L, -1)->a = number(L, 1, 100.0f); return r; }
int sphere_new(lua_State *L) { int r = primitive_new(L, 1); checkPrimitive(L, -1)->a = number(L, 1, 50.0f); return r; }
int cone_new(lua_State *L) { int r = primitive_new(L, 2); checkPrimitive(L, -1)->a = number(L, 1, 50.0f); checkPrimitive(L, -1)->b = number(L, 2, 100.0f); return r; }
int cylinder_new(lua_State *L) { int r = primitive_new(L, 3); checkPrimitive(L, -1)->a = number(L, 1, 50.0f); checkPrimitive(L, -1)->b = number(L, 2, 100.0f); return r; }
int plane_new(lua_State *L) { int r = primitive_new(L, 4); checkPrimitive(L, -1)->a = number(L, 1, 100.0f); checkPrimitive(L, -1)->b = number(L, 2, 100.0f); return r; }
int ico_new(lua_State *L) { int r = primitive_new(L, 5); checkPrimitive(L, -1)->a = number(L, 1, 50.0f); checkPrimitive(L, -1)->b = number(L, 2, 1.0f); return r; }
int primitive_gc(lua_State *L) { checkPrimitive(L, 1)->~LuaPrimitive(); return 0; }
int primitive_set(lua_State *L) {
	LuaPrimitive *p = checkPrimitive(L, 1);
	p->a = number(L, 2, p->a);
	p->b = number(L, 3, p->b);
	p->c = number(L, 4, p->c);
	return 0;
}

void pushVert(std::vector<float> &v, float x, float y, float z, float nx, float ny, float nz, const Color &c) {
	v.insert(v.end(), {x, y, z, c.r, c.g, c.b, c.a, nx, ny, nz});
}

void buildCone(std::vector<float> &v, std::vector<uint32_t> &idx, float radius, float height, const Color &c) {
	const int seg = 24;
	for(int i = 0; i < seg; ++i) {
		float a0 = (float)i / seg * (float)M_PI * 2.0f;
		float a1 = (float)(i + 1) / seg * (float)M_PI * 2.0f;
		uint32_t base = (uint32_t)(v.size() / 10);
		pushVert(v, 0, height * 0.5f, 0, 0, 1, 0, c);
		pushVert(v, std::cos(a0) * radius, -height * 0.5f, std::sin(a0) * radius, std::cos(a0), 0, std::sin(a0), c);
		pushVert(v, std::cos(a1) * radius, -height * 0.5f, std::sin(a1) * radius, std::cos(a1), 0, std::sin(a1), c);
		idx.insert(idx.end(), {base, base + 1, base + 2});
	}
}

void buildCylinder(std::vector<float> &v, std::vector<uint32_t> &idx, float radius, float height, const Color &c) {
	const int seg = 24;
	for(int i = 0; i < seg; ++i) {
		float a0 = (float)i / seg * (float)M_PI * 2.0f;
		float a1 = (float)(i + 1) / seg * (float)M_PI * 2.0f;
		uint32_t base = (uint32_t)(v.size() / 10);
		pushVert(v, std::cos(a0) * radius, -height * 0.5f, std::sin(a0) * radius, std::cos(a0), 0, std::sin(a0), c);
		pushVert(v, std::cos(a0) * radius, height * 0.5f, std::sin(a0) * radius, std::cos(a0), 0, std::sin(a0), c);
		pushVert(v, std::cos(a1) * radius, height * 0.5f, std::sin(a1) * radius, std::cos(a1), 0, std::sin(a1), c);
		pushVert(v, std::cos(a1) * radius, -height * 0.5f, std::sin(a1) * radius, std::cos(a1), 0, std::sin(a1), c);
		idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
}


void buildIcoSphere(std::vector<float> &v, std::vector<uint32_t> &idx,
                    float radius, int resolution, const Color &color) {
	float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
	std::vector<std::array<float, 3>> verts = {
		{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
		{0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
		{t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}
	};
	for(auto &vert : verts) {
		float length = std::sqrt(vert[0]*vert[0] + vert[1]*vert[1] + vert[2]*vert[2]);
		vert[0] /= length; vert[1] /= length; vert[2] /= length;
	}
	std::vector<std::array<int, 3>> faces = {
		{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
		{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
		{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
		{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
	};
	std::map<std::pair<int,int>, int> cache;
	auto midpoint = [&](int a, int b) {
		std::pair<int,int> key(std::min(a,b), std::max(a,b));
		auto found = cache.find(key);
		if(found != cache.end()) return found->second;
		std::array<float,3> point = {
			(verts[a][0] + verts[b][0]) * 0.5f,
			(verts[a][1] + verts[b][1]) * 0.5f,
			(verts[a][2] + verts[b][2]) * 0.5f
		};
		float length = std::sqrt(point[0]*point[0] + point[1]*point[1] + point[2]*point[2]);
		point[0] /= length; point[1] /= length; point[2] /= length;
		verts.push_back(point);
		int index = (int)verts.size() - 1;
		cache[key] = index;
		return index;
	};
	for(int r = 0; r < resolution; ++r) {
		std::vector<std::array<int,3>> next;
		for(const auto &face : faces) {
			int a = midpoint(face[0], face[1]);
			int b = midpoint(face[1], face[2]);
			int c = midpoint(face[2], face[0]);
			next.push_back({face[0], a, c});
			next.push_back({face[1], b, a});
			next.push_back({face[2], c, b});
			next.push_back({a, b, c});
		}
		faces = next;
	}
	for(const auto &vert : verts) {
		pushVert(v, vert[0] * radius, vert[1] * radius, vert[2] * radius,
		         vert[0], vert[1], vert[2], color);
	}
	for(const auto &face : faces) {
		idx.push_back((uint32_t)face[0]);
		idx.push_back((uint32_t)face[1]);
		idx.push_back((uint32_t)face[2]);
	}
}

int primitive_draw(lua_State *L) {
	LuaPrimitive *p = checkPrimitive(L, 1);
	if(env().renderer == nullptr) return 0;
	float x = number(L, 2), y = number(L, 3), z = number(L, 4);
	Color color = env().renderer->color();
	std::vector<float> v;
	std::vector<uint32_t> idx;
	switch(p->type) {
		case 0: {
			float h = p->a * 0.5f;
			const float corners[6][3] = {{h,-h,h},{-h,-h,h},{h,h,h},{-h,h,h},{h,-h,-h},{-h,-h,-h}};
			(void)corners;
			// reuse drawBox via a temporary path: emit a cube mesh
			struct Face { float nx, ny, nz; float v[4][3]; };
			const Face faces[6] = {
				{0,0,1,{{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}}},
				{0,0,-1,{{h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}}},
				{1,0,0,{{h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}}},
				{-1,0,0,{{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}}},
				{0,1,0,{{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}}},
				{0,-1,0,{{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}}},
			};
			for(const Face &f : faces) {
				uint32_t base = (uint32_t)(v.size() / 10);
				for(int i = 0; i < 4; ++i) pushVert(v, f.v[i][0], f.v[i][1], f.v[i][2], f.nx, f.ny, f.nz, color);
				idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
			}
			break;
		}
		case 1: {
			const int rings = 16, segs = 24;
			float radius = p->a;
			for(int r = 0; r <= rings; ++r) {
				float phi = (float)r / rings * (float)M_PI;
				for(int s = 0; s <= segs; ++s) {
					float theta = (float)s / segs * (float)M_PI * 2.0f;
					float nx = std::sin(phi) * std::cos(theta);
					float ny = std::cos(phi);
					float nz = std::sin(phi) * std::sin(theta);
					pushVert(v, nx * radius, ny * radius, nz * radius, nx, ny, nz, color);
				}
			}
			for(int r = 0; r < rings; ++r) {
				for(int s = 0; s < segs; ++s) {
					uint32_t a = (uint32_t)(r * (segs + 1) + s);
					uint32_t b = a + segs + 1;
					idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
				}
			}
			break;
		}
		case 2: buildCone(v, idx, p->a, p->b, color); break;
		case 3: buildCylinder(v, idx, p->a, p->b, color); break;
		case 5: buildIcoSphere(v, idx, p->a, (int)p->b, color); break;
		case 4: {
			float w = p->a * 0.5f, d = p->b * 0.5f;
			pushVert(v, -w, 0, -d, 0, 1, 0, color);
			pushVert(v, w, 0, -d, 0, 1, 0, color);
			pushVert(v, w, 0, d, 0, 1, 0, color);
			pushVert(v, -w, 0, d, 0, 1, 0, color);
			idx.insert(idx.end(), {0, 1, 2, 0, 2, 3});
			break;
		}
	}
	env().renderer->pushMatrix();
	env().renderer->translate3D(x, y, z);
	env().renderer->drawMesh3D(v, idx, false);
	env().renderer->popMatrix();
	return 0;
}

//--------------------------------------------------------------
} // namespace

//--------------------------------------------------------------
int easycam_new(lua_State *L) {
	// ofEasyCam shares the Camera behaviour; mouse orbit is not reimplemented
	lua_getglobal(L, "of");
	lua_getfield(L, -1, "Camera");
	lua_remove(L, -2);
	lua_call(L, 0, 1);
	return 1;
}

//--------------------------------------------------------------
void registerMiscBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kRectMeta);
	lua_pushcfunction(L, rect_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "set", rect_set);
	setFunction(L, "getX", rect_getX);
	setFunction(L, "getY", rect_getY);
	setFunction(L, "getWidth", rect_getWidth);
	setFunction(L, "getHeight", rect_getHeight);
	setFunction(L, "getArea", rect_getArea);
	setFunction(L, "getCenter", rect_getCenter);
	setFunction(L, "inside", rect_inside);
	setFunction(L, "intersects", rect_intersects);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kFpsMeta);
	lua_pushcfunction(L, fps_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "update", fps_update);
	setFunction(L, "getFps", fps_getFps);
	setFunction(L, "getFrameRate", fps_getFrameRate);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kParamMeta);
	lua_pushcfunction(L, param_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "set", param_set);
	setFunction(L, "get", param_get);
	setFunction(L, "getName", param_getName);
	setFunction(L, "setName", param_setName);
	setFunction(L, "setMin", param_setMin);
	setFunction(L, "setMax", param_setMax);
	setFunction(L, "getMin", param_getMin);
	setFunction(L, "getMax", param_getMax);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kNodeMeta);
	lua_pushcfunction(L, node_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "setPosition", node_setPosition);
	setFunction(L, "move", node_move);
	setFunction(L, "getPosition", node_getPosition);
	setFunction(L, "getX", node_getX);
	setFunction(L, "getY", node_getY);
	setFunction(L, "getZ", node_getZ);
	setFunction(L, "setScale", node_setScale);
	setFunction(L, "rotateDeg", node_rotateDeg);
	setFunction(L, "rotateXDeg", node_rotateX);
	setFunction(L, "rotateYDeg", node_rotateY);
	setFunction(L, "resetTransform", node_reset);
	setFunction(L, "transformGL", node_transform);
	setFunction(L, "restoreTransformGL", node_restore);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kMaterialMeta);
	lua_pushcfunction(L, material_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "setDiffuseColor", material_setDiffuse);
	setFunction(L, "setAmbientColor", material_setAmbient);
	setFunction(L, "setSpecularColor", material_setSpecular);
	setFunction(L, "begin", material_begin);
	setFunction(L, "endMaterial", material_end);
	setFunction(L, "finish", material_end);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kLightMeta);
	lua_pushcfunction(L, light_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "enable", light_enable);
	setFunction(L, "disable", light_disable);
	setFunction(L, "getIsEnabled", light_isEnabled);
	setFunction(L, "setPosition", light_setPosition);
	setFunction(L, "setDiffuseColor", light_setDiffuse);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kPrimitiveMeta);
	lua_pushcfunction(L, primitive_gc); lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	setFunction(L, "set", primitive_set);
	setFunction(L, "draw", primitive_draw);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, rect_new); lua_setfield(L, ofTable, "Rectangle");
	lua_pushcfunction(L, fps_new); lua_setfield(L, ofTable, "FpsCounter");
	lua_pushcfunction(L, param_new); lua_setfield(L, ofTable, "Parameter");
	lua_pushcfunction(L, node_new); lua_setfield(L, ofTable, "Node");
	lua_pushcfunction(L, material_new); lua_setfield(L, ofTable, "Material");
	lua_pushcfunction(L, light_new); lua_setfield(L, ofTable, "Light");
	lua_pushcfunction(L, box_new); lua_setfield(L, ofTable, "BoxPrimitive");
	lua_pushcfunction(L, sphere_new); lua_setfield(L, ofTable, "SpherePrimitive");
	lua_pushcfunction(L, cone_new); lua_setfield(L, ofTable, "ConePrimitive");
	lua_pushcfunction(L, cylinder_new); lua_setfield(L, ofTable, "CylinderPrimitive");
	lua_pushcfunction(L, plane_new); lua_setfield(L, ofTable, "PlanePrimitive");
	lua_pushcfunction(L, ico_new); lua_setfield(L, ofTable, "IcoSpherePrimitive");
	lua_pushcfunction(L, easycam_new); lua_setfield(L, ofTable, "EasyCam");
}

} // namespace loaf
