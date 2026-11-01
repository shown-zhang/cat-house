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
| 指令化贴图烘焙 | 底色 + 最多 6 个图层，按顺序叠加，输出 RGBA 贴图 |
| 四类绘制图元 | 元球区域、贝塞尔条纹、线性渐变、值噪声 |
| 模型贴图细节保留 | 以原模型 baseColor 贴图为明暗基底相乘，保留毛发/阴影细节 |
| GLB 原地改造 | 读取 GLB，追加 PNG，改写材质 baseColorTexture，其余数据原样保留 |
| 独立渲染 | `texbake_render` 不碰模型，可直接生成图鉴缩略图/预览图 |
| 多材质处理 | 每个使用 baseColorTexture 的材质都会烘焙；同一张源图只烘焙一次 |
| PNG 导出 | 可选，便于检查烘焙结果 |
| 可复现 | 噪声带种子；同一组输入结果一致 |

### 不做什么（明确边界）

- 不改模型形状：短腿/断尾/折耳等由主项目切换不同模型，本库只负责贴图。
- 不做 UV 展开/重排：图案直接画在与原 baseColor 贴图相同的 UV 空间里，依赖模型自带 UV。
- 不做色彩空间转换：按 sRGB 字节值直接相乘，属于游戏常用近似。
- 不处理外部贴图依赖：不支持 `images[].uri`、KHR_texture_basisu 等本库无法解码的贴图。
- 不负责渲染显示：只产出 GLB/PNG 数据。

---

## 2. 文件与构建

```
include/texbake.h        公共头文件（对接只需要它）
src/texbake.c            图层光栅化 + 烘焙主编排
src/tb_glb.c/.h          GLB 读写（内部）
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

### 3.1 坐标系

- 绘制指令坐标空间固定为 `0..1024`，即 `TEXBAKE_UV_SCALE`。
- `坐标 / 1024 = UV`，`x` 向右，`y` 向下（与贴图第一行一致）。
- 换任意 `bake_size` 输出时坐标含义不变，图案会等比映射。

### 3.2 图层叠加

```
base_color  →  layers[0]  →  layers[1]  → ...  →  乘原贴图细节  →  输出
```

图层顺序就是数组顺序，后画的覆盖先画的。

### 3.3 细节保留

`detail_strength` 控制原模型 baseColor 贴图的明暗参与程度：

```
输出 = 图案色 × (1 - d + d × 原贴图亮度 / 255)
```

- `d = 1`：完整保留原贴图明暗（推荐，毛发细节明显）
- `d = 0`：完全平涂，忽略原贴图
- 原贴图为白色基底时，`d = 1` 相当于用图案色给毛发“上色”

### 3.4 输出尺寸

- `bake_size = 0`：默认 `TEXBAKE_DEFAULT_SIZE`（1024）
- 显式指定：`1..TEXBAKE_MAX_SIZE`（4096）
- 尺寸为正方形；原 4096 贴图会先解码再缩放到目标尺寸

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

typedef struct {
    TexbakeColor base_color;          // 打底色
    TexbakeLayer layers[TEXBAKE_MAX_LAYERS];
    int layer_count;
    float detail_strength;            // 0..1，默认 1
    int bake_size;                    // 0 = 1024
} TexbakePattern;
```

单个图层：

```c
typedef struct {
    TexbakeLayerKind kind;            // REGION / STRIPE / GRADIENT / NOISE
    TexbakeColor color;               // 该图层颜色
    float strength;                   // gradient / noise 的强度 0..1，0 表示不生效

    // kind == REGION
    TexbakeBlob blobs[TEXBAKE_MAX_BLOBS];   // {x, y, radius}，坐标 0..1024
    int blob_count;                          // 0..8
    float threshold;                         // 0..1，<=0 时按 0.5，越大区域越小

    // kind == STRIPE
    TexbakeCurve curves[TEXBAKE_MAX_CURVES]; // 二次贝塞尔：x0y0 / x1y1 / x2y2 + w0/w1
    int curve_count;                         // 0..6

    // kind == GRADIENT
    float gx0, gy0, gx1, gy1;                // 渐变轴，起点为底色，终点为该图层色

    // kind == NOISE
    float frequency;                         // 每 UV 单位的噪声格点数，<=0 不生效
    uint32_t seed;                           // 噪声种子
} TexbakeLayer;
```

枚举：

