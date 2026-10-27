#ifndef GENE_LIB_H
#define GENE_LIB_H

/*
 * gene_lib 对外唯一入口。
 *
 * 基因库只负责两件事：
 *   1. 基因生成：随机生成（流浪猫模式）、减数分裂 + 自由组合（培育模式）
 *   2. 性状表达：文本描述（给人看）、机器可读表型（给 3D 模型用）
 *
 * 第三方只需包含本头文件，无需了解 ECS、系统、位点掩码等内部实现。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENE_LIB_MAX_CHILDREN 16

typedef enum {
    GENE_LIB_OK = 0,
    GENE_LIB_ERR_NULL = -1,
    GENE_LIB_ERR_INVALID_GENE = -2, /* 性别位点不合法，无法表达 */
    GENE_LIB_ERR_SEX = -3,          /* 培育时两只猫性别不匹配 */
    GENE_LIB_ERR_BUFFER = -4,       /* 输出缓冲区为空或长度不足 */
} GeneLibStatus;

/* 不透明基因载体：可自由复制/序列化保存，不要解析其内部位含义 */
typedef struct {
    uint64_t value;
} GeneLibGene;

/* 单个位点的基因型：显性等位基因个数 0 / 1 / 2 */
typedef enum {
    GENE_LIB_RECESSIVE = 0,
    GENE_LIB_HETEROZYGOUS = 1,
    GENE_LIB_HOMOZYGOUS = 2,
} GeneLibGenotype;

typedef enum {
    GENE_LIB_MALE = 0,
    GENE_LIB_FEMALE,
} GeneLibSex;

/* 调色板索引：渲染层按此查表取色 */
typedef enum {
    GENE_LIB_COLOR_BLACK = 0,
    GENE_LIB_COLOR_BLUE,
    GENE_LIB_COLOR_CHOCOLATE,
    GENE_LIB_COLOR_LILAC,
    GENE_LIB_COLOR_CINNAMON,
    GENE_LIB_COLOR_FAWN,
    GENE_LIB_COLOR_ORANGE,
    GENE_LIB_COLOR_CREAM,
    GENE_LIB_COLOR_SILVER,
    GENE_LIB_COLOR_WHITE,
} GeneLibColor;

typedef enum {
    GENE_LIB_EAR_STRAIGHT = 0,
    GENE_LIB_EAR_FOLD,
    GENE_LIB_EAR_CURL,
} GeneLibEarShape;

typedef enum {
    GENE_LIB_EYE_GREEN = 0,
    GENE_LIB_EYE_YELLOW,
    GENE_LIB_EYE_AMBER,
    GENE_LIB_EYE_BLUE,
} GeneLibEyeColor;

typedef enum {
    GENE_LIB_HAIR_SHORT = 0,
    GENE_LIB_HAIR_LONG,
} GeneLibHairLength;

typedef enum {
    GENE_LIB_HAIR_TEXTURE_NORMAL = 0,
    GENE_LIB_HAIR_TEXTURE_REX,
    GENE_LIB_HAIR_TEXTURE_HAIRLESS,
} GeneLibHairTexture;

typedef enum {
    GENE_LIB_LEG_NORMAL = 0,
    GENE_LIB_LEG_SHORT,
    GENE_LIB_LEG_SEVERE,
} GeneLibLegLength;

typedef enum {
    GENE_LIB_TAIL_LONG = 0,
    GENE_LIB_TAIL_SHORT,
    GENE_LIB_TAIL_BOB,
} GeneLibTailForm;

/* ---------- 图案几何：供 3D 模型烘焙贴图 ---------- */

#define GENE_LIB_MAX_PATTERN_LAYERS 4
#define GENE_LIB_MAX_META_CIRCLES 6
#define GENE_LIB_MAX_STRIPES 3
#define GENE_LIB_PATTERN_UV_MAX 1023

typedef enum {
    GENE_LIB_PATTERN_REGION = 0, /* 元球不规则区域：白斑、玳瑁斑块 */
    GENE_LIB_PATTERN_STRIPE,     /* 贝塞尔条纹：虎斑 */
    GENE_LIB_PATTERN_GRADIENT,   /* 渐变：重点色 */
    GENE_LIB_PATTERN_NOISE,      /* 噪声调制：银化、烟色 */
} GeneLibPatternKind;

