# 02 音频链路联通说明

> 研究文档。基线 master `be46fba74`。配套：[01-algorithms-catalog.md](./01-algorithms-catalog.md)（算法本体）、[03-performance.md](./03-performance.md)（开销评估）。

## 0. 总览

三条链共用同一批核心部件（`au3/`），由三种驱动方式串联：

```mermaid
flowchart LR
    subgraph 主线程
        UI[Qt/QML UI] --> PB[Au3Player / Au3Record]
    end
    PB -->|startStream| AIO[AudioIO::StartStream]
    AIO --> BUF[AllocateBuffers：混音器 + RingBuffer + 处理缓冲]

    subgraph 音频线程【生产】
        AT[AudioThread 循环 每10ms] --> FILL[FillPlayBuffers]
        FILL --> MIX[Mixer：轨道读出→包络→soxr 重采样]
        MIX --> RTE[实时效果 ProcessingScope：轨道链+主控链]
        RTE --> PUT[mPlaybackBuffers.Put]
        FILL2[DrainRecordBuffers] --> APP[WaveTrack::Append→Sequence 落块]
    end

    subgraph PA 回调线程【消费，实时约束】
        CB[audacityAudioCallback] --> GET[RingBuffer.Get + 音量 + 交错 + ClampBuffer]
        CBIN[输入解交织→mCaptureBuffers.Put] -.-> FILL2
    end

    PUT --> GET
    GET --> DEV[设备]
    DEV -.-> CBIN

    subgraph 离线渲染【主线程内同步执行】
        MAR[MixAndRender / 效果应用] --> OMIX[Mixer+EffectStage（内嵌效果）]
        OMIX --> OAPP[输出轨 Append]
        EFF[EffectBase::Process] --> EOT[EffectOutputTracks 复制影子轨] --> OMIX
    end
```

**线程约定**：
- 【主线程】UI/Qt 线程——发起播放/录制/渲染。
- 【音频线程】AudioIO 自建 `std::thread`（`AudioIO::StartThread`），默认 **10ms 轮询**填充/排空缓冲。
- 【PA 回调线程】PortAudio 设备回调（`audacityAudioCallback`），实时约束：只做取数、混音收尾、写出。

---

## 1. 播放链

### 1.1 启动段【主线程】

1. `Au3Player::play`（`src/playback/internal/au3/au3player.cpp:87`）：取 playRegion → `makeTransportTracks`（:628）把每条 WaveTrack 包成 **`StretchingSequence::Create(track, clips)`**（变速 clip 的回放管线）→ `audioEngine()->startStream(...)`。
2. `Au3AudioEngine::startStream`（`src/au3audio/internal/au3audioengine.cpp:116`）：翻译 options；`newDefault=true` 时注入 **`DefaultPlaybackPolicy`**（Loop 回绕、变光标都由 policy 承担，非 PlaybackSchedule 内建）。
3. `AudioIO::StartStream`（`au3-audio-io/AudioIO.cpp:928`）：填 `RecordingSchedule`、`mPlaybackSchedule.Init(t0,t1,options,…)`（时间轨包络经 `IntegralOfInverse` 折算 mWarpedLength）。
4. `AudioIO::StartPortAudioStream`（:477）：**播放一律 `paFloat32` + 双通道输出**；采样率 `GetBestRate` 协商；`Pa_OpenStream(..., audacityAudioCallback, ...)`（:699，`framesPerBuffer=paFramesPerBufferUnspecified`，回调帧数由后端定）。
5. `AudioIO::AllocateBuffers`（:1287），缓冲全部来自 policy 的 `{batch 0.05s, latency 0.05s, ring 0.25s}`：
   - **master RingBuffer** `mPlaybackBuffers`：2 个（每输出声道），floatSample，容量 ≈ max(0.25s×rate, 2×硬件延迟)；
   - **per-track RingBuffer**：每序列每声道 1 个，同容量，**只喂电平表**；
   - `mProcessingBuffers`（每输入声道 vector）、`mMasterBuffers`(2)、`mScratchBuffers`(2×通道+1，实时效果哑输入/哑输出)；
   - **每序列一个 Mixer**（非交织 float 输出，`ApplyVolume::Discard`——混音器不做音量、不跨轨混合）；**Inputs 不带 EffectStage**；
   - `mTimeQueue.Init`（每 480 样本一个时间记录）。
