## ![skynet logo](https://github.com/cloudwu/skynet/wiki/image/skynet_metro.jpg)

Skynet is a multi-user Lua framework supporting the actor model, often used in games.

[It is heavily used in the Chinese game industry](https://github.com/cloudwu/skynet/wiki/Uses), but is also now spreading to other industries, and to English-centric developers. To visit related sites, visit the Chinese pages using something like Google or Deepl translate.

The community is friendly and almost all contributors can speak English, so English speakers are welcome to ask questions in [Discussion](https://github.com/cloudwu/skynet/discussions), or submit issues in English.

# 持续同步skynet版本
目前版本 [e0bc6fe2d6ea05193f75a1467ecb7d9de748b23a](https://github.com/cloudwu/skynet/commit/e0bc6fe2d6ea05193f75a1467ecb7d9de748b23a)

# skynet_fly使用的skynet维护版本，对skynet做了一些优化改动

1. 增加快进时间debug调用。
2. 修改debug_console使用协议从http0.9升级为http1.1。
3. 优化mongoAPI代码风格，统一成下划线。
4. gate forward 命令断言检查改为返回true or false。
5. skynet.lua 去掉对`coroutine.create`函数的局部引用。 `local coroutine_create = coroutine.create`，因为不去掉`luaPanda.lua`重写`coroutine.create`无法起效。
6. 增加了服务录像功能。
7. 兼容windows的一些适配修改。
