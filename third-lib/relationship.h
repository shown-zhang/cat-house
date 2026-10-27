#ifndef RELATIONSHIP_H
#define RELATIONSHIP_H

#include <stddef.h>
#include <stdint.h>

#define CAT_ID_NONE UINT32_MAX
#define LITTER_NONE UINT32_MAX

typedef uint32_t CatId;
typedef uint32_t LitterId;

/* 一次繁育事件：同一批次的猫互为兄弟姐妹 */
typedef struct {
    CatId    mother;
    CatId    father;
    int32_t  birth_day; /* 相对纪元的天数，-1 表示未知 */
} Litter;

/* 单只猫只保存批次引用与基因，父母和性别均由外部派生 */
typedef struct {
    LitterId litter;
    uint64_t gene; /* 基因表达库据此解析性别等性状 */
} Cat;

typedef struct {
    Cat      *cats;
    uint32_t  cat_len;
    uint32_t  cat_cap;
    Litter   *litters;
    uint32_t  litter_len;
    uint32_t  litter_cap;
} Pedigree;

void pedigree_init(Pedigree *p);
void pedigree_free(Pedigree *p);

LitterId litter_add(Pedigree *p, CatId mother, CatId father, int32_t birth_day);
CatId    cat_add(Pedigree *p, LitterId litter, uint64_t gene);

/* 父母均由批次派生 */
CatId cat_father(const Pedigree *p, CatId id);
CatId cat_mother(const Pedigree *p, CatId id);

/* 以下查询均为扫描派生，不维护索引 */
size_t   cat_children(const Pedigree *p, CatId id, CatId *out, size_t cap);
size_t   cat_siblings(const Pedigree *p, CatId id, CatId *out, size_t cap);
size_t   cat_mates(const Pedigree *p, CatId id, CatId *out, size_t cap);
size_t   cat_litters(const Pedigree *p, CatId mother, LitterId *out, size_t cap);
size_t   cat_ancestors(const Pedigree *p, CatId id, uint32_t max_gen, CatId *out, size_t cap);
size_t   cat_descendants(const Pedigree *p, CatId id, uint32_t max_gen, CatId *out, size_t cap);
uint32_t cat_generation(const Pedigree *p, CatId id);

/* 批量求全部猫的代际，out 长度须为 cat_len；memo 只建一次，避免逐只重复分配 */
void cat_generation_all(const Pedigree *p, uint32_t *out);

/* 返回 0 表示通过；负数表示引用越界或血缘成环 */
int pedigree_check(const Pedigree *p);

int pedigree_save(const Pedigree *p, const char *path);
int pedigree_load(Pedigree *p, const char *path);

#endif
