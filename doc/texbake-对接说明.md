# texbake 贴图烘焙库 · 对接说明

> 适用范围：把「毛色 + 斑纹 + 白斑 + 重点色 + 银化」等外观信息，按绘制指令烘焙进猫模型
> 的 baseColor 贴图，输出可直接加载的新 GLB。
>
> 本库不依赖基因库、不调用任何 AI 绘图服务、不发起网络请求。输入是纯几何/颜色指令，
> CPU 逐像素绘制，本地编码 PNG。

---

## 1. 能力总览

| 能力 | 说明 |
|---|---|
| 3D 表面烘焙（推荐） | 把图案定义在模型对象空间，逐 UV 纹素求值后落到贴图；跨 UV 岛连续，不受 atlas 碎片化影响 |
| UV 图鉴渲染 | 沿用 0..1024 UV 坐标，适合预览、缩略图，不依赖模型 |
| 两层图元体系 | UV 图元（元球/贝塞尔条纹/渐变/噪声）+ 3D 图元（椭球/条带/渐变/噪声） |
| 空间过滤 | 3D 图层可按 body_u、高度、法线朝向、AABB 区域限制绘制范围 |
| gutter 膨胀 | 栅格化后向 UV 岛外扩展若干像素，消除双线性采样接缝与跨岛串色 |
| 模型贴图细节保留 | 以原模型 baseColor 贴图按 UV 采样作为明暗基底，保留毛发/阴影细节 |
| GLB 原地改造 | 读取 GLB，追加 PNG，改写材质 baseColorTexture，其余数据原样保留 |
| 多材质处理 | 每个使用 baseColorTexture 的材质都会烘焙；同一张源图只烘焙一次 |
| PNG 导出 | 可选，便于检查烘焙结果 |
| 可复现 | 噪声带种子；同一组输入结果一致 |

### 不做什么（明确边界）

- 不改模型形状：短腿/断尾/折耳等由主项目切换不同模型，本库只负责贴图。
- 不做 UV 展开/重排：仍使用模型自带 UV atlas，图案靠 3D 求值避免断裂。
- 不做色彩空间转换：按 sRGB 字节值直接相乘，属于游戏常用近似。
- 不从骨骼权重分部位：本库按几何量（body_u / 高度 / 法线 / AABB）划分区域。
  原因见 6.3。
- 不处理外部贴图依赖：不支持 `images[].uri`、KHR_texture_basisu 等本库无法解码的贴图。
- 不负责渲染显示：只产出 GLB/PNG 数据。

---

## 2. 文件与构建

```
include/texbake.h        公共头文件（对接只需要它）
src/texbake.c            公共 API 编排 + UV 图层光栅化
src/tb_surface.c/.h      网格/UV 栅格化、3D 求值、gutter 膨胀（内部）
src/tb_glb.c/.h          GLB 读写与 accessor 访问（内部）
src/tb_json.c/.h         JSON DOM（内部）
src/tb_image.c/.h        贴图解码/缩放/PNG 编码（内部）
src/tb_stb_impl.c        stb 实现编译单元（内部）
third_party/stb/         stb_image / stb_image_write（公有领域/MIT）
doc/texbake-对接说明.md   本文档
```

本仓库直接构建：

```bash
cmake -S . -B build -G Ninja
cmake --build build
./bin/modelmaking-test.exe
```

产物：`bin/libmodelmaking.a`、`bin/modelmaking-test.exe`。

### 在自己的项目里使用

只需要公共头文件和静态库，**不需要** `src/` 与 `third_party/`：

```cmake
target_include_directories(your_app PRIVATE path/to/model-making/include)
target_link_libraries(your_app PRIVATE path/to/model-making/bin/libmodelmaking.a)
# Linux/macOS 还需要 m
```

或以子目录方式引入：

```cmake
add_subdirectory(path/to/model-making modelmaking_build)
target_include_directories(your_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/path/to/model-making/include)
target_link_libraries(your_app PRIVATE modelmaking)
```

---

## 3. 核心概念

### 3.1 两套坐标空间

**UV 空间（旧模式）**

- 绘制坐标固定 `0..1024`，`坐标 / 1024 = UV`，`x` 向右、`y` 向下。
- 图案直接画在贴图上，跨 UV 岛会断开，适合图鉴预览，不建议正式烘焙。

