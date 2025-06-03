local skynet = require "skynet"
local coroutine = coroutine
local xpcall = xpcall
local traceback = debug.traceback
local table = table
local assert = assert
local error = error

function skynet.queue()
	local current_thread
	local ref = 0
	local thread_queue = {}
	local trace_tag_check = {}

	local function xpcall_ret(tarce_tag, ok, ...)
		if tarce_tag then
			trace_tag_check[tarce_tag] = nil
		end
		ref = ref - 1
		if ref == 0 then
			current_thread = table.remove(thread_queue,1)
			if current_thread then
				skynet.wakeup(current_thread)
			end
		end
		assert(ok, (...))
		return ...
	end

	return function(f, ...)
		local thread = coroutine.running()
		if current_thread and current_thread ~= thread then
			local tarce_tag = skynet.get_lua_trace()
			if tarce_tag and trace_tag_check[tarce_tag] then --queue loop
				error(string.format("queue loop tarce_tag[%s] current_thread[%s] thread[%s]", tarce_tag, current_thread, thread))
			end
			table.insert(thread_queue, thread)
			skynet.wait()
			assert(ref == 0)	-- current_thread == thread
		end
		current_thread = thread
		local tarce_tag = skynet.get_lua_trace()
		if tarce_tag then
			trace_tag_check[tarce_tag] = current_thread
		end
		ref = ref + 1
		return xpcall_ret(tarce_tag, xpcall(f, traceback, ...))
	end
end

return skynet.queue
