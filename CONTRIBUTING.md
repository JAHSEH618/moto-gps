> **语言 / Language:** 中文 · [English](CONTRIBUTING.en.md)

# 参与开发

欢迎提交非商业用途的 bug 复现、文档修正和代码改进。提交贡献前请阅读 LICENSE.md
与 THIRD_PARTY_NOTICES.md；原创贡献须可按本项目许可分发，第三方内容必须说明来源。

- 先说明目标板型号、iOS / ESP-IDF 版本、复现步骤和实际结果。
- 变更共享协议时，同步更新 C++ / iOS 两端与 golden fixture 测试。
- 路线/位置 bug 请使用公共 POI 或匿名化数据，不要公开家庭位置、完整出行轨迹。
- 不提交 Key、Apple 签名证书、设备标识、服务凭证或未经授权的地图缓存。
- PR 中列出实际执行的测试；编译通过不能代替蓝牙、路线正确性或实车验收。
- 地图几何不得用随机线条或虚构建筑填补真实数据缺失。

## 合并门

本仓库的 `origin` 没有启用 GitHub Actions：`gh workflow list -R JAHSEH618/moto-gps` 返回空，
上游最近的运行都是 0 秒的 `action_required`。所以不要在 PR 里等绿灯 —— `scripts/verify.sh`
才是实际执行的门（todo 日志、native ctest、Swift 包测试、iOS App 构建）；
`scripts/verify.sh --install-hook` 会把它装成 pre-push 钩子。`.github/workflows/checks.yml`
只在 Actions 被启用之后才有意义。

安全问题请勿在公开 Issue 粘贴密钥或可用于攻击现有服务的细节。
本仓库是开发快照，暂不承诺商业维护、响应期限或稳定性。
