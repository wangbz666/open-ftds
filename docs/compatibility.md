# FTDS 源码兼容说明

兼容层依据公开项目中可见的调用方式实现，目标是让已有源码以较少改动迁移。它提供：

- 顶层 `<ftds.h>`；
- `initFtdsClient()`；
- `FTDS_DELAY_START` / `FTDS_DELAY_END`；
- `TRACE_LEVEL_MODULE_NORMAL` / `TRACE_LEVEL_MODULE_HIGH`；
- `STAT_MODE_DELAY`；
- `libftdsclient.a` 兼容库名。

## 已支持的两种 END 语义

### 公开项目中常见的预计算耗时

```c
uint64_t ts = monotonic_now_ns();
FTDS_DELAY_START("READ", NULL, encoded_id, STAT_MODE_DELAY, NULL);
int ret = read_data();
uint64_t delay = monotonic_now_ns() - ts;
FTDS_DELAY_END("READ", NULL, encoded_id, STAT_MODE_DELAY, &delay,
               ret == 0 ? 0 : -1);
```

`END` 的倒数第二个参数指向纳秒耗时，OpenFTDS 直接聚合该值。

### 由兼容层计时

```c
openftds_token_t token;
FTDS_DELAY_START("READ", "cache", 447, STAT_MODE_DELAY, &token);
int ret = read_data();
FTDS_DELAY_END("READ", "cache", 447, STAT_MODE_DELAY, &token, ret);
```

若 `START` 和 `END` 都传同一 token，OpenFTDS 使用自身单调时钟计算耗时。两端都传 `NULL`
也可用，兼容层使用线程局部栈按事件 ID 配对，最大嵌套深度为 64。

## 事件级别编码

公开调用会把级别放在事件 ID 的最高 8 位。兼容层在最高字节为 0、1、2 时分别映射为
normal、high、critical，同时保留完整事件 ID 写入数据文件。未按此编码的普通事件 ID 默认
为 normal。

## 不是 ABI 替换的部分

源码宏和静态库链接名兼容不等于厂商二进制 ABI 完全等价。若目标程序在不重新编译的情况
下动态加载某个既有 `.so`、依赖未公开符号或读取某种专有 `.ios/.nmp`，需要另行根据实际
符号表和样本适配。本项目的数据格式定义在 `docs/format.md`，是可审计的 JSON Lines。

## 迁移清单

```bash
rg -n 'ftds\.h|ftdsclient|initFtdsClient|FTDS_DELAY_|TRACE_LEVEL_|STAT_MODE_' .
```

逐个确认：事件 ID 是否唯一、耗时单位是否为纳秒、返回码是否以 0 表示成功、START/END
是否可能跨线程。线程局部配对要求同一事件的 START 与 END 在同一线程；预计算耗时模式不
依赖内部配对结果来计算耗时，但仍建议成对调用。
