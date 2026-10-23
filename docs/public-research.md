# 公开资料与设计依据

检索到的华为鲲鹏 Global Cache Adaptor 开源项目包含 FTDS 调用点，但仓库没有
`ftds.h` 或 `libftdsclient` 的实现。公开代码展示了以下接口形态：

- `#include "ftds.h"`；
- `initFtdsClient()`；
- `FTDS_DELAY_START(name, module, id, STAT_MODE_DELAY, value)`；
- `FTDS_DELAY_END(name, module, id, STAT_MODE_DELAY, value, status)`；
- 事件 ID 中编码 trace level；
- 构建时链接 `-lftdsclient`。

来源：

- [Global Cache Adaptor 仓库](https://github.com/kunpengcompute/global-cache-adaptor)
- [CephProxyFtds.h 公开调用封装](https://github.com/kunpengcompute/global-cache-adaptor/blob/master/src/ceph_proxy/CephProxyFtds.h)
- [项目 CMakeLists.txt 中的链接项](https://github.com/kunpengcompute/global-cache-adaptor/blob/master/CMakeLists.txt)
- [Perfetto Track Events](https://perfetto.dev/docs/instrumentation/track-events)
- [LTTng-UST manual](https://lttng.org/man/3/lttng-ust/)
- [OpenTelemetry C++](https://opentelemetry.io/docs/languages/cpp/)

Perfetto、LTTng 和 OpenTelemetry 都能覆盖部分需求，但它们的事件模型、部署链路或输出
格式与这种固定事件 ID、周期聚合、离线排行的用法不同。OpenFTDS 因此选择无第三方运行
时依赖的独立实现，并保留公开调用形态。