**归一化对象空间（表面烘焙，推荐）**

- 取模型 `POSITION` 包围盒中心为原点；
- 包围盒**最长边的一半**缩放到 1，其余轴等比缩放（保持形状比例）；
- 轴方向与模型文件一致。`cat.glb` 中：`x` 为左右、`y` 向上、`z` 为身体前后。
- 坐标大致落在 `[-1, 1]`，最长轴恰好 `-1..1`。

所有 3D 图层都在这个空间里求值，再逐个 UV 纹素落回 atlas，所以同一图案跨岛连续。

### 3.2 图层叠加

```
base_color  →  layers[0]  →  layers[1]  → ...  →  乘原贴图细节  →  输出
```

图层顺序就是数组顺序，后画的覆盖先画的。

### 3.3 表面烘焙流程

```
读 GLB
  → 合并所有带 UV 的 primitive（应用节点变换）
  → 按 UV 栅格化每个三角形
  → 每个纹素插值出：对象空间位置 P、法线 N、body_u
  → 在 3D 中求值所有 3D 图层 → 写入 RGBA
  → 向 UV 岛外膨胀 gutter 像素（最近纹素填充）
  → 采样原 baseColor 贴图做明暗细节
  → 编码 PNG、追加到 GLB、替换材质 baseColorTexture
```

属性（位置/法线/body_u）是逐纹素即时计算并使用的，不落盘成中间图，
避免 4096 尺寸下数百 MB 的属性图内存。需要诊断时用 `TexbakeSurfaceStats`。

### 3.4 细节保留

`detail_strength` 控制原贴图的明暗参与程度：

```
输出 = 图案色 × (1 - d + d × 原贴图采样值)
```

- `d = 1`：完整保留原贴图明暗（推荐，毛发细节明显）
- `d = 0`：完全平涂，便于检查图案本身
- 表面烘焙会按每个纹素的 UV 从原图做双线性采样，而不是先缩放整张图

### 3.5 gutter 膨胀

栅格化只覆盖 UV 岛内部。`gutter` 会用**最近纹素**的颜色向外填充（8 邻域 BFS），
默认 8 像素：

- 岛边缘的双线性采样不会采到空白像素，不会出现接缝；
- 两个岛距离较近时，按“谁更近”分配，避免把邻岛颜色抹过来造成串色；
- `fill_background = 1` 会把 gutter 之外的空白也填满，利于生成 mipmap，但更耗时。

建议 `gutter >= 4`；同时建议运行时关闭 baseColor 的 mipmap，或至少保留足够 padding。

### 3.6 输出尺寸

- `bake_size = 0`：默认 `TEXBAKE_DEFAULT_SIZE`（1024）
- 显式指定：`1..TEXBAKE_MAX_SIZE`（4096）
- 尺寸为正方形；原贴图按 UV 逐纹素采样，不改变图案坐标含义

---

## 4. 公共 API

### 4.1 数据结构

```c
#define TEXBAKE_MAX_LAYERS 6
#define TEXBAKE_MAX_BLOBS  8
#define TEXBAKE_MAX_CURVES 6
#define TEXBAKE_UV_SCALE   1024.0f
#define TEXBAKE_DEFAULT_SIZE 1024
#define TEXBAKE_MAX_SIZE     4096

typedef struct { uint8_t r, g, b; } TexbakeColor;
typedef struct { float x, y, z; } TexbakeVec3;

typedef struct {
    TexbakeColor base_color;          // 打底色
    TexbakeLayer layers[TEXBAKE_MAX_LAYERS];
    int layer_count;
    float detail_strength;            // 0..1，默认 1
    int bake_size;                    // 0 = 1024
} TexbakePattern;
```

3D 图元：

```c
typedef struct {
    float x, y, z;                    // 中心
    float rx, ry, rz;                 // 三个半轴，>0
    float softness;                   // 0..1 边缘柔化
} TexbakeBlob3D;

typedef struct {
    float ax, ay, az;                 // 条纹重复方向
    float period;                     // 相邻条纹中心间距，>0
    float width;                      // 每条纹宽，>0
    float phase;                      // 0..1 相位
    float warp;                       // 0..1 噪声扰动
    float warp_freq;                  // 扰动频率，建议 1~4
} TexbakeStripe3D;

typedef struct {
    float ax, ay, az;                 // 渐变轴
    float start, end;                 // 沿轴起止，起点为底色，终点为图层色
} TexbakeGradient3D;

typedef struct {
    int enabled;                      // 0 = 不做过滤（默认）
    float body_u_min, body_u_max;     // 0..1
    float y_min, y_max;               // 归一化 y
    float normal_y_min, normal_y_max; // -1 朝下 .. 1 朝上
    float box_min[3], box_max[3];     // 归一化空间 AABB
    int invert;                       // 1 = 反选
} TexbakeFilter;

void texbake_filter_init(TexbakeFilter* filter); // 关闭过滤并填满默认范围
```