| 枚举 | 值 |
|---|---|
| `TexbakeLayerKind` | `TEXBAKE_LAYER_REGION` / `STRIPE` / `GRADIENT` / `NOISE` |
| `TexbakePaletteIndex` | `BLACK, BLUE, CHOCOLATE, LILAC, CINNAMON, FAWN, ORANGE, CREAM, SILVER, WHITE`（顺序与原基因库颜色索引一致） |
| `TexbakeStatus` | `0` 成功；`-1..-6` 见 4.3 |

### 4.2 函数

```c
void  texbake_pattern_init(TexbakePattern* pattern);   // 必须先调用，建立默认值
void  texbake_default_palette(TexbakePalette* out);    // 默认 10 色调色板
TexbakeColor texbake_palette_get(const TexbakePalette*, int index); // 越界返回白色

// 仅渲染图案，不碰模型；detail_rgb 可为 NULL（平涂），否则必须是 width*height*3
int texbake_render(const TexbakePattern* pattern,
                   int width, int height,
                   const uint8_t* detail_rgb,
                   TexbakeImage* out);

// 读取 GLB → 烘焙 → 写新 GLB；png_path_out 可为 NULL
int texbake_bake_glb(const char* glb_path_in,
                     const char* glb_path_out,
                     const char* png_path_out,
                     const TexbakePattern* pattern);

void texbake_image_free(TexbakeImage* image);           // 释放 texbake_render 结果
```

`TexbakeImage`：`width / height / rgba`，RGBA 行优先、顶部起始、A 恒为 255。

### 4.3 返回码

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `TEXBAKE_OK` | 成功 |
| -1 | `TEXBAKE_ERR_NULL` | 必填指针为空 |
| -2 | `TEXBAKE_ERR_INVALID_ARG` | 尺寸/图层数量/字段越界 |
| -3 | `TEXBAKE_ERR_IO` | 读写文件失败 |
| -4 | `TEXBAKE_ERR_FORMAT` | GLB/JSON/贴图解析失败或结构不支持 |
| -5 | `TEXBAKE_ERR_NO_BASE_TEXTURE` | 没有可烘焙的 baseColorTexture，或全部源图解码失败 |
| -6 | `TEXBAKE_ERR_OOM` | 内存分配失败 |

---

## 5. 最小使用示例

```c
#include "texbake.h"

int bake_example(void)
{
    TexbakePattern pattern;
    texbake_pattern_init(&pattern);                 // 白底、detail_strength=1、1024
    pattern.base_color = (TexbakeColor){ 222, 132, 58 }; // 橘
    pattern.bake_size = 1024;

    /* 一条横向虎斑：从左边中点弯到右边中点 */
    pattern.layer_count = 1;
    TexbakeLayer* stripe = &pattern.layers[0];
    stripe->kind = TEXBAKE_LAYER_STRIPE;
    stripe->color = (TexbakeColor){ 30, 28, 26 };
    stripe->curve_count = 1;
    stripe->curves[0] = (TexbakeCurve){
        0.0f, 512.0f,      // 起点
        512.0f, 300.0f,    // 控制点
        1024.0f, 512.0f,   // 终点
        70.0f, 70.0f       // 两端线宽（绘制坐标单位）
    };

    return texbake_bake_glb("assets/cat.glb", "cat_baked.glb", "cat_baked.png", &pattern);
}
```

只做图鉴预览、不需要 GLB 时：

```c
TexbakeImage image = {0};
int status = texbake_render(&pattern, 512, 512, NULL, &image);
if (status == TEXBAKE_OK) {
    // image.rgba 可直接上传 GPU / 编码 PNG
    texbake_image_free(&image);
}
```

> 注意：`texbake_pattern_init` 会把整个 pattern（含所有图层）清零。
> 后续手动追加图层时，请先 `memset` 该图层或用 `= (TexbakeLayer){0}` 清零，避免残留字段。
> `TexbakeLayer` 优先使用指定初始化，未填写的字段务必为 0。

---

## 6. 与基因库对接

本库刻意不认识基因类型，只在数据层面对接。基因库的表型输出里与外观相关的是：

- `base_color`
- `pattern_layout.layers[0..count-1]`（4 类图元）
- 颜色索引（`GeneLibColor`）

### 6.1 映射表

