#include "model.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cglm/cglm.h>
#include <cgltf.h>
#include <stb_image.h>

/* 把 GLB 内嵌图片解码成 RGBA8（stb 输出原点在左上，与 glTF UV 一致） */
static int decode_image(const cgltf_image *image, ModelTexture *out)
{
    if (!image || !image->buffer_view || !image->buffer_view->buffer ||
        !image->buffer_view->buffer->data) {
        return -1;
    }

    const cgltf_buffer_view *view = image->buffer_view;
    const uint8_t *data = (const uint8_t *)view->buffer->data + view->offset;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *rgba = stbi_load_from_memory(data, (int)view->size,
                                                &width, &height, &channels, 4);
    if (!rgba || width <= 0 || height <= 0) {
        stbi_image_free(rgba);
        return -1;
    }

    out->pixels = (uint8_t *)rgba;
    out->width = width;
    out->height = height;
    return 0;
}

/* 取出指定类型的第一个属性 accessor */
static const cgltf_accessor *find_attribute(const cgltf_primitive *primitive,
                                            cgltf_attribute_type type)
{
    for (size_t i = 0; i < primitive->attributes_count; ++i) {
        if (primitive->attributes[i].type == type) {
            return primitive->attributes[i].data;
        }
    }
    return NULL;
}

static int fill_primitive(const cgltf_primitive *primitive, ModelPrimitive *out,
                          int32_t texture_index)
{
    memset(out, 0, sizeof(*out));
    out->texture = texture_index;

    const cgltf_accessor *positions = find_attribute(primitive, cgltf_attribute_type_position);
    if (!positions || positions->count == 0 || positions->type != cgltf_type_vec3) {
        return -1;
    }

    const size_t vertex_count = positions->count;
    out->vertices = calloc(vertex_count, sizeof(ModelVertex));
    if (!out->vertices) {
        return -1;
    }
    out->vertex_count = (uint32_t)vertex_count;

    float *buffer = malloc(vertex_count * 3 * sizeof(float));
    if (!buffer) {
        return -1;
    }

    if (cgltf_accessor_unpack_floats(positions, buffer, vertex_count * 3) < vertex_count * 3) {
        free(buffer);
        return -1;
    }
    for (size_t i = 0; i < vertex_count; ++i) {
        out->vertices[i].position[0] = buffer[i * 3 + 0];
        out->vertices[i].position[1] = buffer[i * 3 + 1];
        out->vertices[i].position[2] = buffer[i * 3 + 2];
    }

    const cgltf_accessor *normals = find_attribute(primitive, cgltf_attribute_type_normal);
    if (normals && normals->count >= vertex_count && normals->type == cgltf_type_vec3) {
        if (cgltf_accessor_unpack_floats(normals, buffer, vertex_count * 3) >= vertex_count * 3) {
            for (size_t i = 0; i < vertex_count; ++i) {
                out->vertices[i].normal[0] = buffer[i * 3 + 0];
                out->vertices[i].normal[1] = buffer[i * 3 + 1];
                out->vertices[i].normal[2] = buffer[i * 3 + 2];
            }
        }
    } else {
        for (size_t i = 0; i < vertex_count; ++i) {
            out->vertices[i].normal[0] = 0.0f;
            out->vertices[i].normal[1] = 0.0f;
            out->vertices[i].normal[2] = 1.0f;
        }
    }

    const cgltf_accessor *texcoords = find_attribute(primitive, cgltf_attribute_type_texcoord);
    if (texcoords && texcoords->count >= vertex_count && texcoords->type == cgltf_type_vec2) {
        free(buffer);
        buffer = malloc(vertex_count * 2 * sizeof(float));
        if (!buffer) {
            return -1;
        }
        if (cgltf_accessor_unpack_floats(texcoords, buffer, vertex_count * 2) >= vertex_count * 2) {
            for (size_t i = 0; i < vertex_count; ++i) {
                out->vertices[i].uv[0] = buffer[i * 2 + 0];
                out->vertices[i].uv[1] = buffer[i * 2 + 1];
            }
        }
    }

    const size_t index_count = primitive->indices ? primitive->indices->count : vertex_count;
    out->indices = malloc(index_count * sizeof(uint32_t));
    if (!out->indices) {
        free(buffer);
        return -1;
    }
    out->index_count = (uint32_t)index_count;
    for (size_t i = 0; i < index_count; ++i) {
        out->indices[i] = primitive->indices
            ? (uint32_t)cgltf_accessor_read_index(primitive->indices, i)
            : (uint32_t)i;
    }

    if (primitive->material) {
        memcpy(out->base_color,
               primitive->material->pbr_metallic_roughness.base_color_factor,
               sizeof(out->base_color));
    } else {
        out->base_color[0] = 1.0f;
        out->base_color[1] = 1.0f;
        out->base_color[2] = 1.0f;
        out->base_color[3] = 1.0f;
    }

    free(buffer);
    return 0;
}

