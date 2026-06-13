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

**不使用未来数据。** 预测某一场时，只使用排在它之前的比赛（资格赛预测用更早的资格赛；
淘汰赛预测再叠加已打完的淘汰赛）。因此队伍的均分和已打场数反映的是“该比赛开始前”的状态，
使离线回放（`--evaluate`）和实时预测使用完全相同的信息口径。

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

CLI 的 `--picklist` 会根据资格赛表现给队伍打分排序，综合三项：强度（均分）、稳定性（分数方差越小越好）、
趋势（近期相对早期的提升），并按已打场数做置信度衰减。可用 `--strategy balanced|offense|consistency`
切换权重，`--exclude` 排除已被选走的队伍，`--before MATCH_KEY` 截止到赛事某一刻排名。

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
  "cache_dir": "data/cache",
  "cache_ttl_seconds": 60,
  "confidence_match_count": 6,
  "score_diff_scale": 30.0,
  "sigmoid_scale": 1.0,
  "model_version": "baseline-v1"
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

### Web Dashboard (Flask)

1. 先编译 CLI（网页会调用 `build/frc_prediction`）：

```bash
cmake --build build
```

2. 启动服务：

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app.py
```

3. 浏览器打开 `http://127.0.0.1:5001`，输入赛事 key（可选填一场比赛），点击 **Run**。
   网页会自动帮你跑 CLI，不用手动生成数据文件。

网页功能：

- **胜率进度条** + 左红右蓝的联盟对比（预计得分、胜率、置信度、平均比赛数）。
- **Team Stats** 表格和图表；选中比赛的队伍会被红/蓝高亮，并在图表上方列出其均分。
- **比赛简写**：可直接输入 `3`、`qm3`、`sf2m1`，不用写完整 key。
- **自动刷新**：可配置间隔，赛事进行中保持预测更新。
- **Picklist 区块**：选择策略（balanced/offense/consistency）、排除已选队伍，一键生成排名表 + 图表。
  复用上方的 Event 和 Match 输入（Match 作为“截止到该场”的 cutoff）。

示例：

```bash
./build/frc_prediction --event 2024casj --matches
./build/frc_prediction --event 2024casj --rankings
./build/frc_prediction --event 2024casj --teams
./build/frc_prediction --event 2024casj --stats
./build/frc_prediction --event 2024casj --stats --top 10
./build/frc_prediction --event 2024casj --stats-json --top 10
./build/frc_prediction --event 2024casj --stats --top 10 --stats-csv data/stats.csv
./build/frc_prediction --event 2024casj --predict 2024casj_qm1
./build/frc_prediction --event 2024casj --predict-upcoming
./build/frc_prediction --event 2024casj --predict-upcoming --json
./build/frc_prediction --event 2024casj --predict-upcoming --json --output data/prediction.json
./build/frc_prediction --event 2024casj --evaluate
./build/frc_prediction --event 2024casj --evaluate --phase qm
./build/frc_prediction --event 2024casj --evaluate --phase elim --eval-json data/eval.json
./build/frc_prediction --event 2024casj --evaluate --phase all --eval-csv data/eval.csv
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced

当使用 --json 且未指定 --output 时，默认输出路径：

```
data/predictions/<match_key>.json
```

预测输出包含队伍数量、联盟平均比赛数，以及相对赛场均分的调整值。
资格赛预测只使用资格赛比赛；淘汰赛预测使用资格赛加已完成的淘汰赛。
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

## Picklist 策略（如何计算）

Picklist 的核心目标是联盟互补，而不是简单的强到弱排序。

### 输入特征

- 资格赛（默认）中的队伍平均得分
- 标准差（稳定性）
- 最近三场的趋势
- 自己队伍的平均得分（用于互补/重叠惩罚）

### 评分公式

对每个候选队伍：

```
strength    = candidate_avg / event_avg
consistency = 1 / (1 + stddev)
trend       = (recent_avg - candidate_avg) / event_avg

if my_avg >= event_avg:
  complement = event_avg / (candidate_avg + event_avg)  # 更偏辅助
else:
  complement = candidate_avg / (candidate_avg + event_avg)  # 更偏得分

overlap_penalty = max(0, 1 - abs(candidate_avg - my_avg) / event_avg)

picklist_score =
  w_strength * strength
  + w_consistency * consistency
  + w_trend * trend
  + w_complement * complement
  - w_overlap * overlap_penalty
```

策略预设权重：

- **balanced**：0.45 strength，0.25 consistency，0.10 trend，0.25 complement，0.15 overlap
- **offense**：0.60 strength，0.15 consistency，0.10 trend，0.30 complement，0.10 overlap
- **consistency**：0.30 strength，0.50 consistency，0.10 trend，0.30 complement，0.10 overlap

### 使用方式

```
./build/frc_prediction --event 2024casj --picklist frc254 --top 24 --strategy balanced
```