6. `TransportState` 构造（:153）：`RealtimeEffects::InitializationScope` → 每轨道组 `AddGroup`（实例按**流率**初始化，非轨道率）。
7. 预填充：置一次性标志让音频线程先灌满 RingBuffer → `Pa_StartStream` → 返回 token。

### 1.2 音频线程【生产】

8. `AudioThread`（:1900）：10ms 循环 → `SequenceBufferExchange`（:1997）= `FillPlayBuffers` + `DrainRecordBuffers`。
9. `FillPlayBuffers`（:2003）：构造 **`RealtimeEffects::ProcessingScope`**；两个 master RingBuffer 空闲 ≥ 批量才动；循环 `ProcessPlaybackSlices` 直到灌满，每轮 `Flush()` 发布（release 序）。
10. `ProcessPlaybackSlices`（:2095）一次填充的核心：
    a. `policy.GetPlaybackSlice` 给 {frames, toProduce}（尾部补零/Loop 回绕）；
    b. `mTimeQueue.Producer`：每 480 样本写一个轨道时间记录（**先写时间再写样本**，与消费端原子序配套）；
    c. 每 Mixer `Process(toProduce)`（`au3-mixer/Mix.cpp:292`）：`MixerSource::Acquire`——轨道率≠流率走 **`MixVariableRates`**（`GetFloats` 读 float 入 65536 队列 + 逐样本乘包络值 + soxr 每 1024 块重采样），同率走 `MixSameRate`；`CopySamples(float→float, 无抖动)` 落入 mBuffer；
    d. mixer 各通道 `std::copy_n` 进 `mProcessingBuffers`；
    e. **轨道级实时效果** `pScope->Process(channelGroup, …)`（`RealtimeEffectManager.cpp:122`）：逐 state 逐 512 子块处理，状态间指针交换，效果延迟样本从生产端丢弃；mute/solo 直接清零；
    f. 音量/声像汇入 `mMasterBuffers`，同时 per-track 数据 Put 进电平表 RingBuffer；
    g. **主控效果** `pScope->Process(MasterGroup, …)`（:2346）；
    h. master 结果 `mPlaybackBuffers[ch]->Put`（:2362）。

### 1.3 PA 回调线程【消费】

11. `AudioCallback`（:3336）→ `FillOutputBuffers`（:2775）：
    - seek：`CallbackDoSeek`——挂起音频线程、`Mixer::Reposition`、**Discard 全部 RingBuffer 已存数据**、重灌；
    - 每通道 `RingBuffer::Get`（不足补零，栈上 alloca 缓冲）；
    - 音量（microfades 线性插值）**叠加**进交错输出 `outputFloats[ch*nCh+i]`（与软件监听求和）；
    - `CallbackCheckCompletion`→paComplete；`ClampBuffer` 限幅。**全程 float32，无 dither**（设备格式转换由 PortAudio 完成）。
12. 时间推进（**本 fork 改动**）：回调不再推进时间，改由【主线程】16ms 定时器消费 `mAudioCallbackInfoQueue`（{dacTime, frames}，LockFreeQueue 512）外推后调 `mTimeQueue.Consumer` → `SetSequenceTime` 供 UI 读。

### 1.4 监听（monitoring）与软件播放

`StartMonitoring`（:855）开 capture-only（+软件播放时 2 输出）流：**无 Mixer、无音频线程回放侧**，回调里只走 `DoPlaythrough`（输入解交织直写输出缓冲）。录制中的软件播放同路径叠加。

---

## 2. 录制链