单个图层：

```c
typedef struct {
    TexbakeLayerKind kind;            // REGION/STRIPE/GRADIENT/NOISE
                                      // 或 BLOB3D/STRIPE3D/GRADIENT3D/NOISE3D
    TexbakeColor color;
    float strength;                   // GRADIENT/GRADIENT3D/NOISE/NOISE3D 生效

    // UV 图层
    TexbakeBlob blobs[TEXBAKE_MAX_BLOBS];
    int blob_count;
    float threshold;
    TexbakeCurve curves[TEXBAKE_MAX_CURVES];
    int curve_count;
    float gx0, gy0, gx1, gy1;
    float frequency;                  // UV 噪声：每 UV 单位的格点数
    uint32_t seed;

    // 3D 图层
    TexbakeBlob3D blobs3d[TEXBAKE_MAX_BLOBS];
    int blob3d_count;
    TexbakeStripe3D stripe3d;
    TexbakeGradient3D gradient3d;
    // 3D 噪声复用 frequency（每归一化单位的格点数）与 seed

    TexbakeFilter filter;             // 仅 3D 图层生效
} TexbakeLayer;
```

3D 图层语义：

| kind | 覆盖规则 |
|---|---|
| `TEXBAKE_LAYER_BLOB3D` | 多个椭球取最大覆盖；`distance = sqrt((x-cx)²/rx² + …)`，`d > 1` 为外部，`softness` 控制边缘过渡 |
| `TEXBAKE_LAYER_STRIPE3D` | 沿 `(ax,ay,az)` 方向按 `period` 重复的条带；`width` 是条带宽度；`warp` 用 3D 噪声扰动相位，`warp` 的单位是“一个 period 的比例” |
| `TEXBAKE_LAYER_GRADIENT3D` | 沿轴线性过渡，`dot(P, axis)` 从 `start` 到 `end`；`strength` 控制最大混入比例 |
| `TEXBAKE_LAYER_NOISE3D` | 对象空间 3D 值噪声 fbm，`frequency` 为每归一化单位的格点数；`strength` 为最大混入比例；同 `seed` 可复现 |

过滤条件取交集，`invert` 最后取反。示例：

- 只画背部：`normal_y_min = 0.3`
- 只画腹部：`normal_y_max = -0.3`
- 只画头部附近：`body_u_min = 0.75`（配合 `body_axis_flip` 调整方向）
- 只画脚掌：`y_max = -0.7`，或用 `box_min/box_max` 精确框选
- 只画胸口：`body_u` 区间 + `normal_y_max` + `box`

### 4.2 表面烘焙选项

```c
typedef struct {
    int gutter;                 // UV 岛外膨胀像素，0 = 不膨胀，默认 8
    float body_axis[3];         // body_u 轴；全 0 = 自动取最长水平轴
    int body_axis_flip;         // 1 = 反转 body_u
    int fill_background;        // 1 = 空白区域也用最近纹素填满
} TexbakeSurfaceOptions;

void texbake_surface_options_init(TexbakeSurfaceOptions* options);

typedef struct {
    int width, height;
    int triangle_count;
    int covered_texels;
    int gutter_texels;
    float coverage;             // covered / (width*height)
} TexbakeSurfaceStats;
```

`body_u` 自动轴：比较归一化空间 `x` 与 `z` 方向的包围盒长度，取较长者；
`0` 在该轴负方向端、`1` 在正方向端。若与模型头尾方向相反，置 `body_axis_flip = 1`，
或直接传入自定义 `body_axis`。

### 4.3 函数

