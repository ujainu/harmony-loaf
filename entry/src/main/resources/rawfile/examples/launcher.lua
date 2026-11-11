-- launcher.lua
-- A small button launcher for loaf.
--
-- Usage:
--   1. Drag this file onto the loaf window.
--   2. Click a card (or press 1-6) to load that sketch.
--   3. To come back, drag this file onto the loaf window again.
--
-- It loads sketches by sending an OSC message to loaf's own listener:
--   /loaf/load <absolute path>
-- This is the same mechanism loaf exposes for remote control.

local function fileExists(path)
	local f = io.open(path, "r")
	if f then
		f:close()
		return true
	end
	return false
end

local function findExamples()
	local start = loaf.getStartDirectory()
	local cwd = loaf.getCurrentDirectory()
	local home = os.getenv("HOME") or ""
	local candidates = {
		start .. "/examples",
		start .. "/../examples",
		cwd .. "/examples",
		cwd .. "/scripts/examples",
		home .. "/Desktop/dsh/harmonypc/loaf/src_loaf/examples",
		"/Users/dumingzhu/Desktop/dsh/harmonypc/loaf/src_loaf/examples",
	}
	for _, path in ipairs(candidates) do
		if fileExists(path .. "/helloworld/helloworld.lua") then
			return path
		end
	end
	return candidates[1]
end

local EXAMPLES = findExamples()

local sketches = {
	{ title = "Hello World",  subtitle = "OSC + animated circle", path = EXAMPLES .. "/helloworld/helloworld.lua" },
	{ title = "Graphics",     subtitle = "2D primitives",         path = EXAMPLES .. "/of/graphicsExample.lua" },
	{ title = "Fonts",        subtitle = "TrueType text",         path = EXAMPLES .. "/of/fontsExample.lua" },
	{ title = "Images",       subtitle = "load png / jpg",        path = EXAMPLES .. "/of/imageLoaderExample.lua" },
	{ title = "Polygon",      subtitle = "shapes + winding",      path = EXAMPLES .. "/of/polygonExample.lua" },
	{ title = "Dragon Curve", subtitle = "FBO + lines",           path = EXAMPLES .. "/dragoncurve.lua" },
}

local W, H = 960, 600
local cards = {}
local fontTitle, fontCard, fontSmall
local hoverIndex = 0
local status = ""
local statusTime = 0
local sender = nil

local function now()
	return of.getElapsedTimef()
end

local function loadSketch(index)
	local sketch = sketches[index]
	if not sketch then return end
	if not fileExists(sketch.path) then
		status = "missing: " .. sketch.path
		statusTime = now()
		return
	end
	if not sender then
		status = "OSC sender not ready"
		statusTime = now()
		return
	end
	status = "loading " .. sketch.title .. " ..."
	statusTime = now()
	local message = osc.Message()
	message:setAddress("/loaf/load")
	message:addStringArg(sketch.path)
	sender:sendMessage(message)
end

local function loadFont(path, size)
	local font = of.TrueTypeFont()
	local ok = pcall(function() font:load(path, size) end)
	if not ok then return nil end
	return font
end

local function drawText(font, text, x, y)
	if font then
		font:drawString(text, x, y)
	else
		of.drawBitmapString(text, x, y)
	end
end

local function drawCard(card, index)
	local sketch = card.sketch
	local hover = index == hoverIndex

	of.fill()
	if hover then
		of.setColor(44, 39, 30)
	else
		of.setColor(28, 28, 33)
	end
	of.drawRectangle(card.x, card.y, card.w, card.h)

	of.setColor(255, 170, 50)
	of.drawRectangle(card.x, card.y, 4, card.h)

	of.noFill()
	of.setLineWidth(hover and 2 or 1)
	if hover then
		of.setColor(255, 170, 50)
	else
		of.setColor(62, 62, 70)
	end
	of.drawRectangle(card.x, card.y, card.w, card.h)
	of.fill()

	of.setColor(120, 120, 132)
	drawText(fontSmall, string.format("%02d", index), card.x + 18, card.y + 28)

	of.setColor(238, 238, 242)
	drawText(fontCard, sketch.title, card.x + 18, card.y + 70)

	of.setColor(150, 150, 162)
	drawText(fontSmall, sketch.subtitle, card.x + 18, card.y + 96)

	if hover then
		of.setColor(255, 190, 80)
	else
		of.setColor(170, 150, 110)
	end
	drawText(fontSmall, "click to run  >", card.x + 18, card.y + 122)
end

function setup()
	of.setWindowTitle("loaf launcher")
	of.setWindowShape(W, H)
	of.setFrameRate(60)
	of.setCircleResolution(60)
	of.background(18)

	local fontPath = EXAMPLES .. "/of/fonts/verdana.ttf"
	fontTitle = loadFont(fontPath, 30)
	fontCard = loadFont(fontPath, 20)
	fontSmall = loadFont(fontPath, 12)

	loaf.setListenPort(9990)
	loaf.startListening()
	sender = osc.Sender()
	sender:setup("127.0.0.1", 9990)
	if sender:getPort() ~= 9990 then
		status = "could not open OSC sender"
		statusTime = now()
		sender = nil
	end

	local x0, y0 = 70, 170
	local cw, ch = 250, 140
	local gx, gy = 30, 25
	for i, sketch in ipairs(sketches) do
		local col = (i - 1) % 3
		local row = math.floor((i - 1) / 3)
		cards[i] = {
			x = x0 + col * (cw + gx),
			y = y0 + row * (ch + gy),
			w = cw,
			h = ch,
			sketch = sketch,
		}
	end

	print("launcher: " .. #sketches .. " sketches")
	print("launcher: examples = " .. EXAMPLES)
	print("launcher: OSC listener on 9990")
end

function update()
	if status ~= "" and (now() - statusTime) > 4.0 then
		status = ""
	end
end

function draw()
	of.background(18, 18, 20)

	hoverIndex = 0
	for i, card in ipairs(cards) do
		if mouseX >= card.x and mouseX <= card.x + card.w and
		   mouseY >= card.y and mouseY <= card.y + card.h then
			hoverIndex = i
		end
	end

	of.setColor(255, 170, 50)
	of.drawRectangle(70, 58, 36, 4)
	of.setColor(240, 240, 245)
	drawText(fontTitle, "loaf launcher", 70, 108)
	of.setColor(160, 160, 170)
	drawText(fontSmall, "click a sketch  |  press 1-6  |  drag another .lua onto the window", 70, 134)

	for i, card in ipairs(cards) do
		drawCard(card, i)
	end

	of.setColor(110, 110, 120)
	drawText(fontSmall, "examples: " .. EXAMPLES, 70, 540)
	drawText(fontSmall, "CMD+R reload  |  CMD+F fullscreen  |  ESC quit  |  drag launcher.lua again to come back", 70, 560)

	if status ~= "" then
		of.setColor(255, 120, 90)
		drawText(fontSmall, status, 70, 584)
	end
end

function mousePressed(x, y, button)
	if button ~= 0 then return end
	for i, card in ipairs(cards) do
		if x >= card.x and x <= card.x + card.w and y >= card.y and y <= card.y + card.h then
			loadSketch(i)
			return
		end
	end
end

function keyPressed(key)
	if key >= 49 and key <= 54 then
		loadSketch(key - 48)
	end
end
