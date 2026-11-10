#include "Bindings3D.h"

#include "Environment.h"
#include "Math3D.h"

#include <cmath>
#include <vector>

namespace loaf {

namespace {

const char *kCameraMetatable = "loaf.Camera";
const char *kMeshMetatable = "loaf.Mesh";

struct LuaCamera {
	float x = 0.0f, y = 0.0f, z = 500.0f;
	float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
	float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
	float fov = 60.0f;
	float nearClip = 0.1f;
	float farClip = 10000.0f;
	bool active = false;
};

struct LuaMesh {
	std::vector<float> vertices; // x,y,z,r,g,b,a,nx,ny,nz
	std::vector<uint32_t> indices;
	bool useVertexColors = false;
};

float number(lua_State *L, int index, float fallback = 0.0f) {
	return lua_isnumber(L, index) ? (float)lua_tonumber(L, index) : fallback;
}

LuaCamera *checkCamera(lua_State *L, int index) {
	return static_cast<LuaCamera *>(luaL_checkudata(L, index, kCameraMetatable));
}

LuaMesh *checkMesh(lua_State *L, int index) {
	return static_cast<LuaMesh *>(luaL_checkudata(L, index, kMeshMetatable));
}

//--------------------------------------------------------------
int camera_new(lua_State *L) {
	LuaCamera *camera = static_cast<LuaCamera *>(lua_newuserdatauv(L, sizeof(LuaCamera), 0));
	new(camera) LuaCamera();
	luaL_getmetatable(L, kCameraMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int camera_gc(lua_State *L) {
	checkCamera(L, 1)->~LuaCamera();
	return 0;
}

int camera_setPosition(lua_State *L) {
	LuaCamera *camera = checkCamera(L, 1);
	camera->x = number(L, 2);
	camera->y = number(L, 3);
	camera->z = number(L, 4);
	return 0;
}

int camera_lookAt(lua_State *L) {
	LuaCamera *camera = checkCamera(L, 1);
	camera->targetX = number(L, 2);
	camera->targetY = number(L, 3);
	camera->targetZ = number(L, 4);
	return 0;
}

int camera_setFov(lua_State *L) {
	checkCamera(L, 1)->fov = number(L, 2, 60.0f);
	return 0;
}

int camera_setNearClip(lua_State *L) {
	checkCamera(L, 1)->nearClip = number(L, 2, 0.1f);
	return 0;
}

int camera_setFarClip(lua_State *L) {
	checkCamera(L, 1)->farClip = number(L, 2, 10000.0f);
	return 0;
}

int camera_begin(lua_State *L) {
	LuaCamera *camera = checkCamera(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	float aspect = (float)env().renderer->width() / (float)env().renderer->height();
	Mat4 projection = Mat4::perspective(camera->fov * (float)M_PI / 180.0f, aspect,
	                                    camera->nearClip, camera->farClip);
	Mat4 view = Mat4::lookAt(camera->x, camera->y, camera->z,
	                         camera->targetX, camera->targetY, camera->targetZ,
	                         camera->upX, camera->upY, camera->upZ);
	env().renderer->begin3D(projection, view);
	camera->active = true;
	return 0;
}

int camera_end(lua_State *L) {
	LuaCamera *camera = checkCamera(L, 1);
	if(camera->active && env().renderer != nullptr) {
		env().renderer->end3D();
	}
	camera->active = false;
	return 0;
}

//--------------------------------------------------------------
int mesh_new(lua_State *L) {
	LuaMesh *mesh = static_cast<LuaMesh *>(lua_newuserdatauv(L, sizeof(LuaMesh), 0));
	new(mesh) LuaMesh();
	luaL_getmetatable(L, kMeshMetatable);
	lua_setmetatable(L, -2);
	return 1;
}

int mesh_gc(lua_State *L) {
	checkMesh(L, 1)->~LuaMesh();
	return 0;
}

int mesh_clear(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	mesh->vertices.clear();
	mesh->indices.clear();
	mesh->useVertexColors = false;
	return 0;
}

int mesh_addVertex(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
	if(lua_gettop(L) >= 7) {
		r = number(L, 4, 255.0f) / 255.0f;
		g = number(L, 5, 255.0f) / 255.0f;
		b = number(L, 6, 255.0f) / 255.0f;
		a = number(L, 7, 255.0f) / 255.0f;
		mesh->useVertexColors = true;
	}
	mesh->vertices.push_back(number(L, 2));
	mesh->vertices.push_back(number(L, 3));
	mesh->vertices.push_back(number(L, 4));
	mesh->vertices.push_back(r);
	mesh->vertices.push_back(g);
	mesh->vertices.push_back(b);
	mesh->vertices.push_back(a);
	mesh->vertices.push_back(0.0f);
	mesh->vertices.push_back(1.0f);
	mesh->vertices.push_back(0.0f);
	return 0;
}

int mesh_addColor(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	if(mesh->vertices.size() < 10) {
		return 0;
	}
	size_t base = mesh->vertices.size() - 10;
	mesh->vertices[base + 3] = number(L, 2, 255.0f) / 255.0f;
	mesh->vertices[base + 4] = number(L, 3, 255.0f) / 255.0f;
	mesh->vertices[base + 5] = number(L, 4, 255.0f) / 255.0f;
	mesh->vertices[base + 6] = number(L, 5, 255.0f) / 255.0f;
	mesh->useVertexColors = true;
	return 0;
}

int mesh_addNormal(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	if(mesh->vertices.size() < 10) {
		return 0;
	}
	size_t base = mesh->vertices.size() - 10;
	mesh->vertices[base + 7] = number(L, 2);
	mesh->vertices[base + 8] = number(L, 3);
	mesh->vertices[base + 9] = number(L, 4);
	return 0;
}

int mesh_addIndex(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	int index = (int)number(L, 2, 0.0f);
	if(index >= 0) {
		mesh->indices.push_back((uint32_t)index);
	}
	return 0;
}

int mesh_addTriangle(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	mesh->indices.push_back((uint32_t)number(L, 2));
	mesh->indices.push_back((uint32_t)number(L, 3));
	mesh->indices.push_back((uint32_t)number(L, 4));
	return 0;
}

int mesh_getNumVertices(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)(checkMesh(L, 1)->vertices.size() / 10));
	return 1;
}

int mesh_getNumIndices(lua_State *L) {
	lua_pushinteger(L, (lua_Integer)checkMesh(L, 1)->indices.size());
	return 1;
}

int mesh_getVertex(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	int index = (int)number(L, 2, 1.0f) - 1;
	if(index < 0 || (size_t)(index * 10 + 10) > mesh->vertices.size()) {
		lua_pushnil(L);
		return 1;
	}
	lua_newtable(L);
	const char *names[10] = {"x", "y", "z", "r", "g", "b", "a", "nx", "ny", "nz"};
	for(int i = 0; i < 10; ++i) {
		lua_pushnumber(L, mesh->vertices[index * 10 + i]);
		lua_setfield(L, -2, names[i]);
	}
	return 1;
}

int mesh_draw(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	if(!mesh->useVertexColors) {
		Color color = env().renderer->color();
		for(size_t i = 0; i < mesh->vertices.size(); i += 10) {
			mesh->vertices[i + 3] = color.r;
			mesh->vertices[i + 4] = color.g;
			mesh->vertices[i + 5] = color.b;
			mesh->vertices[i + 6] = color.a;
		}
	}
	env().renderer->drawMesh3D(mesh->vertices, mesh->indices, false);
	return 0;
}

int mesh_drawWireframe(lua_State *L) {
	LuaMesh *mesh = checkMesh(L, 1);
	if(env().renderer == nullptr) {
		return 0;
	}
	env().renderer->drawMesh3D(mesh->vertices, mesh->indices, true);
	return 0;
}

//--------------------------------------------------------------
void pushVertex(LuaMesh &mesh, float x, float y, float z,
                float nx, float ny, float nz, const Color &color) {
	mesh.vertices.push_back(x);
	mesh.vertices.push_back(y);
	mesh.vertices.push_back(z);
	mesh.vertices.push_back(color.r);
	mesh.vertices.push_back(color.g);
	mesh.vertices.push_back(color.b);
	mesh.vertices.push_back(color.a);
	mesh.vertices.push_back(nx);
	mesh.vertices.push_back(ny);
	mesh.vertices.push_back(nz);
	mesh.useVertexColors = true;
}

int l_drawBox(lua_State *L) {
	if(env().renderer == nullptr) {
		return 0;
	}
	float x = number(L, 1);
	float y = number(L, 2);
	float z = number(L, 3);
	float w = number(L, 4, 100.0f);
	float h = number(L, 5, w);
	float d = number(L, 6, w);
	Color color = env().renderer->color();

	LuaMesh mesh;
	float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
	struct Face { float nx, ny, nz; float v[4][3]; };
	const Face faces[6] = {
		{ 0, 0, 1, {{-hw, -hh, hd}, {hw, -hh, hd}, {hw, hh, hd}, {-hw, hh, hd}}},
		{ 0, 0, -1, {{hw, -hh, -hd}, {-hw, -hh, -hd}, {-hw, hh, -hd}, {hw, hh, -hd}}},
		{ 1, 0, 0, {{hw, -hh, hd}, {hw, -hh, -hd}, {hw, hh, -hd}, {hw, hh, hd}}},
		{ -1, 0, 0, {{-hw, -hh, -hd}, {-hw, -hh, hd}, {-hw, hh, hd}, {-hw, hh, -hd}}},
		{ 0, 1, 0, {{-hw, hh, hd}, {hw, hh, hd}, {hw, hh, -hd}, {-hw, hh, -hd}}},
		{ 0, -1, 0, {{-hw, -hh, -hd}, {hw, -hh, -hd}, {hw, -hh, hd}, {-hw, -hh, hd}}},
	};
	for(const Face &face : faces) {
		uint32_t base = (uint32_t)(mesh.vertices.size() / 10);
		for(int i = 0; i < 4; ++i) {
			pushVertex(mesh, x + face.v[i][0], y + face.v[i][1], z + face.v[i][2],
			           face.nx, face.ny, face.nz, color);
		}
		mesh.indices.push_back(base);
		mesh.indices.push_back(base + 1);
		mesh.indices.push_back(base + 2);
		mesh.indices.push_back(base);
		mesh.indices.push_back(base + 2);
		mesh.indices.push_back(base + 3);
	}
	env().renderer->drawMesh3D(mesh.vertices, mesh.indices, false);
	return 0;
}

int l_drawSphere(lua_State *L) {
	if(env().renderer == nullptr) {
		return 0;
	}
	float cx = number(L, 1);
	float cy = number(L, 2);
	float cz = number(L, 3);
	float radius = number(L, 4, 50.0f);
	int rings = 16;
	int segments = 24;
	Color color = env().renderer->color();

	LuaMesh mesh;
	for(int ring = 0; ring <= rings; ++ring) {
		float v = (float)ring / rings;
		float phi = v * (float)M_PI;
		for(int segment = 0; segment <= segments; ++segment) {
			float u = (float)segment / segments;
			float theta = u * (float)M_PI * 2.0f;
			float nx = std::sin(phi) * std::cos(theta);
			float ny = std::cos(phi);
			float nz = std::sin(phi) * std::sin(theta);
			pushVertex(mesh, cx + nx * radius, cy + ny * radius, cz + nz * radius,
			           nx, ny, nz, color);
		}
	}
	for(int ring = 0; ring < rings; ++ring) {
		for(int segment = 0; segment < segments; ++segment) {
			uint32_t a = (uint32_t)(ring * (segments + 1) + segment);
			uint32_t b = a + (uint32_t)segments + 1;
			mesh.indices.push_back(a);
			mesh.indices.push_back(b);
			mesh.indices.push_back(a + 1);
			mesh.indices.push_back(a + 1);
			mesh.indices.push_back(b);
			mesh.indices.push_back(b + 1);
		}
	}
	env().renderer->drawMesh3D(mesh.vertices, mesh.indices, false);
	return 0;
}

} // namespace

//--------------------------------------------------------------
void register3DBindings(lua_State *L, int ofTable) {
	luaL_newmetatable(L, kCameraMetatable);
	lua_pushcfunction(L, camera_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, camera_setPosition);
	lua_setfield(L, -2, "setPosition");
	lua_pushcfunction(L, camera_lookAt);
	lua_setfield(L, -2, "lookAt");
	lua_pushcfunction(L, camera_lookAt);
	lua_setfield(L, -2, "setTarget");
	lua_pushcfunction(L, camera_setFov);
	lua_setfield(L, -2, "setFov");
	lua_pushcfunction(L, camera_setNearClip);
	lua_setfield(L, -2, "setNearClip");
	lua_pushcfunction(L, camera_setFarClip);
	lua_setfield(L, -2, "setFarClip");
	lua_pushcfunction(L, camera_begin);
	lua_setfield(L, -2, "begin");
	lua_pushcfunction(L, camera_end);
	lua_setfield(L, -2, "endCamera");
	lua_pushcfunction(L, camera_end);
	lua_setfield(L, -2, "end_");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	luaL_newmetatable(L, kMeshMetatable);
	lua_pushcfunction(L, mesh_gc);
	lua_setfield(L, -2, "__gc");
	lua_newtable(L);
	lua_pushcfunction(L, mesh_clear);
	lua_setfield(L, -2, "clear");
	lua_pushcfunction(L, mesh_addVertex);
	lua_setfield(L, -2, "addVertex");
	lua_pushcfunction(L, mesh_addColor);
	lua_setfield(L, -2, "addColor");
	lua_pushcfunction(L, mesh_addNormal);
	lua_setfield(L, -2, "addNormal");
	lua_pushcfunction(L, mesh_addIndex);
	lua_setfield(L, -2, "addIndex");
	lua_pushcfunction(L, mesh_addTriangle);
	lua_setfield(L, -2, "addTriangle");
	lua_pushcfunction(L, mesh_getNumVertices);
	lua_setfield(L, -2, "getNumVertices");
	lua_pushcfunction(L, mesh_getNumIndices);
	lua_setfield(L, -2, "getNumIndices");
	lua_pushcfunction(L, mesh_getVertex);
	lua_setfield(L, -2, "getVertex");
	lua_pushcfunction(L, mesh_draw);
	lua_setfield(L, -2, "draw");
	lua_pushcfunction(L, mesh_drawWireframe);
	lua_setfield(L, -2, "drawWireframe");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	lua_pushcfunction(L, camera_new);
	lua_setfield(L, ofTable, "Camera");
	lua_pushcfunction(L, mesh_new);
	lua_setfield(L, ofTable, "Mesh");
	lua_pushcfunction(L, l_drawBox);
	lua_setfield(L, ofTable, "drawBox");
	lua_pushcfunction(L, l_drawSphere);
	lua_setfield(L, ofTable, "drawSphere");
}

} // namespace loaf