```c
void  texbake_pattern_init(TexbakePattern* pattern);   // 必须先调用
void  texbake_default_palette(TexbakePalette* out);
TexbakeColor texbake_palette_get(const TexbakePalette*, int index);
void  texbake_filter_init(TexbakeFilter* filter);
void  texbake_surface_options_init(TexbakeSurfaceOptions* options);

// ---- UV 模式（预览） ----
int texbake_render(const TexbakePattern* pattern,
                   int width, int height,
                   const uint8_t* detail_rgb,   // 可为 NULL
                   TexbakeImage* out);

int texbake_bake_glb(const char* glb_path_in,
                     const char* glb_path_out,
                     const char* png_path_out,
                     const TexbakePattern* pattern);

// ---- 表面烘焙（正式使用） ----
int texbake_render_surface(const char* glb_path,
                           const TexbakePattern* pattern,
                           const TexbakeSurfaceOptions* options, // 可为 NULL
                           TexbakeImage* out,
                           TexbakeSurfaceStats* stats);          // 可为 NULL

int texbake_bake_glb_surface(const char* glb_path_in,
                             const char* glb_path_out,
                             const char* png_path_out,
                             const TexbakePattern* pattern,
                             const TexbakeSurfaceOptions* options); // 可为 NULL

void texbake_image_free(TexbakeImage* image);
```

`TexbakeImage`：`width / height / rgba`，RGBA 行优先、顶部起始、A 恒为 255。

表面 API **只接受 3D 图层**；混入 UV 图层会返回 `TEXBAKE_ERR_INVALID_ARG`。

### 4.4 返回码

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `TEXBAKE_OK` | 成功 |
| -1 | `TEXBAKE_ERR_NULL` | 必填指针为空 |
| -2 | `TEXBAKE_ERR_INVALID_ARG` | 尺寸/图层数量/字段越界，或表面 API 收到 UV 图层 |
| -3 | `TEXBAKE_ERR_IO` | 读写文件失败 |
| -4 | `TEXBAKE_ERR_FORMAT` | GLB/JSON/贴图解析失败或结构不支持 |
| -5 | `TEXBAKE_ERR_NO_BASE_TEXTURE` | 没有可烘焙的 baseColorTexture，或全部源图解码失败 |
| -6 | `TEXBAKE_ERR_OOM` | 内存分配失败 |

---

## 5. 最小使用示例

### 5.1 表面烘焙（推荐）

```c
#include "texbake.h"

int bake_example(void)
{
    TexbakePattern pattern;
    texbake_pattern_init(&pattern);
    pattern.base_color = (TexbakeColor){ 222, 132, 58 }; // 橘色底
    pattern.bake_size = 1024;
    pattern.detail_strength = 1.0f;

    /* 虎斑：沿身体前后轴重复，带有机弯曲 */
    TexbakeLayer* stripe = &pattern.layers[pattern.layer_count++];
    *stripe = (TexbakeLayer){ 0 };
    stripe->kind = TEXBAKE_LAYER_STRIPE3D;
    stripe->color = (TexbakeColor){ 30, 28, 26 };
    stripe->stripe3d.ax = 0.0f;
    stripe->stripe3d.ay = 0.0f;
    stripe->stripe3d.az = 1.0f;      // cat.glb 的 z 是身体前后轴
    stripe->stripe3d.period = 0.22f;
    stripe->stripe3d.width = 0.075f;
    stripe->stripe3d.warp = 0.35f;
    stripe->stripe3d.warp_freq = 2.0f;
    stripe->seed = 12345u;

    /* 腹部白色：只画朝下的表面 */
    TexbakeLayer* belly = &pattern.layers[pattern.layer_count++];
    *belly = (TexbakeLayer){ 0 };
    belly->kind = TEXBAKE_LAYER_BLOB3D;
    belly->color = (TexbakeColor){ 248, 247, 243 };
    belly->blob3d_count = 1;
    belly->blobs3d[0] = (TexbakeBlob3D){ 0.0f, -0.1f, 0.0f, 0.5f, 0.5f, 0.8f, 0.3f };
    texbake_filter_init(&belly->filter);
    belly->filter.enabled = 1;
    belly->filter.normal_y_max = -0.15f;

    TexbakeSurfaceOptions options;
    texbake_surface_options_init(&options); // gutter = 8

    return texbake_bake_glb_surface("assets/cat.glb", "cat_baked.glb",
                                    "cat_baked.png", &pattern, &options);
}
```

只做预览、不写 GLB：

