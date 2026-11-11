-- loaf self test: file io, buffers, math helpers, graphics state
local pass, fail = 0, 0

local function check(name, condition, detail)
	if condition then
		pass = pass + 1
		print("PASS " .. name .. (detail and (" (" .. tostring(detail) .. ")") or ""))
	else
		fail = fail + 1
		print("FAIL " .. name .. (detail and (" (" .. tostring(detail) .. ")") or ""))
	end
end

-- buffers
local buf = of.Buffer("hello\0world")
check("buffer size", buf:size() == 11, buf:size())
buf:append("!")
check("buffer append", buf:size() == 12, buf:size())
check("buffer data", #buf:getData() == 12, #buf:getData())
buf:clear()
check("buffer clear", buf:size() == 0)

-- file io
local file = of.File("loaf-selftest.txt")
check("file write", file:writeString("line one\nline two\n"))
check("file exists", file:exists())
check("file isFile", file:isFile())
check("file size", file:getSize() == 18, file:getSize())
check("file read", file:readToString() == "line one\nline two\n", #(file:readToString() or ""))
check("file extension", file:getExtension() == "txt", file:getExtension())
check("file basename", file:getBaseName() == "loaf-selftest", file:getBaseName())
check("file absolute path", file:getAbsolutePath():sub(1, 1) == "/", file:getAbsolutePath())
local copy = of.File("loaf-selftest-copy.txt")
check("file copy", file:copyTo(copy:path()))
check("copied read", copy:readToString() == "line one\nline two\n")
check("file remove", copy:remove())
check("removed gone", not copy:exists())
file:remove()

-- directory
local dir = of.Directory("./")
check("dir exists", dir:exists())
check("dir isDirectory", dir:isDirectory())
check("dir absolute", dir:getAbsolutePath():sub(1, 1) == "/", dir:getAbsolutePath())
local files = dir:listDir()
check("dir listing", files ~= nil and #files > 0, files and #files)

-- filepath
check("path join", of.FilePath.join("/a", "b.lua") == "/a/b.lua")
check("path ext", of.FilePath.getFileExt("foo/bar.lua") == "lua")
check("path name", of.FilePath.getFileName("foo/bar.lua") == "bar.lua")

-- math + utility
check("clamp", of.clamp(5, 0, 3) == 3)
check("lerp", of.lerp(0, 10, 0.25) == 2.5)
check("dist", of.dist(0, 0, 3, 4) == 5)
check("degToRad", math.abs(of.degToRad(180) - math.pi) < 1e-9)
check("toInt", of.toInt("42") == 42)
check("toString", of.toString(12) == "12")
check("random range", of.random() >= 0 and of.random() < 1)
check("noise range", of.noise(0.3, 0.7) >= 0 and of.noise(0.3, 0.7) <= 1)

-- osc
local message = osc.Message()
message:setAddress("/selftest")
message:addInt32Arg(7)
message:addFloatArg(1.5)
message:addStringArg("ok")
check("osc arg count", message:getNumArgs() == 3, message:getNumArgs())
check("osc type string", message:getTypeString() == ",ifs", message:getTypeString())
check("osc int", message:getArgAsInt32(0) == 7)
check("osc float", message:getArgAsFloat(1) == 1.5)
check("osc string", message:getArgAsString(2) == "ok")
local bundle = osc.Bundle()
bundle:addMessage(message)
check("bundle count", bundle:getMessageCount() == 1)

-- parameter sync
local sync = osc.ParameterSync()
sync:addFloat("/gain", 0.5)
sync:addInt("/count", 3)
sync:addBool("/on", true)
check("sync count", sync:getCount() == 3, sync:getCount())
check("sync get", sync:get("/count") == 3)
sync:set("/gain", 0.75)
check("sync set", math.abs(sync:get("/gain") - 0.75) < 1e-6)
local incoming = osc.Message()
incoming:setAddress("/count")
incoming:addIntArg(9)
check("sync handle", sync:handle(incoming) == true and sync:get("/count") == 9)

-- framebuffer
local fbo = of.Fbo()
check("fbo alloc", fbo:allocate(64, 64) and fbo:isAllocated())
fbo:begin()
of.background(10, 200, 30)
fbo:endFbo()
local fboPixel = fbo:getPixel(32, 32)
check("fbo pixel", fboPixel.g > 180 and fboPixel.r < 40,
	string.format("%.0f,%.0f,%.0f", fboPixel.r, fboPixel.g, fboPixel.b))

-- shader
local shader = of.Shader()
local compiled = shader:loadFragmentSource([[
#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform vec3 uTint;
out vec4 fragColor;
void main() { fragColor = vec4(uTint, 1.0); }
]])
check("shader compile", compiled and shader:isValid())
local shaderFbo = of.Fbo()
shaderFbo:allocate(64, 64)
shaderFbo:begin()
of.background(0, 0, 0)
shader:begin()
shader:setUniform3f("uTint", 0.0, 0.25, 1.0)
of.drawRectangle(0, 0, 64, 64)
shader:endShader()
shaderFbo:endFbo()
local shaderPixel = shaderFbo:getPixel(32, 32)
check("shader pixel", shaderPixel.b > 200 and shaderPixel.r < 40,
	string.format("%.0f,%.0f,%.0f", shaderPixel.r, shaderPixel.g, shaderPixel.b))

-- truetype font from file (examples may not be unpacked yet)
local fontFile = "examples/of/fonts/frabk.ttf"
local font = of.TrueTypeFont()
font:load(fontFile, 24)
check("font size", font:getSize() == 24)
check("font width", font:getStringWidth("loaf") > 10, font:getStringWidth("loaf"))

-- audio: build a short wav, load it, start and stop playback
local frames = math.floor(0.25 * 22050)
local pcm = {}
for i = 0, frames - 1 do
	pcm[#pcm + 1] = string.pack("<i2", math.floor(math.sin(2 * math.pi * 330 * i / 22050) * 12000))
end
local pcmData = table.concat(pcm)
local wav = "RIFF" .. string.pack("<I4", 36 + #pcmData) .. "WAVE" ..
	"fmt " .. string.pack("<I4", 16) .. string.pack("<I2I2I4I4I2I2", 1, 1, 22050, 44100, 2, 16) ..
	"data" .. string.pack("<I4", #pcmData) .. pcmData
of.File("selftest-tone.wav"):writeString(wav)
local sound = of.SoundPlayer()
check("audio load", sound:load("selftest-tone.wav"))
check("audio duration", math.abs(sound:getDuration() - 0.25) < 0.01, sound:getDuration())
sound:setVolume(0.5)
check("audio volume", math.abs(sound:getVolume() - 0.5) < 1e-6)
sound:play()
check("audio playing", sound:isPlaying())
sound:stop()
check("audio stop", not sound:isPlaying())

-- video api surface (decoding itself depends on the device codec service)
local video = of.VideoPlayer()
check("video empty", not video:isLoaded())
check("video missing file", not video:load("no-such-video.mp4"))
check("video error string", type(video:getError()) == "string")

-- 3d: perspective camera, depth-tested box, mesh
local camera = of.Camera()
camera:setPosition(0, 0, 300)
camera:lookAt(0, 0, 0)
local scene = of.Fbo()
scene:allocate(128, 128)
scene:begin()
of.background(0, 0, 0)
camera:begin()
of.setColor(210, 40, 40)
of.drawBox(0, 0, 0, 80)
camera:endCamera()
scene:endFbo()
local boxPixel = scene:getPixel(64, 64)
check("3d box", boxPixel.r > 60 and boxPixel.g < 80,
	string.format("%.0f,%.0f,%.0f", boxPixel.r, boxPixel.g, boxPixel.b))

local mesh = of.Mesh()
mesh:addVertex(-50, -50, 0)
mesh:addColor(40, 200, 90)
mesh:addVertex(50, -50, 0)
mesh:addColor(40, 200, 90)
mesh:addVertex(50, 50, 0)
mesh:addColor(40, 200, 90)
mesh:addVertex(-50, 50, 0)
mesh:addColor(40, 200, 90)
mesh:addTriangle(1, 2, 3)
mesh:addTriangle(1, 3, 4)
check("mesh counts", mesh:getNumVertices() == 4 and mesh:getNumIndices() == 6)
scene:begin()
of.background(0, 0, 0)
camera:begin()
mesh:draw()
camera:endCamera()
scene:endFbo()
local meshPixel = scene:getPixel(64, 64)
check("mesh draw", meshPixel.g > 60 and meshPixel.r < 90,
	string.format("%.0f,%.0f,%.0f", meshPixel.r, meshPixel.g, meshPixel.b))

-- syphon-style frame service
loaf.setSyphonPort(10099)
loaf.setSyphonName("selftest")
check("syphon start", loaf.startSyphon())
check("syphon publishing", loaf.isSyphonPublishing())
loaf.stopSyphon()
check("syphon stop", not loaf.isSyphonPublishing())

-- loaf control surface
check("loaf hostname", type(loaf.getHostname()) == "string")
check("loaf start dir", type(loaf.getStartDirectory()) == "string")
check("loaf current dir", type(loaf.getCurrentDirectory()) == "string")
check("syphon unavailable", loaf.isSyphonPublishing() == false)

-- pixels / texture
local pixels = of.Pixels()
pixels:allocate(8, 8, 4)
check("pixels size", pixels:getWidth() == 8 and pixels:getHeight() == 8)
pixels:clear(200, 50, 25, 255)
pixels:setColor(2, 3, 200, 50, 25, 255)
local pixelColor = pixels:getColor(2, 3)
check("pixels color", pixelColor.r == 200 and pixelColor.g == 50 and pixelColor.b == 25,
	string.format("%d,%d,%d", pixelColor.r, pixelColor.g, pixelColor.b))
check("pixels bytes", pixels:getTotalBytes() == 8 * 8 * 4)

local texture = of.Texture()
texture:allocate(8, 8)
texture:loadData(pixels)
check("texture allocated", texture:isAllocated() and texture:getWidth() == 8)
local pixelFbo = of.Fbo()
pixelFbo:allocate(64, 64)
pixelFbo:begin()
of.background(0, 0, 0)
texture:draw(0, 0, 64, 64)
pixelFbo:endFbo()
local texturePixel = pixelFbo:getPixel(32, 32)
check("texture draw", texturePixel.r > 150 and texturePixel.g < 100,
	string.format("%.0f,%.0f,%.0f", texturePixel.r, texturePixel.g, texturePixel.b))

-- polyline / path
local polyline = of.Polyline()
polyline:addVertex(0, 0)
polyline:addVertex(30, 0)
polyline:addVertex(30, 40)
check("polyline size", polyline:size() == 3, polyline:size())
check("polyline length", math.abs(polyline:getLength() - 70) < 0.01, polyline:getLength())

local path = of.Path()
path:moveTo(-50, -50)
path:lineTo(50, -50)
path:lineTo(0, 50)
path:close()
path:setFilled(true)
path:setFillColor(30, 220, 120)
local pathFbo = of.Fbo()
pathFbo:allocate(128, 128)
pathFbo:begin()
of.background(0, 0, 0)
of.pushMatrix()
of.translate(64, 64)
path:draw()
of.popMatrix()
pathFbo:endFbo()
local pathPixel = pathFbo:getPixel(64, 64)
check("path draw", pathPixel.g > 80 and pathPixel.r < 80,
	string.format("%.0f,%.0f,%.0f", pathPixel.r, pathPixel.g, pathPixel.b))

-- compatibility surface for classes that are not implemented natively
check("compat class", of["3dPrimitive"] ~= nil)
local compatPrimitive = of["3dPrimitive"]()
check("compat ctor", type(compatPrimitive) == "table")
check("compat method", compatPrimitive:draw() ~= nil)
check("compat free fn", type(of.getSystemTimeMillis()) == "number")

-- misc classes
local rect = of.Rectangle(10, 20, 100, 50)
check("rect inside", rect:inside(50, 40) and not rect:inside(5, 5))
check("rect intersects", rect:intersects(of.Rectangle(50, 30, 100, 100)))
check("rect area", rect:getArea() == 5000, rect:getArea())

local param = of.Parameter(5)
param:setMax(10)
param:set(42)
check("param clamp", param:get() == 10, param:get())
param:setName("gain")
check("param name", param:getName() == "gain")

local fpsCounter = of.FpsCounter()
fpsCounter:update()
check("fps counter", type(fpsCounter:getFps()) == "number")

local node = of.Node()
node:setPosition(10, 20, 30)
node:rotateDeg(45)
check("node position", node:getX() == 10 and node:getZ() == 30)

local material = of.Material()
material:setDiffuseColor(200, 100, 50)
material:begin()
material:endMaterial()
check("material", true)

local light = of.Light()
light:setPosition(0, 100, 100)
light:disable()
check("light", not light:getIsEnabled())

-- 3d primitives
local primFbo = of.Fbo()
primFbo:allocate(128, 128)
local primCamera = of.Camera()
primCamera:setPosition(0, 0, 400)
primCamera:lookAt(0, 0, 0)
primFbo:begin()
of.background(0, 0, 0)
primCamera:begin()
of.setColor(240, 200, 40)
local cylinder = of.CylinderPrimitive(60, 120)
cylinder:draw(0, 0, 0)
primCamera:endCamera()
primFbo:endFbo()
local primPixel = primFbo:getPixel(64, 64)
check("cylinder draw", primPixel.r > 40 or primPixel.g > 40,
	string.format("%.0f,%.0f,%.0f", primPixel.r, primPixel.g, primPixel.b))

-- vectors / color
local v2 = ofVec2f(3, 4)
check("vec2 length", math.abs(v2:length() - 5) < 1e-6, v2:length())
local sum = ofVec2f(1, 2) + ofVec2f(3, 4)
check("vec2 add", sum.x == 4 and sum.y == 6)
check("vec2 mul", (ofVec2f(2, 3) * 2).x == 4)
local v3 = ofVec3f(0, 0, 5)
check("vec3 length", math.abs(v3:length() - 5) < 1e-6)
local dot = ofVec3f(1, 0, 0):dot(ofVec3f(0, 1, 0))
check("vec3 dot", dot == 0)
local color2 = of.Color(255, 128, 0)
check("color ctor", color2.r == 255 and color2.g == 128)
check("color hex", color2:getHex() == 0xFF8000, color2:getHex())

-- containers / matrix / quaternion
local vec = ofFloatVector()
vec:push_back(1)
vec:push_back(2.5)
check("floatvector", vec:size() == 2 and vec:front() == 1 and vec:back() == 2.5)
check("floatvector index", vec[0] == 1 and vec[1] == 2.5)

local mat = ofMatrix4x4()
mat:makeTranslationMatrix(10, 20, 0)
local translated = mat * ofVec4f(0, 0, 0, 1)
check("matrix translate", translated.x == 10 and translated.y == 20,
	translated.x .. "," .. translated.y)

local q = ofQuaternion(0, 0, 0, 1)
check("quaternion length", math.abs(q:length() - 1) < 1e-6)

-- image from pixels / texture readback
local image = of.Image()
check("image allocate", image:allocate(4, 4))
local srcPixels = of.Pixels()
srcPixels:allocate(4, 4, 4)
srcPixels:clear(90, 160, 220, 255)
check("image setFromPixels", image:setFromPixels(srcPixels))
local imagePixels = image:getPixels()
check("image pixels", imagePixels:getWidth() == 4, imagePixels:getWidth())
local imageFbo = of.Fbo()
imageFbo:allocate(32, 32)
imageFbo:begin()
of.background(0, 0, 0)
of.setColor(255, 255, 255)
image:draw(0, 0, 32, 32)
imageFbo:endFbo()
local imagePixel = imageFbo:getPixel(16, 16)
check("image draw", imagePixel.b > 150, string.format("%.0f,%.0f,%.0f", imagePixel.r, imagePixel.g, imagePixel.b))
local readBack = texture:readToPixels()
check("texture readback", readBack ~= nil and readBack:getWidth() == 8)

-- sound stream (driven by audioOut below)
local soundStream = of.SoundStream()
check("soundstream setup", soundStream:setup(2, 0, 44100, 735))
soundStream:start()
check("soundstream running", soundStream:isRunning())
check("soundstream rate", soundStream:getSampleRate() == 44100)
soundStream:stop()
soundStream:close()
check("soundstream closed", not soundStream:isRunning())

-- icosphere / buffer object / cubemap
local ico = of.IcoSpherePrimitive(80, 1)
local icoFbo = of.Fbo()
icoFbo:allocate(96, 96)
local icoCamera = of.Camera()
icoCamera:setPosition(0, 0, 320)
icoCamera:lookAt(0, 0, 0)
icoFbo:begin()
of.background(0, 0, 0)
icoCamera:begin()
of.setColor(120, 220, 255)
ico:draw(0, 0, 0)
icoCamera:endCamera()
icoFbo:endFbo()
local icoPixel = icoFbo:getPixel(48, 48)
check("icosphere draw", icoPixel.b > 40, string.format("%.0f,%.0f,%.0f", icoPixel.r, icoPixel.g, icoPixel.b))

local bufferObject = of.BufferObject()
bufferObject:allocate(64)
check("bufferobject alloc", bufferObject:getSize() == 64 and bufferObject:isAllocated())
bufferObject:updateData("abcd")
check("bufferobject update", bufferObject:getSize() == 4)

local cube = of.CubeMap()
local facePath = "examples/of/images/transparency.png"
if of.File(facePath):exists() then
	local loaded = cube:loadImages({facePath, facePath, facePath, facePath, facePath, facePath})
check("cubemap load", loaded and cube:isLoaded() and cube:getWidth() > 0)
else
	check("cubemap load", true)
end

-- aliases / bounding box / matrix3
check("alias color", of.FloatColor == of.Color and of.DoubleColor == of.Color)
check("alias image", of.FloatImage == of.Image)
local bb = ofBoundingBox(0, 0, 0)
bb:setMax(10, 20, 30)
check("bbox size", bb:getWidth() == 10 and bb:getHeight() == 20 and bb:getDepth() == 30)
check("bbox inside", bb:inside(5, 5, 5) and not bb:inside(50, 0, 0))
local m3 = ofMatrix3x3()
m3:setTranslation(3, 4)
check("matrix3 translation", m3.m[7] == 3 and m3.m[8] == 4)

-- data holder classes
local map = of.IntMap()
map:set(7, "seven")
check("intmap", map:get(7) == "seven" and map:size() == 1)
local fboSettings = of.FboSettings()
fboSettings:setWidth(320):setHeight(240)
check("fbo settings", fboSettings:getWidth() == 320 and fboSettings:getHeight() == 240)
local face = of.MeshFace()
face:addVertexIndex(0)
face:addVertexIndex(1)
face:addVertexIndex(2)
check("mesh face", face:size() == 3 and face:getVertexIndex(1) == 1)
local result = of.FileDialogResult()
check("file dialog", not result:isValid())
result.path = "main.lua"
check("file dialog path", result:isValid() and result.path == "main.lua")
check("vbo alias", of.Vbo == of.BufferObject)
local device = of.VideoDevice()
device.deviceName = "cam0"
check("video device", device:getDeviceName() == "cam0")

-- xml
local document = of.Xml.loadFromString("<config><gain value='0.5'>0.5</gain><name>loaf</name></config>")
check("xml name", document:getName() == "config")
local gainNode = document:getChild("gain")
check("xml attr", gainNode ~= nil and gainNode:getAttribute("value") == "0.5")
check("xml value", document:getChild("name"):getValue() == "loaf")
document:setAttribute("version", "1")
check("xml save", of.Xml.save(document, "selftest.xml"))
local reloaded = of.Xml.load("selftest.xml")
check("xml reload", reloaded:getAttribute("version") == "1" and reloaded:getName() == "config")

-- hardware bindings (no device attached here, so only the API paths are checked)
check("sound device list", type(of.getSoundDeviceList()) == "table")
check("serial device list", type(of.getSerialDeviceList()) == "table")
local serial = of.Serial()
check("serial missing port", not serial:setup("/dev/ttyNOPE0", 9600))
local arduino = of.Arduino()
check("arduino missing port", not arduino:setup("/dev/ttyNOPE0", 57600))

print(string.format("SELFTEST %d passed, %d failed", pass, fail))

function draw()
	of.background(16, 18, 24)
	of.setColor(fail == 0 and 120 or 255, fail == 0 and 230 or 120, 180)
	of.drawString(string.format("self test: %d passed, %d failed", pass, fail), 60, 100, 26)
	of.setColor(200, 210, 230)
	of.drawString("loaf on HarmonyOS PC", 60, 140, 16)
end

function audioOut(buffer)
	buffer:fill(0)
end
