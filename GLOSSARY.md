# Audacity（本仓库）

Audacity 4：Qt/QML 新架构应用层 + 完整嵌入的 Audacity 3 核心。本词汇表定义音频领域的规范术语，供人与 agent 在讨论、文档与代码导航时统一语言。

## Language

### 架构分层

**AU3**:
仓库内 `au3/` 子项目，Audacity 3 核心（约 70 个静态库），全部音频算法本体所在。
_Avoid_: 旧核心、legacy 层（口语可用，书面用 AU3）

**AU4**:
仓库内 `src/` 的 Qt/QML 新架构层，经 au3wrap 桥接 AU3，只承担编排与视图。
_Avoid_: 新 UI、Qt 层

### 链路

**音频链路 (Audio Pipeline)**:
音频数据从轨道存储到设备（或反向）流经的部件序列；分为播放链、录制链、离线渲染链三条。
_Avoid_: 音频管线、播放引擎（后者仅指 AudioIO）

**播放链 (Playback chain)**:
音频线程由混音器填充数据、经实时效果、写入播放环，PA 回调消费出设备的路径。
_Avoid_: 回放链

**录制链 (Record chain)**:
PA 回调把设备输入写入捕获环，音频线程消费、按需重采样后追加进轨道副本的路径。
_Avoid_: 录音链

**离线渲染链 (Offline render chain)**:
主线程内同步执行的混音/导出/效果应用路径；实时效果在此经 EffectStage 内嵌进混音器。
_Avoid_: 导出链（导出只是其调用方之一）

**播放环 (Playback RingBuffer)**:
音频线程与 PA 回调之间的无锁 float 环形缓冲；master 每输出声道一个，另有仅供电平表的每轨环。
_Avoid_: RingBuffer 裸名（捕获环同类不同向）

**捕获环 (Capture RingBuffer)**:
PA 回调到音频线程方向的录制环形缓冲，格式为捕获格式（int24 一律以 float 承载）。

### 效果

**效果器 (Effect)**:
注册进内置注册表或插件的音频处理单元，按 EffectGroup 分官方类别。
_Avoid_: 插件（插件特指 VST/AU/LV2/Nyquist 等宿主外单元）

**实时效果 (Realtime effect)**:
挂在轨道链或主控链上、播放中随数据流调用的效果槽位；设置用双缓冲交换协议传入音频线程。
_Avoid_: 实时插件

**主控链 (Master chain)**:
作用于全项目混音结果的效果列表，区别于每轨的效果列表。

**效果实例 (Effect instance)**:
效果器的一次可运行化（携带流状态：延迟线、滤波器状态），生命周期为 Initialize/Process/Finalize。
_Avoid_: slave（旧称）

**EffectStage**:
离线渲染路径中把效果实例插入混音器拉取链的装饰级；实时播放不经过它。
_Avoid_: （无俗名，注意与实时路径区分即可）

### 存储

**波形轨道 (WaveTrack)**:
≤2 通道的音频轨道，持有若干剪辑。
_Avoid_: 音轨

**剪辑 (WaveClip)**:
一段带独立偏移/包络/速率的音频区间，每通道持有一个序列。
_Avoid_: 片段

**序列 (Sequence)**:
单通道样本的块列表（二分定位），录制与渲染的追加终点。
_Avoid_: （勿与 StretchingSequence 混用）

**样本块 (SampleBlock)**:
序列的磁盘存储单元（SQLite BLOB，约 128K–256K float 样本/块，附两级 min/max/RMS 摘要）；样本写入后内存副本即释放。
_Avoid_: BlockFile（旧称）

**有效格式/存储格式 (Effective/Stored format)**:
序列样本的实际信息量与物理存放格式；窄化回有效格式时免抖动，保证位精确导出。

### 信号处理

**抖动 (Dither)**:
格式窄化时加入的定向噪声，分 none/rectangle/triangle/shaped 四档；播放链 float 直通不抖动，导出默认 shaped。

**重采样 (Resample)**:
soxr 单声道流式封装；播放（轨道率→流率）、录制（设备率→项目率）、离线整 clip 三处各有一份实例。

**包络 (Envelope)**:
控制点压缩存储的分段线性增益曲线；播放时以增量法逐样本乘入混音队列。

**拉伸序列 (StretchingSequence)**:
把变速/变调 clip 包装成可与波形轨道互换的播放序列；内核为相位锁定声码器，随机访问需重置重拉伸。
_Avoid_: 时伸缩轨
