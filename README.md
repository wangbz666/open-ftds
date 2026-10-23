# OpenFTDS

OpenFTDS 是一个面向 C/C++ 存储与高性能服务的轻量级时延埋点工具。它采用与公开
FTDS 调用方式相近的开始/结束模型，按固定事件 ID 聚合调用次数、错误数、总时延、
最小/最大时延和近似 P50/P95/P99，并把周期数据写入可轮转的 `.ios` 文件。

它不依赖守护进程、数据库或第三方运行时库，适合部署在离线、受限或对依赖敏感的环境中。
兼容层覆盖公开项目中可见的 `ftds.h`、`initFtdsClient`、`FTDS_DELAY_START/END`
和 `-lftdsclient` 使用方式。

## 能力

- C11 API 和 C++ RAII 封装；
- `START/END` 风格时延埋点，固定 `event_id`、模块和级别；
- 多线程安全的调用、错误和时延直方图聚合；
- fork 后按 PID 自动重建，适合 vLLM worker 多进程；
- 运行期开关、级别过滤、1/N 采样和慢 I/O 计数；
- 后台周期落盘，默认单文件 20 MiB、保留 50 个文件；
- FTDS 风格目录：`data/stat/*.ios` 与 `run/client/*.log`；
- `.ios` 使用 JSON Lines，便于审计、脚本处理与长期维护；
- `openftds-report` 离线聚合并按 P99、平均值、最大值、次数或错误数排序；
- 顶层 `ftds.h`、`libftdsclient.a` 与公开调用名兼容层。

## 快速开始

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/openftds-example
./tools/openftds-report ./demo-output
```

只有系统编译器和 `make` 时：

```bash
make test
make example
./build/make/openftds-report ./demo-output
```

应用接入：

```c
#include <openftds/openftds.h>

openftds_config_t config = {
    .directory = "/var/log/my-service/ftds",
    .flush_interval_ms = 10000,
    .sample_rate = 1,
    .slow_threshold_ns = 100000000,
};
openftds_init(&config);

openftds_token_t ts;
OPENFTDS_DELAY_START(OPENFTDS_LEVEL_HIGH, ts);
int rc = do_io();
OPENFTDS_DELAY_END("READ_TOTAL", "cache", 447,
                   OPENFTDS_LEVEL_HIGH, ts, rc);

openftds_shutdown();
```

运行中可调用 `openftds_set_enabled()`、`openftds_set_min_level()` 和
`openftds_set_sample_rate()`，或通过 `openftds_get_status()` 查询当前进程状态。

不调用 `openftds_init()` 也可以：第一次埋点会自动初始化，输出根目录取环境变量
`OPENFTDS_DIR`，未设置时使用当前目录下的 `openftds/`。

## C++ 用法

```cpp
#include <openftds/openftds.hpp>

void read_object() {
    OPENFTDS_SCOPED_DELAY("READ_OBJECT", "object", 1001,
                          OPENFTDS_LEVEL_HIGH);
    // 作用域结束时自动记录时延。
}
```

需要记录返回状态时，直接使用 `openftds::ScopedDelay` 并调用 `set_status()`。

## 公开调用名兼容层

`<ftds.h>` 和 `<openftds/ftds_compat.h>` 提供 `initFtdsClient`、
`FTDS_DELAY_START` 和 `FTDS_DELAY_END`。构建同时生成 `libftdsclient.a`，方便保留已有
`-lftdsclient` 链接项。

```c
#include <openftds/ftds_compat.h>

openftds_token_t token;
FTDS_DELAY_START("READ", "cache", 447, STAT_MODE_DELAY, &token);
do_io();
FTDS_DELAY_END("READ", "cache", 447, STAT_MODE_DELAY, &token, 0);
```

兼容层也支持公开代码中常见的模式：`START` 最后一个参数为 `NULL`，`END` 的倒数第二个
参数指向已计算的纳秒耗时。具体见 [FTDS 源码兼容说明](docs/compatibility.md)。

## 输出

```text
<root>/
├── data/stat/openftds-<host>-<pid>-<start>-000000.ios
└── run/client/openftds-<pid>.log
```

每行 `.ios` 都是一个独立 JSON 对象：

```json
{"schema":1,"captured_at_ns":1770000000000000000,"pid":1234,"event_id":447,"module":"cache","name":"READ_TOTAL","level":1,"count":1000,"errors":2,"slow":7,"slow_threshold_ns":100000000,"total_ns":250000000,"min_ns":81000,"max_ns":920000,"hist_us_log2":[0,0,0,0,0,3,120,700,170,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}
```

百分位数来自以微秒为单位的 log2 直方图，是桶上界近似值；最小值、最大值和平均值使用
纳秒原始累计值计算。

```bash
openftds-report /var/log/my-service/ftds --sort p99 --top 30
openftds-report /var/log/my-service/ftds --json
```

## 与现有开源工具的关系

| 工具 | 优点 | 与本项目的差异 |
|---|---|---|
| Perfetto | 丰富的时间线、flow、counter 与可视化界面 | C++ SDK 较大，输出和部署模型不同 |
| LTTng-UST | Linux 用户态/内核统一跟踪，开销低 | 依赖 LTTng 工具链与 CTF 分析流程 |
| OpenTelemetry C++ | 跨进程、跨服务分布式追踪 | 更适合服务可观测性，依赖和导出链路更重 |
| Tracy | 实时交互式 profiler | 面向开发期分析，不是 FTDS 风格周期统计文件 |

如果已经部署这些系统，应优先评估直接使用它们。OpenFTDS 的定位是：依赖最少、部署简单、
以固定事件 ID 分析存储 I/O 模块时延。

## 设计说明与限制

- 一个进程最多注册 4096 个事件 ID；同一进程内同一 ID 应始终表示同一事件。
- 聚合记录而非逐请求原始 trace，因此存储开销稳定，但不能重建单个请求的完整调用链。
- 进程正常退出会自动 flush；`SIGKILL` 或断电可能损失当前尚未落盘的一个周期。
- 高频热点事件使用每事件短自旋锁保证统计快照一致；如果单一事件被极大量线程同时更新，
  建议为不同路径分配不同事件 ID。
- 当前 `.ios` 是公开的 OpenFTDS JSONL 格式，不是任何厂商私有格式。

## 离线交付资料

- [离线交付与部署](OFFLINE_DELIVERY.md)
- [给内网 AI 的接入任务书](AI_INTEGRATION_GUIDE.md)
- [FTDS 源码兼容说明](docs/compatibility.md)
- [架构与并发模型](docs/architecture.md)
- [数据格式](docs/format.md)
- [公开资料与设计依据](docs/public-research.md)
- [运行期开关、采样与慢 I/O](docs/runtime-control.md)
- [YRCache/Jenkins 接入方案](docs/yrcache-jenkins-integration.md)

## 许可证

MIT License。