```c
TexbakeImage image = { 0 };
TexbakeSurfaceStats stats = { 0 };
int status = texbake_render_surface("assets/cat.glb", &pattern, NULL, &image, &stats);
if (status == TEXBAKE_OK) {
    // stats.coverage 可用于检查 UV 覆盖率，例如 0.603
    texbake_image_free(&image);
}
```

> 注意：`texbake_pattern_init` 会把整个 pattern（含所有图层）清零。
> 追加图层时请用 `*layer = (TexbakeLayer){ 0 };` 清零，避免残留字段。
> 启用 `filter` 前先调用 `texbake_filter_init`，否则范围字段为 0 会把整张图过滤掉。

### 5.2 UV 预览模式

```c
pattern.bake_size = 512;
pattern.layer_count = 1;
pattern.layers[0] = (TexbakeLayer){ 0 };
pattern.layers[0].kind = TEXBAKE_LAYER_STRIPE;
pattern.layers[0].color = (TexbakeColor){ 30, 28, 26 };
pattern.layers[0].curve_count = 1;
pattern.layers[0].curves[0] = (TexbakeCurve){ 0, 512, 512, 300, 1024, 512, 70, 70 };

TexbakeImage image = { 0 };
texbake_render(&pattern, 512, 512, NULL, &image);
```

---

## 6. 与基因库对接

本库刻意不认识基因类型，只在数据层面对接。

### 6.1 推荐映射

| 基因库表型 | 本库 3D 图层 | 换算建议 |
|---|---|---|
| 底色 / 斑块颜色 | `base_color` / `color` | 颜色索引经 `texbake_palette_get` 查表 |
| `GENE_LIB_PATTERN_REGION` | `TEXBAKE_LAYER_BLOB3D` | 基因库的 UV 圆心不能直接变成对象空间坐标；建议由主项目按表型/部位生成椭球（如“胸口白斑”固定放在 body_u/高度/朝向的某个区域） |
| `GENE_LIB_PATTERN_STRIPE` | `TEXBAKE_LAYER_STRIPE3D` | 曲线数量 → 通过 `period/width/phase/warp` 表达条纹密度与粗细；条纹方向一般取身体前后轴 |
| `GENE_LIB_PATTERN_GRADIENT` | `TEXBAKE_LAYER_GRADIENT3D` | `param` → `strength`；轴与 `start/end` 由主项目补充（例如鼻尖→尾尖） |
| `GENE_LIB_PATTERN_NOISE` | `TEXBAKE_LAYER_NOISE3D` | `count` → `frequency`（两套都是“格点数”语义）；`param` → `strength`；seed 由猫的稳定标识派生 |
| 局部白斑/重点色 | 3D 图层 + `filter` | 用 `body_u`、`normal_y`、`y`、`box` 限定区域 |

层级顺序保持不变：基因库先画的图层先放入 `layers`。

### 6.2 为什么不再建议 UV 坐标映射

模型的 UV atlas 有 1653 个岛、覆盖率约 60.3%，同一块皮毛被切成大量碎片。
把“身体上的斑块”直接换算成 UV 坐标，必然出现跨岛断裂、错位和拉伸。
3D 图层从几何出发，已经不依赖 UV 布局；主项目只需要决定“表型 → 空间区域”的语义映射。

### 6.3 关于骨骼分部位

`cat.glb` 的蒙皮数据是退化的：`JOINTS_0` 全为 0、`WEIGHTS_0` 恒为 `(1,0,0,0)`，
即所有顶点都绑到 `Root`；同时绑定姿态的骨骼朝向与网格旋转了约 90°。
因此无法用骨骼权重可靠地划分 HEAD/LEG/PAW/TAIL。

本库改用可解释的几何过滤：

- `body_u`：沿身体前后轴归一化，适合头/尾附近、重点色渐变；
- `y_min/y_max`：高度过滤，适合脚掌、腹部；
- `normal_y_min/max`：表面朝向过滤，适合背部/腹部；
- `box_min/box_max`：归一化空间 AABB，适合精确框选局部。

如果未来模型换成权重正常的资产，可在主项目侧把骨骼区域换算成这些过滤参数。

---

## 7. 输出说明

`texbake_bake_glb_surface` 对 GLB 做的事：

