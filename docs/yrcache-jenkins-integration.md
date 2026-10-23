# YRCache / Jenkins 接入方案

## 已核实的流水线约束

Jenkins `yrcache-venv` 最近成功构建日志显示：

- 远端 workspace 每次构建前清空；
- Python 3.12，`setup.py bdist_wheel`，PyTorch `BuildExtension` 与 Ninja；
- `.cc` 使用 `c++ -std=c++20`，CUDA 文件由 nvcc 处理；
- 最终由 `g++` 链接 `yrcache/c_ops...so`；
- 构建通过临时 Git checkout 复制到远端构建目录。

因此，OpenFTDS 源码应提交到 YRCache Git 的 `third_party/open-ftds/`，不能把预编译 `.a`
留在 Jenkins workspace，也不能把 `openftds.c` 直接加入 CUDAExtension 的 `sources`。

## 推荐目录

```text
YRCache/
├── setup.py
├── csrc/
└── third_party/
    └── open-ftds/       # 本仓库完整源码，建议 git subtree 或直接 vendor
```

首次接入建议直接 vendor 并记录上游 tag/commit，减少内网构建对 Git submodule 认证的依赖。

## setup.py / torch BuildExtension

把以下逻辑合并到 YRCache 已有的 `build_ext` 中；如果已有自定义类，不要重复注册
`cmdclass`，而是在其 `build_extensions()` 开头构建并注入静态库。

```python
from pathlib import Path
import sys
from torch.utils.cpp_extension import BuildExtension

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "third_party/open-ftds/integration/yrcache"))
from openftds_build import build_openftds


class YRCacheBuildExt(BuildExtension):
    def build_extensions(self):
        archive = build_openftds(self.build_temp)
        for ext in self.extensions:
            if ext.name == "yrcache.c_ops":
                ext.extra_objects = [*getattr(ext, "extra_objects", []), archive]
                ext.extra_link_args = [*getattr(ext, "extra_link_args", []), "-pthread"]
        super().build_extensions()


# setup(..., cmdclass={"build_ext": YRCacheBuildExt})
```

`integration/yrcache/openftds_build.py` 使用 `CC` 以 C11、`-fPIC` 编译，再由 `AR` 生成
静态库。它不依赖 torch，不会调用 nvcc，也不会改变 `NVCC_COMPUTE_VERSION`。

## CMake 方案

如果 YRCache 后续把扩展构建迁移到 CMake，在顶层项目启用 C 语言：

```cmake
project(YRCache LANGUAGES C CXX CUDA)
include(third_party/open-ftds/integration/yrcache/OpenFTDS.cmake)
target_link_libraries(c_ops PRIVATE yrcache_openftds)
```

## vLLM 多进程

0.2.1 起，OpenFTDS 通过 `pthread_atfork` 在子进程中仅设置一个安全标志，普通打点路径不再
调用 `getpid()`。子进程首次使用时才执行恢复；若平台不能注册 atfork 回调，则自动退回 PID
比较。恢复过程会：

1. 关闭继承的父进程输出文件描述符；
2. 丢弃父进程未 flush 的聚合副本，避免重复统计；
3. 重建互斥量、条件变量和事件表；
4. 沿用父进程配置，以子进程 PID 创建自己的 `.ios` 和日志；
5. 懒启动新的后台 flush 线程。

仍建议在 worker fork 后、创建大量线程前调用一次 `openftds_init(NULL)` 或做一次状态查询，
使重建时点明确。不要从 signal handler 直接调用库 API。

## Jenkins 验收步骤

1. 只提交 OpenFTDS 源码、接入代码和测试，不提交本机构建产物。
2. 在外网分支先执行 `python3 setup.py bdist_wheel`。
3. 从日志确认 `openftds.c` 命令包含 `-std=c11 -fPIC`，且命令是 `cc/gcc` 而非 nvcc。
4. 从最终 `g++ -shared` 命令确认包含 `libopenftds.a` 与 `-pthread`。
5. 安装 wheel 后运行父进程初始化、fork 两个 worker、分别打点并退出的测试。
6. 检查输出目录包含不同 PID 的文件，报告工具能合并读取。

不要直接访问 Jenkins 的 `/build` URL 做“查看”；该 URL 会触发新任务。先使用 job 页面或
`api/json` 只读接口核验状态，只有明确要验证已提交代码时再由有权限的人发起构建。