| 基因库图层 | 本库图层 | 字段换算建议 |
|---|---|---|
| `GENE_LIB_PATTERN_REGION` | `TEXBAKE_LAYER_REGION` | `circles[k].x/y/r` → `blobs[k].x/y/radius`（同为 0..1023/0..1024 坐标）；`count` → `blob_count`；`param` → `threshold = param / 255.0f` |
| `GENE_LIB_PATTERN_STRIPE` | `TEXBAKE_LAYER_STRIPE` | `stripes[k].pts[6]` → `curves[k].x0..y2`；`w0/w1`（0..255）→ 线宽（0..1024 坐标单位），示例按 ×4 放大，可按美术效果调整 |
| `GENE_LIB_PATTERN_GRADIENT` | `TEXBAKE_LAYER_GRADIENT` | `param` → `strength = param / 255.0f`；**渐变轴需要调用方补充**（基因库只给了范围/强度，没有位置信息） |
| `GENE_LIB_PATTERN_NOISE` | `TEXBAKE_LAYER_NOISE` | `count` → `frequency`（都是“格点数”语义）；`param` → `strength = param / 255.0f`；`seed` 建议由猫的稳定标识派生，保证同一只猫纹理不变 |
| `color` / `base_color` | `TexbakeColor` | `texbake_palette_get(&palette, index)` 查默认调色板；也可换成你们自己的调色板 |

层级顺序保持不变：基因库先画的图层先放入 `layers`。

### 6.2 适配示例

```c
#include "texbake.h"
#include "gene_lib.h"   // 主项目里的基因库

static int bake_from_phenotype(const GeneLibPhenotype* ph,
                               const char* src_glb,
                               const char* dst_glb,
                               const char* dst_png)
{
    TexbakePalette palette;
    texbake_default_palette(&palette);

    TexbakePattern pattern;
    texbake_pattern_init(&pattern);
    pattern.base_color = texbake_palette_get(&palette, (int)ph->base_color);
    pattern.bake_size = 1024;

    for (int i = 0; i < ph->pattern_layout.count && i < TEXBAKE_MAX_LAYERS; ++i) {
        const GeneLibPatternLayer* src = &ph->pattern_layout.layers[i];
        TexbakeLayer* dst = &pattern.layers[pattern.layer_count];
        *dst = (TexbakeLayer){0};
        dst->kind = (TexbakeLayerKind)src->kind;
        dst->color = texbake_palette_get(&palette, (int)src->color);

        switch (dst->kind) {
        case TEXBAKE_LAYER_REGION: {
            int n = src->count;
            if (n > TEXBAKE_MAX_BLOBS) n = TEXBAKE_MAX_BLOBS;
            dst->blob_count = n;
            for (int k = 0; k < n; ++k) {
                dst->blobs[k].x = (float)src->u.circles[k].x;
                dst->blobs[k].y = (float)src->u.circles[k].y;
                dst->blobs[k].radius = (float)src->u.circles[k].r;
            }
            dst->threshold = src->param / 255.0f;
            break;
        }
        case TEXBAKE_LAYER_STRIPE: {
            int n = src->count;
            if (n > TEXBAKE_MAX_CURVES) n = TEXBAKE_MAX_CURVES;
            dst->curve_count = n;
            for (int k = 0; k < n; ++k) {
                const GeneLibStripeCurve* c = &src->u.stripes[k];
                dst->curves[k].x0 = (float)c->pts[0];
                dst->curves[k].y0 = (float)c->pts[1];
                dst->curves[k].x1 = (float)c->pts[2];
                dst->curves[k].y1 = (float)c->pts[3];
                dst->curves[k].x2 = (float)c->pts[4];
                dst->curves[k].y2 = (float)c->pts[5];
                dst->curves[k].w0 = (float)c->w0 * 4.0f; // 0..255 → 0..1020 坐标
                dst->curves[k].w1 = (float)c->w1 * 4.0f;
            }
            break;
        }
        case TEXBAKE_LAYER_GRADIENT:
            dst->strength = src->param / 255.0f;
            /* 轴由主项目决定；下例为自上而下 */
            dst->gx0 = 0.0f; dst->gy0 = 0.0f;
            dst->gx1 = 0.0f; dst->gy1 = 1024.0f;
            break;
        case TEXBAKE_LAYER_NOISE:
            dst->frequency = (float)src->count;
            dst->strength = src->param / 255.0f;
            dst->seed = 12345u; /* 建议改成由猫基因/ID 派生 */
            break;
        default:
            continue;
        }
        pattern.layer_count++;
    }

    return texbake_bake_glb(src_glb, dst_glb, dst_png, &pattern);
}
```

