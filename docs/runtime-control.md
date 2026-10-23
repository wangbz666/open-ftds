# 运行期开关、采样与慢 I/O

## API

```c
openftds_set_enabled(0);               /* 新 START/record 立即停止 */
openftds_set_enabled(1);               /* 恢复 */
openftds_set_min_level(OPENFTDS_LEVEL_HIGH);
openftds_set_sample_rate(100);          /* 每线程约 1/100 */

openftds_status_t status;
openftds_get_status(&status);
```

已开始且获得非零 token 的操作在关闭后仍会完成记录，避免半条统计。采样在 START 时决定；
直接调用 `openftds_record()` 时在该调用处决定。`count` 是实际采样数，不自动乘以采样率。

初始化前可设置：

- `OPENFTDS_ENABLED=0|off|false|no`；
- `OPENFTDS_MIN_LEVEL=0|1|2`；
- `OPENFTDS_SAMPLE_RATE=N`；
- `OPENFTDS_DIR=/absolute/path`。

## 慢 I/O

初始化配置 `slow_threshold_ns` 后，每个事件额外输出 `slow` 和
`slow_threshold_ns`。`openftds-report` 显示 `SLOW%`，并支持 `--sort slow`。

## 信号触发

库函数不是 async-signal-safe。需要用信号切换时，handler 只设置
`volatile sig_atomic_t` 标志，由业务线程或管理线程观察标志后调用上述 API 或
`openftds_flush()`。不要在 handler 内直接 flush、加锁或写 JSON。

## 当前边界

OpenFTDS 仍是区间聚合工具，不保存每个 I/O 的完整跨模块调用链。真正的 per-I/O TraceMode
会显著改变存储量和热路径开销，应作为单独可选模块设计，不能用聚合字段冒充。