/* 计算世界空间包围球 */
static void compute_bounds(Model *model)
{
    if (model->item_count == 0) {
        return;
    }

    vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
    vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (size_t i = 0; i < model->item_count; ++i) {
        const ModelDrawItem *item = &model->items[i];
        if (item->primitive >= model->primitive_count) {
            continue;
        }
        const ModelPrimitive *primitive = &model->primitives[item->primitive];

        mat4 transform;
        memcpy(transform, item->transform, sizeof(transform));

        for (uint32_t v = 0; v < primitive->vertex_count; ++v) {
            vec4 point = {primitive->vertices[v].position[0],
                          primitive->vertices[v].position[1],
                          primitive->vertices[v].position[2],
                          1.0f};
            glm_mat4_mulv(transform, point, point);
            for (int axis = 0; axis < 3; ++axis) {
                if (point[axis] < min[axis]) {
                    min[axis] = point[axis];
                }
                if (point[axis] > max[axis]) {
                    max[axis] = point[axis];
                }
            }
        }
    }

    if (min[0] == FLT_MAX) {
        return;
    }

    model->center[0] = (min[0] + max[0]) * 0.5f;
    model->center[1] = (min[1] + max[1]) * 0.5f;
    model->center[2] = (min[2] + max[2]) * 0.5f;

    const float dx = (max[0] - min[0]) * 0.5f;
    const float dy = (max[1] - min[1]) * 0.5f;
    const float dz = (max[2] - min[2]) * 0.5f;
    model->radius = sqrtf(dx * dx + dy * dy + dz * dz);
}