1. 【主线程】`RecordController::start` → `Au3Record::doRecord`（`src/record/internal/au3/au3record.cpp:671`）：目标轨走 `PendingTracks::RegisterPendingChangedTrack`（**写副本，原轨不受损**）+ 占位 clip；`startStream(t1=DBL_MAX)`。
2. `StartStream` 录制分支：`captureFormat = 轨道存储格式`；**int24 特例替换为 float**（不直接用 PA 的 3 字节格式，:541）。
3. `AllocateBuffers`：每捕获声道 1 个 `RingBuffer(mCaptureFormat, ≈4.5+0.5·min(16,ch) 秒)`；`mResample[i] = Resample(true, mFactor)`，**mFactor = 项目率/设备率**；`mMinCaptureSecsToCopy = 0.2s`。
4. 【PA 回调】`DrainInputBuffers`（:2974）：按各声道 Avail 最小值截断（丢样统计）；解交织（int16 带 clamp）→ `mCaptureBuffers[t]->Put + Flush()`（每回调）。
5. 【音频线程】`DrainRecordBuffers`（:2388，攒满 0.2s 才动）：
   a. **延迟校正**：`TotalCorrection() = mLatencyCompensation − mLeadInTime`；负值→从 RingBuffer 头 `Discard`；正值→先 `Append` 静音；
   b. `mCaptureBuffers->Get` 出来；**mFactor≠1 时在此做重采样**（先转 float 再 soxr）；
   c. 多输入通道按 `mTrackChannelSourceMap` 平均混合；
   d. `sequence->Append(..., narrowestSampleFormat)`（:2654）。
6. Append 落轨：`WaveTrack::Append` → `WaveClip::Append` → `Sequence::Append`——先进内存 `mAppendBuffer`，攒到 `GetIdealAppendLen()` 就 `DoAppend` 写成 SqliteSampleBlock 入 BlockArray。
7. 【主线程】`StopStream`（:1555）：`Pa_AbortStream` → 音频线程最后一次 exchange 排空 → 逐 `sequence->Flush()` 把 AppendBuffer 余量落盘 → 丢样区间 `InsertSilence` 补洞 → `commitRecording` 转正+写历史。

---

## 3. 离线渲染链

### 3.1 MixAndRender（`au3-effects/MixAndRender.cpp:22`）

调用方：菜单"混音并渲染"、双 mono 合成立体声（`au3tracksinteraction.cpp:1214`）、**效果预览前的预混优化**（`EffectPreview.cpp:129`）。

1. 统计输入（全 mono 且 pan=0 → 输出单声道）；rate/format 由调用方给定。
2. 每条输入轨：`StretchingSequence::Create` + **`GetEffectStages(*wt)`**——把轨道实时效果链打包成 `MixerOptions::StageSpecification`。**离线渲染时实时效果经 EffectStage 内嵌进 Mixer**（与回放不同！）。
3. 建 `Mixer`（outBlockSize = `GetIdealBlockSize()` = 262144 样本@float）。
4. 循环 `mixer.Process()`：`MixerSource::MixVariableRates` 重采样 → EffectStage 逐效果处理 → 最终 `CopySamples(float→输出格式, 按需 dither)` → 输出轨 `AppendBuffer`。
5. `mix->Flush()`；成功后 `RealtimeEffectList::Get(*mix).Clear()`（渲染结果剥离效果）。

### 3.2 离线效果应用（复制轨道+替换模式）

入口：`EffectExecutionScenario::performEffect`（`src/effects/effects_base/internal/effectexecutionscenario.cpp:100`）或 `EffectBase::DoEffect`。

