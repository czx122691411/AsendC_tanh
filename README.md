# Ascend C TanhCustom 学习实践

围绕 Ascend C 中级认证 TanhCustom 题目，按最小可运行闭环学习数据搬运、Tiling、核函数、调试和性能分析。

## 当前状态

- 已同步本地题目模板、配套 aclnn 测试和技术资料。
- 高阶 `AscendC::Tanh` 已落地 kernel（带 `calCount` 的无临时缓冲重载），
  ascend910b 目标编译通过；CPU 域数值回归 7 形状全 PASS，最大误差 2.43e-4 < 1e-3
  （`experiments/tanh_numeric/`，见知识卡 08）。
- **真机未验证**：算子包未安装、`AclNNInvocation` 未跑、NPU 精度与性能未知。
- 模板当前编译目标为 `ascend910b`（A2），这不等同于已确认考核后台设备。

## 目录

| 目录 | 内容 |
| --- | --- |
| `TanhCustom/` | 算子工程，保留本地模板代码 |
| `AclNNInvocation/` | 配套原始调用与精度测试代码 |
| `experiments/` | CPU 域学习实验 harness，不进入考试提交 |
| `knowledge-cards/` | 已验证知识卡与进度记录 |
| `docs/` | 题目摘要、技术资料和学习约定 |

已排除本地旧构建产物和包含账号信息的保存网页。题目工程中的原有版权声明予以保留；本仓库没有为第三方源码额外授予许可证。

## 在云端开始

```bash
git clone https://github.com/czx122691411/AsendC_tanh.git
cd AsendC_tanh
```

先读取 [学习约定](docs/学习约定.md)，再检查环境和源码 TODO。由于模板尚未补齐，目前不应期待直接构建通过。

完成代码后，按以下流程在具备配套 CANN 和 NPU 的 Linux 环境验证：

1. 加载实际 CANN 9.0.0 环境，检查 SoC 型号。
2. 核对 `TanhCustom/CMakePresets.json` 中芯片和 Toolkit 路径；当前路径为 `/usr/local/Ascend/cann-9.0.0`，需按实例调整。
3. 在 `TanhCustom/` 中运行 `bash build.sh`，安装实际生成的算子包，并按日志配置环境。
4. 在 `AclNNInvocation/` 中运行 `bash run.sh`。

没有 NPU 时可先研究 CPU 域调试入口；原始 aclnn 测试不能直接视为纯 CPU 测试。

## 资料与提交

- [题目摘要](docs/题目摘要.md)
- [技术资料与云端开发指南](docs/技术资料与云端开发指南.md)

GitHub 仓库用于学习和版本管理。正式考试提交时仅按题面打包 `TanhCustom/`，不包含测试工程或学习文档。