int model_load(Model *model, const char *path)
{
    if (!model || !path) {
        return -1;
    }
    memset(model, 0, sizeof(*model));

    cgltf_options options = {0};
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[model] 解析失败：%s\n", path);
        return -1;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        fprintf(stderr, "[model] 读取 buffer 失败：%s\n", path);
        cgltf_free(data);
        return -1;
    }

    size_t primitive_count = 0;
    for (size_t m = 0; m < data->meshes_count; ++m) {
        primitive_count += data->meshes[m].primitives_count;
    }

    /* 每个 mesh 的第一个 primitive 在 model->primitives 中的下标 */
    size_t *mesh_base = calloc(data->meshes_count ? data->meshes_count : 1, sizeof(size_t));
    int32_t *image_to_texture = NULL;
    if (data->images_count > 0) {
        image_to_texture = malloc(data->images_count * sizeof(int32_t));
        if (!image_to_texture) {
            free(mesh_base);
            cgltf_free(data);
            return -1;
        }
        for (size_t i = 0; i < data->images_count; ++i) {
            image_to_texture[i] = -1;
        }
    }

    size_t offset = 0;
    for (size_t m = 0; m < data->meshes_count; ++m) {
        mesh_base[m] = offset;
        offset += data->meshes[m].primitives_count;
    }

    if (primitive_count > 0) {
        model->primitives = calloc(primitive_count, sizeof(ModelPrimitive));
        if (!model->primitives) {
            free(mesh_base);
            free(image_to_texture);
            cgltf_free(data);
            return -1;
        }
    }
    if (data->images_count > 0) {
        model->textures = calloc(data->images_count, sizeof(ModelTexture));
        if (!model->textures) {
            free(mesh_base);
            free(image_to_texture);
            model_free(model);
            cgltf_free(data);
            return -1;
        }
    }

    /* 图元：几何 + 材质 */
    size_t pi = 0;
    for (size_t m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh *mesh = &data->meshes[m];
        for (size_t p = 0; p < mesh->primitives_count; ++p) {
            const cgltf_primitive *src = &mesh->primitives[p];

            int32_t texture_index = -1;
            if (src->material &&
                src->material->pbr_metallic_roughness.base_color_texture.texture) {
                const cgltf_texture *texture =
                    src->material->pbr_metallic_roughness.base_color_texture.texture;
                if (texture->image) {
                    const size_t image_index = (size_t)(texture->image - data->images);
                    if (image_index < data->images_count) {
                        if (image_to_texture[image_index] < 0) {
                            ModelTexture decoded;
                            memset(&decoded, 0, sizeof(decoded));
                            if (decode_image(texture->image, &decoded) == 0) {
                                image_to_texture[image_index] = (int32_t)model->texture_count;
                                model->textures[model->texture_count] = decoded;
                                model->texture_count++;
                            }
                        }
                        texture_index = image_to_texture[image_index];
                    }
                }
            }

            model->primitive_count = pi + 1;
            if (fill_primitive(src, &model->primitives[pi], texture_index) != 0) {
                fprintf(stderr, "[model] 图元解析失败（mesh %zu, primitive %zu）\n", m, p);
                free(mesh_base);
                free(image_to_texture);
                model_free(model);
                cgltf_free(data);
                return -1;
            }
            pi++;
        }
    }

    /* 绘制项：节点世界矩阵 × 图元 */
    size_t item_count = 0;
    for (size_t i = 0; i < data->nodes_count; ++i) {
        const cgltf_node *node = &data->nodes[i];
        if (node->mesh) {
            item_count += node->mesh->primitives_count;
        }
    }
    if (item_count == 0) {
        item_count = primitive_count;
    }

    if (item_count > 0) {
        model->items = calloc(item_count, sizeof(ModelDrawItem));
        if (!model->items) {
            free(mesh_base);
            free(image_to_texture);
            model_free(model);
            cgltf_free(data);
            return -1;
        }

        for (size_t i = 0; i < data->nodes_count; ++i) {
            const cgltf_node *node = &data->nodes[i];
            if (!node->mesh) {
                continue;
            }
            const size_t mesh_index = (size_t)(node->mesh - data->meshes);
            if (mesh_index >= data->meshes_count) {
                continue;
            }

            float world[16];
            cgltf_node_transform_world(node, world);

            for (size_t k = 0; k < node->mesh->primitives_count; ++k) {
                ModelDrawItem *item = &model->items[model->item_count];
                memcpy(item->transform, world, sizeof(item->transform));
                item->primitive = (uint32_t)(mesh_base[mesh_index] + k);
                model->item_count++;
            }
        }

        /* 没有任何节点引用 mesh 时，按单位矩阵兜底绘制全部图元 */
        if (model->item_count == 0) {
            for (size_t i = 0; i < primitive_count; ++i) {
                ModelDrawItem *item = &model->items[model->item_count];
                mat4 identity;
                glm_mat4_identity(identity);
                memcpy(item->transform, identity, sizeof(item->transform));
                item->primitive = (uint32_t)i;
                model->item_count++;
            }
        }
    }

    compute_bounds(model);

    free(mesh_base);
    free(image_to_texture);
    cgltf_free(data);
    return 0;
}

void model_free(Model *model)
{
    if (!model) {
        return;
    }

    for (size_t i = 0; i < model->primitive_count; ++i) {
        free(model->primitives[i].vertices);
        free(model->primitives[i].indices);
    }
    for (size_t i = 0; i < model->texture_count; ++i) {
        stbi_image_free(model->textures[i].pixels);
    }

    free(model->primitives);
    free(model->textures);
    free(model->items);
    memset(model, 0, sizeof(*model));
}
