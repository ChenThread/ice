local component = require("component")
local term = require("term")
local gpu = component.gpu
local computer = require("computer")
local tape = component.tape_drive

COLMAP = {}
local i
for i=0,15 do
	local w = i*255/15
	COLMAP[i] = (w<<16)|(w<<8)|w
	gpu.setPaletteColor(i, COLMAP[i])
end
for i=0,240-1 do
	local r = i%6
	local g = (i//6)%8
	local b = (i//(6*8))

	r = (r*255+2)//5
	g = (g*255+3)//7
	b = (b*255+2)//4

	COLMAP[i+16] = (r<<16)|(g<<8)|b
end

if tape and tape.isReady() then
	tape.stop()
	while tape.seek(-tape.getSize()) ~= 0 do
		os.sleep(0.05)
		tape.stop()
	end
	tape.stop()
end

local fname = ...
local fp = io.open(fname, "rb")

local W = fp:read(1):byte()
local H = fp:read(1):byte()
gpu.setResolution(W, H)
gpu.setBackground(0x000000)
gpu.setForeground(0xFFFFFF)
term.clear()

if sysnative then
	tlast = os.clock()
else
	os.sleep(0.05)
	if tape and tape.isReady() then
		tape.play()
		os.sleep(1.0) -- deal to sound latency
	end
	tlast = computer.uptime()
end

local delay_acc = 0.0
local function delay(d)
	assert(d >= 0.0)
	delay_acc = delay_acc + d
	local dquo = math.floor(delay_acc / 0.05) * 0.05
	delay_acc = delay_acc - dquo
	os.sleep(dquo)
end

while true do
	local s = fp:read(1)
	if s == "" or s == nil then
		break
	end
	local c = s:byte()

	if c == 0xFF then
		tnow = computer.uptime()
		tlast = tlast + 0.05
		while tnow < tlast do
			--delay(tlast-tnow)
			os.sleep(tlast-tnow)
			tnow = computer.uptime()
		end
	elseif c == 0x00 then
		local col = COLMAP[fp:read(1):byte()]
		gpu.setBackground(col)
	else
		local by = fp:read(1):byte() 
		local bx = fp:read(1):byte()
		local bw = fp:read(1):byte()
		local bh = c & 0x3F
		local type = c & 0x40
		if type == 0x40 then
			gpu.fill(bx, by, bw, bh, " ")
		else
			if bh < bw then
				for i = 0, bh-1 do
					gpu.set(bx, by + i, string.rep(" ", bw), false)
				end
			else
				for i = 0, bw - 1 do
					gpu.set(bx + i, by, string.rep(" ", bh), true)
				end
			end
		end	
	end
end

gpu.setBackground(0x000000)
gpu.setForeground(0xFFFFFF)

