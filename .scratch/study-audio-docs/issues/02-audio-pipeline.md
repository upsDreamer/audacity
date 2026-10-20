# 02 链路文档：音频链路联通说明

Status: resolved

## 任务

产出 `study_audacity/docs/02-audio-pipeline.md`：mermaid 总图 + 录制链/播放链/离线渲染链三章 + 每环节输入输出契约。主线为 au3 核心部件（AudioIO/PlaybackSchedule/RingBuffer、au3-mixer 的 MixerSource/EffectStage/Downmix、au3-audio-graph、WaveTrack→Sequence→SampleBlock→SqliteSampleBlock、RealtimeEffect*、MixAndRender、Resample），`src/` 新架构层（playback/record/au3audio/effects_base、au3wrap）只在挂接点出现。

## 验收标准

- [ ] 一张 mermaid 总图串起三条链
- [ ] 三条链各有独立章节，标明线程边界（音频回调线程 vs 主线程）与数据流向
- [ ] 每个环节写明输入输出契约（缓冲形状、采样格式、采样率转换发生点）
- [ ] 实时效果在链路中的插入位置（EffectStage）单独讲清
- [ ] 导入导出、src/ 编排层只讲挂接位置

## Comments

- 2026-10-20 建立。
- 2026-10-20 完成：`study_audacity/docs/02-audio-pipeline.md`。mermaid 总图 + 播放/录制/离线渲染三链调用流（函数名+文件:行）+ 环节契约表 + 格式转换点汇总 + 新架构挂接点三节。已标注线程边界与本 fork 的播放位置推进改动（16ms 定时器）。
