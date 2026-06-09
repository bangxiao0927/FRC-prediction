# FRC Prediction

FRC Prediction 是一个面向 FRC 赛事的数据预测与选队辅助工具。项目目标是在 qualification 阶段实时预测比赛胜负概率，并在 elimination/alliance selection 阶段根据队伍表现推荐 picklist 和联盟组合。

**语言**：中文（本页），英文版请见 [README](README.md)。

## 核心目标

- **Qualification 实时预测**：根据当前赛事数据、队伍近期表现、联盟组合与对手组合，输出胜负概率和预计分差。
- **Elimination 组合预测**：根据资格赛表现、队伍均分、稳定性和战术互补程度，预测淘汰赛联盟表现。
- **Picklist 推荐**：为 alliance selection 提供候选队伍排序，辅助选择最适合本队战术方向的队伍。
- **可扩展数据管线**：从 The Blue Alliance (TBA) 获取官方数据，后续支持现场 scouting 数据补充。

## 当前技术栈

- **语言**：C++17
- **构建系统**：CMake
- **依赖管理**：vcpkg manifest 模式
- **HTTP 客户端**：`cpr`
- **JSON 解析**：`nlohmann/json`

选择 C++ 的原因是性能好、适合实时计算，也更贴近很多 FRC 队伍的开发环境。vcpkg 用来降低 macOS 上配置第三方库的复杂度。

## 数据来源

- The Blue Alliance API：官方赛事、队伍、比赛、排名与比分数据
- 未来扩展：现场 scouting 数据，例如实际 scoring 能力、防守能力、失误率、机械故障等

## 预测思路

### Qualification 阶段

输入：

- 队伍在当前赛事中的已完成比赛表现
- 队伍近期历史表现
- 红蓝双方联盟组合
- TBA 提供的比分、排名和赛程信息

输出：

- 红/蓝胜率
- 预计分差
- 不确定性提示（数据不足、队伍样本太少等）

### Elimination / Picklist 阶段

输入：

- 资格赛均分、稳定性、趋势
- 队伍战术角色：进攻、防守、endgame、特殊任务等
- 不同队伍组合后的预估得分

输出：

- picklist 排序
- 联盟组合预估分数
- 淘汰赛对局胜率

## 项目结构

```text
.
├── CMakeLists.txt
├── PLAN.md
├── README.md
├── README.zh-CN.md
├── config.example.json
├── src/
│   └── main.cpp
└── vcpkg.json
```

## 快速开始

### 1. 安装 vcpkg

如果你还没有安装 CMake，可以先安装：

```bash
brew install cmake
```

然后安装 vcpkg。vcpkg 可以放在任意目录，例如：

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
```

建议把 `export VCPKG_ROOT=~/vcpkg` 写入你的 shell 配置文件，例如 `~/.zshrc`。

### 2. 配置 TBA API Key

从 The Blue Alliance 获取 API Key 后，可以选择下面任意一种方式：

方式 A：使用环境变量：

```bash
export TBA_AUTH_KEY=your_key_here
```

方式 B：使用本地配置文件：

```bash
cp config.example.json config.json
```

然后编辑 `config.json`：

```json
{
  "tba_auth_key": "your_key_here",
 "default_event_key": "2024casj",
  "cache_dir": "data/cache"
}
```

`config.json` 会被 Git 忽略，避免把 API Key 提交到仓库。
响应会缓存到 `cache_dir`，以减少重复请求。

### 3. 构建

在项目根目录执行：

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

### 4. 运行

```bash
./build/frc_prediction --status
```

示例：

```bash
./build/frc_prediction --event 2024casj --matches
./build/frc_prediction --event 2024casj --rankings
./build/frc_prediction --event 2024casj --teams
```

## 开发路线

- [x] 初始化 CMake + vcpkg 工程
- [x] 加入 TBA API 最小请求示例
- [x] 加入本地配置模板
- [ ] 封装 TBA Client
- [ ] 拉取赛事比赛与排名数据
- [ ] 加入本地缓存
- [ ] 计算队伍基础统计指标
- [ ] 生成 qualification 胜率预测
- [ ] 生成 elimination picklist 推荐

## 近期 TODO

- 封装 `TbaClient` 类，避免 API 请求逻辑集中在 `main.cpp`
- 增加 `EventData` / `TeamStats` / `MatchPrediction` 等数据结构
- 设计基础评分模型：均分、稳定性、近期趋势、联盟协同
- 增加命令行参数，例如 `--event 2024casj`、`--match qm1`

## 贡献与协作

这个项目还处于早期阶段，优先目标是先做出可运行 MVP：能拉取一个赛事的数据，并对 qualification match 给出可解释的胜负概率。后续再扩展到更复杂的 picklist 和 elimination 预测。
