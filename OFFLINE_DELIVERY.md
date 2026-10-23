# 离线交付与部署

本仓库不需要联网拉取依赖。运行库只依赖 C11、POSIX threads 和常见 POSIX 文件接口；
报告工具只依赖 Python 3 标准库。

解压后可查看 `SOURCE_VERSION` 获取版本号和生成该交付包的 Git 提交哈希。

## 1. 验证交付包

在压缩包同级目录执行：

```bash
shasum -a 256 -c open-ftds-0.1.0.sha256
tar -tzf open-ftds-0.1.0.tar.gz | head
```

若内网没有 `shasum`，可使用：

```bash
sha256sum -c open-ftds-0.1.0.sha256
```

## 2. 构建和自测

只有编译器、make 和 Python 3 时：

```bash
make clean
make test
make example
./tools/openftds-report ./demo-output
```

有 CMake 3.16+ 时：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Python 3 只用于报告和对应测试；C/C++ 运行库本身不依赖 Python。

## 3. 安装到临时根目录

建议先做非特权安装并查看文件：

```bash
make install DESTDIR="$PWD/stage" PREFIX=/usr/local
find stage -type f -print
```

会得到：

- `include/ftds.h`：现有 FTDS 源码的兼容入口；
- `include/openftds/*.h`：原生 C/C++ API；
- `lib/libopenftds.a`：原生库名；
- `lib/libftdsclient.a`：兼容原链接名；
- `bin/openftds-report`：离线报告工具。

不要在同一可执行文件里同时链接 `libopenftds.a` 和 `libftdsclient.a`。

## 4. 运行参数

推荐服务启动时显式配置：

```c
openftds_config_t cfg = {
    .directory = "/var/log/my-service/ftds",
    .rotate_bytes = 20u * 1024u * 1024u,
    .max_files = 50,
    .flush_interval_ms = 10000,
    .min_level = OPENFTDS_LEVEL_NORMAL,
    .fsync_on_flush = 0,
};
int rc = openftds_init(&cfg);
```

使用 `initFtdsClient()` 时，目录由 `OPENFTDS_DIR` 环境变量指定；未指定则使用
`./openftds`。服务账号必须拥有目录的创建与写入权限。

## 5. 上线前检查

- 为每个事件 ID 建立名称、模块、开始点、结束点、返回码含义的表。
- 同一进程内不要复用事件 ID。
- 根据 QPS、事件数与 flush 周期验证磁盘增量。
- 先在压测环境检查热点事件的开销与锁竞争。
- 确认日志采集系统不会误删 `.ios`，并设置磁盘告警。
- 正常退出路径调用 `openftds_shutdown()`；`SIGKILL` 无法保证最后周期落盘。

## 6. 回滚

兼容模式下，恢复原 `ftds.h` 搜索路径和原 `libftdsclient` 即可。原生 API 模式下，建议
把埋点封装在项目自己的宏后，回滚时将宏编译为空操作。
