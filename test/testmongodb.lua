local skynet = require "skynet"
local mongo = require "skynet.db.mongo"
local bson = require "bson"

local host, port, db_name, username, password = ...
if port then
	port = math.tointeger(port)
end

-- print(host, port, db_name, username, password)

local function _create_client()
	return mongo.client(
		{
			host = host, port = port,
			username = username, password = password,
			authdb = "admin",
		}
	)
end

function test_auth()
	local ok, err, ret
	local c = mongo.client(
		{
			host = host, port = port,
		}
	)
	local db = c[db_name]
	db:auth(username, password)

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	ok, err, ret = db.testcoll:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

function test_insert_without_index()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	ok, err, ret = db.testcoll:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 1});
	assert(ok and ret and ret.n == 1, err)
end

function test_insert_with_index()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	db.testcoll:ensure_index({test_key = 1}, {unique = true, name = "test_key_index"})

	ok, err, ret = db.testcoll:safe_insert({test_key = 1})
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 1})
	assert(ok == false and string.find(err, "duplicate key error"))
end

function test_find_and_remove()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	local cursor = db.testcoll:find()
	assert(cursor:has_next() == false)

	db.testcoll:ensure_index({test_key = 1}, {test_key2 = -1}, {unique = true, name = "test_index"})

	ok, err, ret = db.testcoll:safe_insert({test_key = 1, test_key2 = 1})
	assert(ok and ret and ret.n == 1, err)

	cursor = db.testcoll:find()
	assert(cursor:has_next() == true)
	local v = cursor:next()
	assert(v)
	assert(v.test_key == 1)

	ok, err, ret = db.testcoll:safe_insert({test_key = 1, test_key2 = 2})
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 2, test_key2 = 3})
	assert(ok and ret and ret.n == 1, err)

	ret = db.testcoll:find_one({test_key2 = 1})
	assert(ret and ret.test_key2 == 1, err)

	ret = db.testcoll:find({test_key2 = {['$gt'] = 0}}):sort({test_key = 1}, {test_key2 = -1}):skip(1):limit(1)
	assert(ret:count() == 3)
	assert(ret:count(true) == 1)
	if ret:has_next() then
		ret = ret:next()
	end
	assert(ret and ret.test_key2 == 1)

	db.testcoll:delete({test_key = 1})
	db.testcoll:delete({test_key = 2})

	ret = db.testcoll:find_one({test_key = 1})
	assert(ret == nil)
end

function test_run_command()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	ok, err, ret = db.testcoll:safe_insert({test_key = 1, test_key2 = 1})
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 1, test_key2 = 2})
	assert(ok and ret and ret.n == 1, err)

	ok, err, ret = db.testcoll:safe_insert({test_key = 2, test_key2 = 3})
	assert(ok and ret and ret.n == 1, err)

	local pipeline = {
		{
			["$group"] = {
				_id = mongo.null,
				test_key_total = { ["$sum"] = "$test_key"},
				test_key2_total = { ["$sum"] = "$test_key2" },
			}
		}
	}
	ret = db:run_command("aggregate", "testcoll", "pipeline", pipeline, "cursor", {})
	assert(ret and ret.cursor.firstBatch[1].test_key_total == 4)
	assert(ret and ret.cursor.firstBatch[1].test_key2_total == 6)
end

function test_expire_index()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop_index("*")
	db.testcoll:drop()

	db.testcoll:ensure_index({test_key = 1}, {unique = true, name = "test_key_index", expireAfterSeconds = 1, })
	db.testcoll:ensure_index({test_date = 1}, {expireAfterSeconds = 1, })

	ok, err, ret = db.testcoll:safe_insert({test_key = 1, test_date = bson.date(os.time())})
	assert(ok and ret and ret.n == 1, err)

	ret = db.testcoll:find_one({test_key = 1})
	assert(ret and ret.test_key == 1)

	for i = 1, 60 do
		skynet.sleep(100);
		print("check expire", i)
		ret = db.testcoll:find_one({test_key = 1})
		if ret == nil then
			return
		end
	end
	print("test expire index failed")
	assert(false, "test expire index failed");
end

local function test_safe_batch_insert()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop()
	
	local docs, length = {}, 10
	for i = 1, length do
		table.insert(docs, {test_key = i})
	end
	
	db.testcoll:safe_batch_insert(docs)

	local ret = db.testcoll:find()
	assert(length == ret:count(), "test safe batch insert failed")
end

local function test_safe_batch_delete()
	local ok, err, ret
	local c = _create_client()
	local db = c[db_name]

	db.testcoll:drop()

	local docs, length = {}, 10
	for i = 1, length do
		table.insert(docs, {test_key = i})
	end

	db.testcoll:safe_batch_insert(docs)

	docs = {}
	local del_num = 5
	for i = 1, del_num do
		table.insert(docs, {test_key = i})
	end

	db.testcoll:safe_batch_delete(docs)

	local ret = db.testcoll:find()
	assert((length - del_num) == ret:count(), "test safe batch delete failed")
end

skynet.start(function()
	if username then
		print("Test auth")
		test_auth()
	end
	print("Test insert without index")
	test_insert_without_index()
	print("Test insert index")
	test_insert_with_index()
	print("Test find and remove")
	test_find_and_remove()
	print("Test run_command")
	test_run_command()
	print("Test expire index")
	test_expire_index()
	print("test safe batch insert")
	test_safe_batch_insert()
	print("test safe batch delete")
	test_safe_batch_delete()
	print("mongodb test finish.");
end)