1. `EffectOutputTracks`（`au3-effects/EffectOutputTracks.cpp:20`）：每条选中轨 `Duplicate(Backup)` 成影子轨（区间内有 pitch/speed 先解拉伸）。
2. `PerTrackEffect::DoProcess`：`blockSize = SetBlockSize(GetMaxBlockSize()*2)` → `WideSampleSource`（轨读 float）→ **`EffectStage::Create`**（`ProcessInitialize`）→ `AudioGraph::Task::RunLoop`：`Acquire`→`instance.ProcessBlock`（首块后查 `GetLatency` 一次性丢弃前置延迟、尾部补零平衡）→ `WaveTrackSink` 写影子轨。
3. 生成器类把结果轨 `ClearAndPaste` 贴回（处理器类直接写在副本上无需 paste）；`EffectOutputTracks::Commit` 用 `ReplaceOne` 转正；事务 + 历史栈。
4. **两遍效果**：`EffectTwoPassSimpleMono` 框架（LegacyCompressor）；NoiseReduction 的两遍 = "取噪声画像"与"OK 应用"两次 Process，profile 经 EffectSettings 传递。

---

## 4. 环节契约速查

| 缓冲 | 个数 | 形状/格式 | 写者→读者 |
|---|---|---|---|
| mPlaybackBuffers（master） | 2 | RingBuffer, float, ~250ms | 音频线程 → PA 回调 |
| mPlaybackTracks[].mBuffers | 每轨×每通道 | RingBuffer, float, 同容量 | 音频线程 → 回调（仅电平表） |
| mProcessingBuffers / mMasterBuffers | Σ轨通道 / 2 | vector\<float\> | 音频线程内部 |
| mCaptureBuffers | 每捕获声道 | RingBuffer, int16 或 float（24bit 存 float），~4.5s+ | PA 回调 → 音频线程 |
| mTimeQueue | 1 | 每 480 样本一个 double | 音频线程 Producer → 主线程 Consumer |
| mAudioCallbackInfoQueue | 1 | LockFreeQueue(512){steady_clock, frames} | PA 回调 → 主线程 |
| Sequence::mAppendBuffer | 每 clip 通道 | mMaxSamples（float=256K 样本），stored 格式 | 录制/渲染 → SampleBlock 落盘 |
| MixerSource::mSampleQueue | 每通道每源 | 65536 float（256KB），需重采样时 | 轨道读出 → soxr |
| EffectStage::mStageBuffers | 每效果级 | 3×blockSize float（含哑通道，Issue 3854） | 上游 → 效果 → 下游 |

**格式转换点汇总**：
| 位置 | 转换 |
|---|---|
| 轨道读出（MixerSource/WideSampleSource） | 存储格式（int16/24/float）→ float，无抖动 |
| 录制回调 | 设备 int16 → float 解交织；24bit 一律 float |
| 录制音频线程 | 设备率 → 项目率（soxr，Best 档） |
| Mixer::Process 末尾（离线） | float → 输出轨存储格式，窄化时 dither（导出高质量/回放低质） |
| Sequence::Append（录制） | 输入格式 → stored 格式，收窄时高质 dither |
| 输出到设备 | 全程 float32；int16 化由 PortAudio 负责 |

**重采样发生点**：播放=MixerSource（轨道率→流率，Fast 钥匙默认 Best 档）；录制=DrainRecordBuffers（设备率→项目率）；离线=WaveClip::Resample 或 MixerSource。

## 5. 新架构（src/）挂接点

- **播放**：`Au3Player` 实现 `IPlayer`；`Au3AudioEngine` 是 AU3 `AudioIO` 单例薄包装（startStream/stop/pause/seek/monitoring/音量一一转发）；播放头位置改由 Qt 定时器消费回调信息队列推进（见 §1.3.12）。
- **实时效果**：`RealtimeEffectService`（`src/effects/effects_base/internal/realtimeeffectservice.cpp`）的 add/remove/replace → `audioEngine()->addRealtimeEffectState…` → `AudioIO::AddState/RemoveState`（回放中也能热插）→ `RealtimeEffectList` 发消息回 UI 镜像。
- **录制**：`RecordController` → `Au3Record`（PendingTracks 副本 + commitRecording 转正）；输入监听/音量经 `audioEngine()->startMonitoring`/`SetMixer`；事件经 `Au3AudioIOListener` 回流新架构。
