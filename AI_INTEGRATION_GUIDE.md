# 给内网 AI 的接入任务书

把本文件与整个 OpenFTDS 源码目录一起提供给内网 AI。不要只复制本文件，因为 AI 需要读取
`include/`、`src/`、`tests/` 和 `docs/` 才能完成可靠接入。

## 可直接复制的提示词

```text
你正在给一个 C/C++ 项目接入本目录中的 OpenFTDS。请先完整阅读 README.md、
OFFLINE_DELIVERY.md、docs/compatibility.md、docs/architecture.md 和
docs/format.md，然后检查目标项目的构建系统与所有 FTDS 调用点。

目标：在不改变业务语义的前提下，用 OpenFTDS 收集固定事件 ID 的调用次数、错误数和
时延分布，输出 data/stat/*.ios，并能由 tools/openftds-report 离线分析。

执行要求：
1. 先列出目标项目中 ftds.h、libftdsclient、initFtdsClient、FTDS_DELAY_START、
   FTDS_DELAY_END、事件 ID 定义和封装宏的全部引用，形成迁移清单。
2. 优先采用最小改动兼容接入：把 OpenFTDS/include 加入 include path，并把本项目编译
   产生的 libftdsclient.a 替换原链接项；如果存在签名冲突，再改用原生 openftds API。
3. 保留原事件 ID、事件名、返回码语义。非零 status 计为错误，不要把耗时或返回码互换。
4. 对公开 FTDS 常见模式“START 的最后参数为 NULL，END 的倒数第二个参数指向已计算的
   纳秒耗时”保持原调用不变；兼容层已经支持这一模式。
5. 在进程启动后调用一次 initFtdsClient() 或 openftds_init()；在可控退出路径调用
   openftds_shutdown()。异常退出最多损失尚未 flush 的当前周期。
6. 输出目录必须可写。生产配置显式指定绝对路径；不要依赖当前工作目录。
7. 增加至少一个单线程、一个多线程、一个非零返回码、一个进程退出 flush 测试。
8. 编译时启用 -Wall -Wextra；运行现有测试，并用 openftds-report 检查 count、errors、
   avg、p99 是否符合预期。
9. 不要修改 OpenFTDS 的 schema 1 字段含义。需要扩展时增加可选字段，并保证旧读取器可
   忽略它们。
10. 最终输出：修改文件清单、构建命令、测试证据、事件 ID 清单、部署目录与回滚方法。

禁止事项：不要在信号处理函数中调用 flush；不要在 fork 后同时沿用父进程后台线程；
不要让同一进程内同一 event_id 表示不同事件；不要同时链接 libopenftds 和
libftdsclient 两份实现。
```

## 内网 AI 应优先选择的接入方式

### 方式 A：已有 FTDS 调用，最少改动

- 头文件搜索路径加入 `OpenFTDS/include`；顶层 `ftds.h` 会转到兼容层。
- 编译本项目后链接 `libftdsclient.a`，原有 `-lftdsclient` 通常无需修改。
- 保留 `FTDS_DELAY_START/END`、`initFtdsClient()` 和事件 ID。
- 用测试确认目标项目传给 `END` 的指针确实是纳秒耗时。

### 方式 B：新代码或允许改造

- C 代码包含 `<openftds/openftds.h>`，使用 `OPENFTDS_DELAY_START/END`。
- C++ 代码包含 `<openftds/openftds.hpp>`，优先使用 `OPENFTDS_SCOPED_DELAY`。
- 直接链接 `libopenftds.a`。

### 方式 C：已有自己的时钟与封装

- 保留现有计时逻辑，在结束处调用 `openftds_record()`。
- `duration_ns` 必须是纳秒；`status == 0` 表示成功。

## AI 接入后的验收条件

- 目标项目无新增编译告警或符号冲突。
- 并发压力下 `count` 等于实际完成次数，`errors` 等于非零返回次数。
- `.ios` 每行能被 `python3 -m json.tool` 或标准 JSON 解析器解析。
- `tools/openftds-report <输出根目录>` 能给出目标事件。
- 文件轮转、目录权限和磁盘上限符合部署环境要求。
- 删除 OpenFTDS 链接项及埋点改动后，业务代码可按回滚方案恢复。
