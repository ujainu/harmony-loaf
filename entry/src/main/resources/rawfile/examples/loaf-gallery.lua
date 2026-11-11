-- loaf gallery: two modules
--   click the left card  -> slowly rotating tetrahedron
--   click the right card -> dancing girl image
--   click "返回" or press Esc to come back

local mode = "menu"
local t = 0
local mesh = nil
local girl = nil

local function findAsset(relative)
	local start = loaf.getStartDirectory()
	local cwd = loaf.getCurrentDirectory()
	local candidates = {
		start .. "/" .. relative,
		start .. "/examples/" .. relative,
		cwd .. "/" .. relative,
		cwd .. "/examples/" .. relative,
	}
	for _, path in ipairs(candidates) do
		if of.File(path):exists() then
			return path
		end
	end
	return candidates[1]
end

local cards = {
	{ x = 90,  y = 190, w = 500, h = 400, title = "旋转三角体", sub = "slow tetrahedron", mode = "tetra" },
	{ x = 630, y = 190, w = 500, h = 400, title = "跳舞的女孩", sub = "dancing girl", mode = "dance" },
}

local function inside(card, x, y)
	return x >= card.x and x <= card.x + card.w and y >= card.y and y <= card.y + card.h
end

local function backButton()
	return 24, 24, 120, 44
end

local function drawBack()
	local x, y, w, h = backButton()
	of.setColor(38, 42, 54)
	of.drawRectangle(x, y, w, h)
	of.setColor(220, 226, 240)
	of.drawString("< 返回", x + 16, y + 28, 16)
end

function setup()
	loaf.setResolution(1280, 800)
	of.setWindowTitle("loaf gallery")
	of.background(16, 18, 26)
	of.setFrameRate(60)

	local m = of.Mesh()
	local s = 120
	local verts = {
		{ 1, 1, 1 },
		{ 1, -1, -1 },
		{ -1, 1, -1 },
		{ -1, -1, 1 },
	}
	local colors = {
		{ 255, 96, 96 },
		{ 96, 200, 255 },
		{ 255, 200, 80 },
		{ 120, 230, 160 },
	}
	for i, v in ipairs(verts) do
		m:addVertex(v[1] * s, v[2] * s, v[3] * s)
		m:addColor(colors[i][1], colors[i][2], colors[i][3])
	end
	m:addTriangle(0, 1, 2)
	m:addTriangle(0, 3, 1)
	m:addTriangle(0, 2, 3)
	m:addTriangle(1, 3, 2)
	mesh = m

	girl = of.Image()
	local girlPath = findAsset("dance/dancing-girl.png")
	girl:load(girlPath)
	print("gallery: girl loaded " .. tostring(girl:isLoaded()) .. " " .. girlPath)
end

function update()
	t = t + of.getLastFrameTime()
end

function mousePressed(x, y, button)
	print(string.format("gallery: click %.1f,%.1f mode=%s", x, y, mode))
	if mode ~= "menu" then
		local bx, by, bw, bh = backButton()
		if x >= bx and x <= bx + bw and y >= by and y <= by + bh then
			mode = "menu"
			print("gallery: back to menu")
		end
		return
	end
	for _, card in ipairs(cards) do
		if inside(card, x, y) then
			mode = card.mode
			print("gallery: enter " .. mode)
			return
		end
	end
end

function keyPressed(key)
	if key == 27 and mode ~= "menu" then
		mode = "menu"
	end
end

local function drawMenu()
	local mx, my = of.getMouseX(), of.getMouseY()
	of.setColor(235, 240, 250)
	of.drawString("loaf gallery", 90, 120, 34)
	of.setColor(130, 145, 170)
	of.drawString("两个模块 · 点击进入", 90, 152, 16)

	for _, card in ipairs(cards) do
		local hover = inside(card, mx, my)
		of.setColor(hover and 34 or 26, hover and 40 or 30, hover and 54 or 42)
		of.drawRectangle(card.x, card.y, card.w, card.h)
		of.setColor(hover and 90 or 56, hover and 120 or 70, 220)
		of.drawRectangle(card.x, card.y, 6, card.h)

		if card.mode == "tetra" then
			local cx, cy = card.x + card.w * 0.5, card.y + 150
			of.setColor(255, 200, 80)
			of.drawTriangle(cx, cy - 80, cx - 70, cy + 60, cx + 70, cy + 60)
			of.setColor(96, 200, 255)
			of.drawTriangle(cx, cy - 80, cx + 70, cy + 60, cx + 20, cy + 20)
		else
			local cx, cy = card.x + card.w * 0.5, card.y + 150
			of.setColor(255, 140, 170)
			of.drawCircle(cx, cy, 62)
			of.setColor(255, 220, 200)
			of.drawCircle(cx, cy - 24, 30)
			of.setColor(240, 190, 90)
			of.drawCircle(cx - 20, cy + 40, 10)
			of.drawCircle(cx + 20, cy + 40, 10)
		end

		of.setColor(235, 240, 250)
		of.drawString(card.title, card.x + 28, card.y + 300, 26)
		of.setColor(150, 165, 190)
		of.drawString(card.sub, card.x + 28, card.y + 332, 15)
		of.setColor(hover and 255 or 140, hover and 210 or 160, hover and 90 or 190)
		of.drawString("click to open  >", card.x + 28, card.y + 368, 14)
	end
end

local function drawTetra()
	local w, h = of.getWidth(), of.getHeight()
	local camera = of.Camera()
	camera:setPosition(0, 90, 560)
	camera:lookAt(0, 0, 0)
	camera:begin()
	of.pushMatrix()
	of.rotateYDeg(t * 18)
	of.rotateXDeg(t * 7)
	mesh:draw()
	of.popMatrix()
	camera:endCamera()

	of.setColor(235, 240, 250)
	of.drawString("slow tetrahedron", 40, h - 60, 18)
	of.setColor(140, 155, 180)
	of.drawString(string.format("%d x %d", w, h), 40, h - 32, 13)
	drawBack()
end

local function drawDance()
	local w, h = of.getWidth(), of.getHeight()
	of.setColor(10, 8, 16)
	local breathe = 1 + 0.02 * math.sin(t * 1.4)
	if girl and girl:isLoaded() then
		local gw, gh = girl:getWidth(), girl:getHeight()
		local scale = math.min((h - 120) / gh, (w - 160) / gw) * breathe
		local dw, dh = gw * scale, gh * scale
		local dx = (w - dw) * 0.5
		local dy = (h - dh) * 0.5 + 10 * math.sin(t * 1.1)
		of.setColor(255, 255, 255)
		girl:draw(dx, dy, dw, dh)
	else
		of.setColor(240, 120, 150)
		of.drawString("image missing", 40, 120, 20)
	end
	of.setColor(235, 240, 250)
	of.drawString("dancing girl", 40, h - 44, 18)
	drawBack()
end

function draw()
	of.background(16, 18, 26)
	if mode == "menu" then
		drawMenu()
	elseif mode == "tetra" then
		drawTetra()
	elseif mode == "dance" then
		drawDance()
	end
end