### 6.3 语义差异提醒

- 基因库的 UV 上限是 `1023`，本库按 `1024` 做归一化，二者视觉上等价，直接传原坐标即可。
- 基因库的渐变只有“范围/强度”，本库需要显式的渐变轴；如果希望重点色按模型部位加深，
  需要主项目自己生成一条合适的 UV 轴（或按部位拆分多段渐变）。
- 条纹线宽在基因库里是 `uint8`，本库是浮点绘制坐标；建议统一换算，避免同一基因档位
  在两套系统中视觉宽度不一致。

---

## 7. 输出说明

`texbake_bake_glb` 对 GLB 做的事：

1. 找到所有材质 `pbrMetallicRoughness.baseColorTexture`。
2. 解码其源图（JPEG/PNG），缩放到 `bake_size`，作为明暗细节基底。
3. 按 pattern 渲染图案，乘上细节，编码 PNG。
4. 在 BIN 末尾追加 PNG，新增 `bufferView`、`images[]`、`textures[]`。
5. 只改材质 `baseColorTexture.index` 指向新贴图；
   `texCoord`、`KHR_texture_transform` 等原有字段保留。
6. 法线贴图、金属度/粗糙度贴图、节点/骨骼/蒙皮/动画全部原样保留。
7. 多个材质引用同一张源图时，只烘焙一次并共享新贴图。

注意：旧 baseColor 原图不会被删除（避免重排全局索引），因此输出文件会比输入略大。

若传入 `png_path_out`，会额外把**第一张**烘焙贴图写成 PNG（3 通道，无 alpha）。

---

## 8. 参数与效果建议

| 参数 | 建议 |
|---|---|
| `detail_strength` | 0.8–1.0；想要更卡通/更平的风格可降到 0.4–0.7 |
| `bake_size` | 游戏内常规 1024；需要特写可 2048；4096 会显著增加解码与编码内存/CPU |
| region `threshold` | 0.3–0.6 常用；阈值越大区域越小；边缘自带柔化 |
| stripe 线宽 | 与 `bake_size` 无关，按绘制坐标给定（0..1024） |
| noise `frequency` | 8–32 适合银化/烟色；过低会成大块，过高会成噪点 |
| noise `seed` | 同一只猫固定不变，才能保证每次烘焙一致 |

内存峰值：解码原图（4096² RGB 约 48 MB）+ 输出尺寸的 RGBA 与图层缓冲。
默认 1024 输出时，整条链路峰值大约 60 MB 量级。

---

## 9. 注意事项

1. **必须调用 `texbake_pattern_init`**，否则 `bake_size`、`detail_strength` 等默认值不生效。
2. 图层里没有用到的字段保持 0；例如 stripe 图层不要残留 `threshold`/`frequency`。
3. 模型必须自带 UV 与 baseColor 贴图；没有 baseColorTexture 的材质会被跳过，
   全部跳过时返回 `-5`。
4. 只支持 GLB 内嵌 BIN（`buffers[0]` 无 `uri`）；不支持外部 buffer。
5. 只解码 stb 支持的格式（JPEG/PNG 等）；压缩纹理（basisu/webp）会被跳过。
6. 图案与原贴图共用同一套 UV，模型 UV 布局改变（换模型）时需要重新确认贴图对齐。
7. 本库内部无全局可变状态，可并发烘焙不同任务；但同一输出文件需由调用方串行写入。
8. 本库只处理外观贴图，不改变几何形状；换腿长/尾长/耳形请在主项目切换模型。

---

## 10. 测试与验证

当前测试（`test/main.c`，9 项全部通过）：

- 默认调色板与索引
- 平涂渲染
- 元球区域、贝塞尔条纹、渐变、噪声
- 噪声同种子复现 / 异种子差异
- JSON 解析与重新序列化
- PNG 编码/解码往返
- `assets/cat.glb` 端到端烘焙：校验新 GLB 结构、PNG 贴图尺寸、
  原法线/粗糙度贴图索引未变、原有图片字节未变

测试产物：

- `build/test_cat_baked.glb`
- `build/test_cat_baked.png`

运行：

```bash
./bin/modelmaking-test.exe
```