typedef struct {
    int16_t x, y;
    uint16_t r;
} GeneLibMetaCircle;

typedef struct {
    int16_t pts[6]; /* x0,y0, x1,y1, x2,y2 */
    uint8_t w0, w1;
} GeneLibStripeCurve;

typedef struct {
    uint8_t kind;  /* GeneLibPatternKind */
    uint8_t color; /* GeneLibColor */
    uint8_t count; /* region: 圆个数; stripe: 条数; gradient: 范围; noise: 频率 */
    uint8_t param; /* region: 阈值; stripe: 线宽; gradient: 强度; noise: 振幅 */
    union {
        GeneLibMetaCircle circles[GENE_LIB_MAX_META_CIRCLES];
        GeneLibStripeCurve stripes[GENE_LIB_MAX_STRIPES];
    } u;
} GeneLibPatternLayer;

/* 按 layers[0..count-1] 顺序叠加即可得到最终花色 */
typedef struct {
    GeneLibPatternLayer layers[GENE_LIB_MAX_PATTERN_LAYERS];
    uint8_t count;
} GeneLibPatternLayout;

/* ---------- 机器可读表型 ---------- */

typedef struct {
    GeneLibSex sex;
    int alive; /* 1 存活；0 基因死亡（纯合致死等位基因） */

    GeneLibColor base_color;

    GeneLibGenotype orange;     /* 伴性：母猫可为杂合（玳瑁） */
    GeneLibGenotype agouti;
    GeneLibGenotype pattern;
    GeneLibGenotype brown;
    GeneLibGenotype dilution;
    GeneLibGenotype silver;
    GeneLibGenotype colorpoint;
    GeneLibGenotype white;

    GeneLibEarShape ear_shape;
    uint8_t ear_severity; /* 折耳/卷耳的表现程度 0 / 1 / 2 */

    GeneLibEyeColor eye_color;

    GeneLibHairLength hair_length;
    GeneLibHairTexture hair_texture;

    GeneLibLegLength leg_length;
    GeneLibTailForm tail_form;

    GeneLibPatternLayout pattern_layout; /* 给 3D 模型的贴图指令 */
} GeneLibPhenotype;

/* ---------- API ---------- */

/*
 * 线程与重入约束：
 * 本库内部持有全局随机流与推导工作区，所有接口必须在同一线程内串行调用，
 * 不支持多线程并发，也不支持在调用过程中重入。
 */

/**
 * @brief 设置随机数种子（影响随机生成与配子抽样）
 */
void gene_lib_seed(uint64_t seed);

/**
 * @brief 随机生成一只猫（流浪猫模式）
 *
 * @param out 输出基因
 * @return GENE_LIB_OK 成功；负值为 GeneLibStatus
 */
int gene_lib_generate(GeneLibGene* out);

/**
 * @brief 两只猫培育：减数分裂 + 自由组合
 *
 * 子代数量随机，致死组合不会被过滤，由 gene_lib_phenotype 的 alive 字段标记。
 *
 * @param parent_a    亲本 A
 * @param parent_b    亲本 B（必须与 A 异性）
 * @param children    输出子代数组，容量至少为 capacity
 * @param capacity    子代数组容量，最大 GENE_LIB_MAX_CHILDREN
 * @return >0 实际子代数量；0 无子代；负值为 GeneLibStatus
 */
int gene_lib_breed(const GeneLibGene* parent_a, const GeneLibGene* parent_b,
                   GeneLibGene* children, int capacity);

/**
 * @brief 把基因表达成人类可读的文本
 *
 * @param gene 基因
 * @param buf  输出缓冲区
 * @param cap  缓冲区大小（含结尾 '\0'）
 * @return >0 写入的字符数（不含 '\0'）；负值为 GeneLibStatus
 */
int gene_lib_describe(const GeneLibGene* gene, char* buf, size_t cap);

/**
 * @brief 把基因表达成机器可读表型（用于塑造 3D 模型）
 *
 * @param gene 基因
 * @param out  输出表型
 * @return GENE_LIB_OK 成功；负值为 GeneLibStatus
 */
int gene_lib_phenotype(const GeneLibGene* gene, GeneLibPhenotype* out);

#ifdef __cplusplus
}
#endif

#endif /* GENE_LIB_H */