1. 找到所有材质 `pbrMetallicRoughness.baseColorTexture`。
2. 加载网格与贴图；按材质对应的源图，只烘焙使用该图的 primitive。
3. 在归一化对象空间求值 3D 图层，按 UV 栅格化；原图按 UV 双线性采样做细节。
4. gutter 膨胀，编码 PNG。
5. 在 BIN 末尾追加 PNG，新增 `bufferView`、`images[]`、`textures[]`。
6. 只改材质 `baseColorTexture.index` 指向新贴图；
   `texCoord`、`KHR_texture_transform` 等原有字段保留。
7. 法线贴图、金属度/粗糙度贴图、节点/骨骼/蒙皮/动画全部原样保留。
8. 多个材质引用同一张源图时，只烘焙一次并共享新贴图。

注意：旧 baseColor 原图不会被删除（避免重排全局索引），因此输出文件会比输入略大。

若传入 `png_path_out`，会额外把**第一张**烘焙贴图写成 PNG（3 通道，无 alpha）。

---

## 8. 参数与效果建议

| 参数 | 建议 |
|---|---|
| `bake_size` | 游戏内常规 1024；特写 2048；4096 会显著增加耗时与内存 |
| `gutter` | 默认 8；贴图较小时可 4，4096 建议 8~16 |
| `detail_strength` | 0.8–1.0 保留毛发；想看纯图案时设 0 |
| `stripe3d.period` | 0.15–0.35（归一化单位）；越小条纹越密 |
| `stripe3d.width` | 0.05–0.12；应小于 `period` |
| `stripe3d.warp` | 0.2–0.5 更自然；0 为笔直条纹 |
| `blob3d.softness` | 0.2–0.4；0 为硬边 |
| `noise3d.frequency` | 8–32 适合银化/烟色 |
| `filter.normal_y_*` | 背部 `min=0.2~0.4`；腹部 `max=-0.2~-0.4` |
| `seed` | 同一只猫固定不变，保证每次烘焙一致 |

已知开销（默认 1024）：

- 网格 1.8 万三角形、覆盖率 60% 时，表面渲染约数百毫秒到 1 秒量级（CPU）；
- 4096 输出时：RGBA 约 67 MB、gutter 队列约 67 MB、原贴图解码约 50 MB，
  峰值约 200 MB 量级；
- `fill_background = 1` 会额外填满整张图，耗时增加但利于 mipmap。

---

## 9. 注意事项

1. **必须调用 `texbake_pattern_init`**，否则 `bake_size`、`detail_strength` 等默认值不生效。
2. 表面 API 只接受 3D 图层；UV 图层请走 `texbake_render` / `texbake_bake_glb`。
3. 启用 `filter` 前先调用 `texbake_filter_init`。
4. 模型必须自带 `TEXCOORD_0` 与 baseColor 贴图；没有 UV 的 primitive 会被跳过，
   全部跳过时返回 `-5` 或 `-4`。
5. 只支持 GLB 内嵌 BIN（`buffers[0]` 无 `uri`）；不支持外部 buffer、sparse accessor。
6. 只解码 stb 支持的格式（JPEG/PNG 等）；压缩纹理（basisu/webp）会被跳过。
7. 表面烘焙使用模型文件的 `POSITION` 空间（绑定姿态），不支持外部动画姿态下的重烘焙。
8. 模型 UV 布局改变（换模型）时，gutter 与图案仍然有效，但建议重新检查
   `stats.coverage` 与 PNG 产物。
9. 本库只处理外观贴图，不改变几何形状；换腿长/尾长/耳形请在主项目切换模型。

---

## 10. 测试与验证

当前测试（`test/main.c`，12 项全部通过）：

- 默认调色板与索引
- UV 平涂、元球区域、贝塞尔条纹、渐变、噪声
- 噪声同种子复现 / 异种子差异
- JSON 解析与重新序列化
- PNG 编码/解码往返
- 表面渲染：`cat.glb` 上 3D 条纹、覆盖率/三角形数/gutter 纹素统计、颜色抽查
- 表面 API 拒绝 UV 图层
- 表面烘焙：新 GLB 结构、PNG 尺寸、法线/粗糙度贴图索引未变
- UV 端到端烘焙回归

测试产物：

- `build/test_cat_surface.glb`
- `build/test_cat_surface.png`
- `build/test_cat_baked.glb`
- `build/test_cat_baked.png`

运行：

```bash
./bin/modelmaking-test.exe
```
