# 06 第三批：落盘路径复刻基准 + Envelope 数据可视化

Status: resolved

## 任务（用户裁决 2026-10-20）

- **SqliteSampleBlock**（拆不开判定）：复刻等价写/读路径——sqlite3 3.45 amalgamation（拉取）+ 相同 PRAGMA（64KB 页/WAL+NORMAL / OFF+OFF）/ 相同表结构与 summary 布局 / 相同"每块单条 INSERT bind_blob"协议。
- **Envelope**（拆不开判定，用户方案）：Python 复刻 `GetValues` 插值语义，导出数据并绘图。

## 交付

- `study_audacity/benchmarks/bench_sqlite_block/`（等价复刻，已标注）
- `study_audacity/benchmarks/plot_envelope.py` → `docs/img/envelope_interpolation.png` + `benchmarks/output/envelope_values.csv`

## 结果摘要

- Sqlite 写：已保存模式（WAL+NORMAL）856–923 MB/s（4.3–4.7 ns/样本，含 summary 计算）；临时/未保存模式 1117–1201 MB/s；**WAL 使写期间磁盘瞬时占用 ≈1.26×**
- Sqlite 读：2.5–2.7 ns/样本（≈1.5 GB/s）；读回与写入逐位一致（无压缩直存证实）
- Envelope 可视化：三种演示包络的线性域/dB 域插值最大差异 0.006–0.023 增益；dB 域每样本双 `pow(10,·)` 的成本即来源于此语义

## 验收标准

- [x] 编译/运行通过，验证 PASS
- [x] "等价复刻/可视化，非真实代码"在文档与 README 中明确标注
- [x] 03 文档 §1/§9 已并入结果

## Comments

- 2026-10-20 建立（用户裁决）；同日完成。
