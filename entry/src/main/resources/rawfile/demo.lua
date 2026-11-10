-- loaf on HarmonyOS PC · built-in demo
-- edit this file in the app sandbox to see live reload in action

local t = 0
local trail = {}
local hue = 0
local frames = 0
local fps = 0
local fpsTime = 0

function setup()
	of.setWindowTitle("loaf · HarmonyOS PC")
	of.setCircleResolution(72)
	of.background(12, 14, 20)
	loaf.setListenPort(9990)
	loaf.startListening()
	print("loaf demo started")
end

function update()
	t = t + math.min(of.getLastFrameTime(), 0.05)
	frames = frames + 1
	fpsTime = fpsTime + of.getLastFrameTime()
	if fpsTime >= 0.5 then
		fps = math.floor(frames / fpsTime)
		frames = 0
		fpsTime = 0
	end

	table.insert(trail, {x = of.getMouseX(), y = of.getMouseY()})
	if #trail > 48 then
		table.remove(trail, 1)
	end
end

function draw()
	of.background(12, 14, 20)
	local w, h = of.getWidth(), of.getHeight()

	-- rotating ring of squares
	of.pushMatrix()
	of.translate(w * 0.5, h * 0.5)
	of.rotate(t * 0.35)
	for i = 1, 14 do
		of.pushMatrix()
		of.rotate(i * of.TWO_PI / 14)
		of.translate(190 + 26 * math.sin(t * 2 + i), 0)
		local r = 110 + 40 * math.sin(t + i * 0.4)
		of.setColor(r, 80 + hue % 60, 210 - i * 6, 220)
		of.drawRectangle(-16, -16, 32, 32)
		of.popMatrix()
	end
	of.popMatrix()

	-- pulsing core
	local radius = 72 + 14 * math.sin(t * 2.4)
	of.setColor(90, 210, 255, 235)
	of.drawCircle(w * 0.5, h * 0.5, radius)
	of.noFill()
	of.setColor(250, 200, 90, 230)
	of.setLineWidth(3)
	of.drawCircle(w * 0.5, h * 0.5, radius + 46)
	of.fill()

	-- sine ribbon
	of.setColor(90, 230, 180, 210)
	of.noFill()
	of.setLineWidth(3)
	of.beginShape()
	for x = 0, w, 6 do
		of.vertex(x, h * 0.78 + math.sin(x * 0.009 + t * 1.8) * 46)
	end
	of.endShape(false)
	of.fill()

	-- mouse trail
	for i = 1, #trail do
		local p = trail[i]
		local a = i / #trail
		of.setColor(120 + 100 * a, 90, 240, 190 * a)
		of.drawCircle(p.x, p.y, 3 + 13 * a)
	end

	-- labels
	of.setColor(240, 242, 250)
	of.drawString("loaf", 52, 96, 40)
	of.setColor(150, 200, 230)
	of.drawString("lua · osc · openFrameworks  →  HarmonyOS PC", 54, 128, 16)
	of.setColor(140, 150, 170)
	of.drawString(string.format("%d x %d   %d fps   osc :%d", w, h, fps, loaf.getListenPort()), 54, 156, 14)
end

function oscReceived(message)
	hue = (hue + 24) % 255
	print("osc " .. tostring(message))
end

function mousePressed(x, y, button)
	of.setColor(255, 120, 160)
end

function keyPressed(key)
	if key == 27 then -- escape
		loaf.clearScript()
	end
end
