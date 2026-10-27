#include "model.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>
#include <cgltf.h>
#include <stb_image.h>

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
} Vertex;

/* 把 glTF 内嵌图片上传成 GL 贴图；失败返回 0 */
static uint32_t upload_texture(const cgltf_texture_view *view)
{
    if (!view || !view->texture || !view->texture->image) {
        return 0;
    }

    const cgltf_image *image = view->texture->image;
    if (!image->buffer_view) {
        fprintf(stderr, "[model] 暂不支持外部贴图 URI：%s\n",
                image->uri ? image->uri : "(null)");
        return 0;
    }

    const uint8_t *bytes = cgltf_buffer_view_data(image->buffer_view);
    if (!bytes) {
        return 0;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(bytes, (int)image->buffer_view->size,
                                                  &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[model] 贴图解码失败：%s\n", stbi_failure_reason());
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    const cgltf_sampler *sampler = view->texture->sampler;
    GLint wrap_s = GL_REPEAT;
    GLint wrap_t = GL_REPEAT;
    GLint min_filter = GL_LINEAR_MIPMAP_LINEAR;
    GLint mag_filter = GL_LINEAR;
    if (sampler) {
        wrap_s = (GLint)sampler->wrap_s;
        wrap_t = (GLint)sampler->wrap_t;
        if (sampler->min_filter) {
            min_filter = (GLint)sampler->min_filter;
        }
        if (sampler->mag_filter) {
            mag_filter = (GLint)sampler->mag_filter;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return texture;
}

static const cgltf_accessor *find_attribute(const cgltf_primitive *primitive,
                                            cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
        const cgltf_attribute *attribute = &primitive->attributes[i];
        if (attribute->type == type && attribute->index == 0) {
            return attribute->data;
        }
    }
    return NULL;
}

/* 把一个图元展开成顶点数组并上传 GPU，同时算出局部包围盒 */
static void build_primitive(const cgltf_primitive *primitive, ModelPrimitive *out,
                            vec3 local_min, vec3 local_max)
{
    memset(out, 0, sizeof(*out));
    out->base_color[0] = 1.0f;
    out->base_color[1] = 1.0f;
    out->base_color[2] = 1.0f;
    out->base_color[3] = 1.0f;

    glm_vec3_fill(local_min, FLT_MAX);
    glm_vec3_fill(local_max, -FLT_MAX);

    if (primitive->type != cgltf_primitive_type_triangles) {
        fprintf(stderr, "[model] 跳过非三角形图元\n");
        return;
    }

    const cgltf_accessor *position = find_attribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor *normal = find_attribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor *texcoord = find_attribute(primitive, cgltf_attribute_type_texcoord);
    if (!position || position->count == 0) {
        fprintf(stderr, "[model] 图元缺少 POSITION\n");
        return;
    }

    const size_t vertex_count = (size_t)position->count;

    float *positions = malloc(sizeof(float) * vertex_count * 3);
    float *normals = calloc(vertex_count * 3, sizeof(float));
    float *uvs = calloc(vertex_count * 2, sizeof(float));
    Vertex *vertices = malloc(sizeof(Vertex) * vertex_count);
    uint32_t *indices = NULL;
    size_t index_count = 0;

    if (!positions || !normals || !uvs || !vertices) {
        fprintf(stderr, "[model] 内存不足\n");
        goto done;
    }

    cgltf_accessor_unpack_floats(position, positions, vertex_count * 3);
    if (normal) {
        cgltf_accessor_unpack_floats(normal, normals, vertex_count * 3);
    } else {
        for (size_t i = 0; i < vertex_count; ++i) {
            normals[i * 3 + 1] = 1.0f;
        }
    }
    if (texcoord) {
        cgltf_accessor_unpack_floats(texcoord, uvs, vertex_count * 2);
    }

    for (size_t i = 0; i < vertex_count; ++i) {
        memcpy(vertices[i].position, positions + i * 3, sizeof(float) * 3);
        memcpy(vertices[i].normal, normals + i * 3, sizeof(float) * 3);
        memcpy(vertices[i].uv, uvs + i * 2, sizeof(float) * 2);

        for (int axis = 0; axis < 3; ++axis) {
            float value = positions[i * 3 + axis];
            if (value < local_min[axis]) {
                local_min[axis] = value;
            }
            if (value > local_max[axis]) {
                local_max[axis] = value;
            }
        }
    }

    if (primitive->indices && primitive->indices->count > 0) {
        index_count = (size_t)primitive->indices->count;
        indices = malloc(sizeof(uint32_t) * index_count);
        if (!indices) {
            fprintf(stderr, "[model] 内存不足\n");
            goto done;
        }
        cgltf_accessor_unpack_indices(primitive->indices, indices,
                                      sizeof(uint32_t), index_count);
    } else {
        index_count = vertex_count;
        indices = malloc(sizeof(uint32_t) * index_count);
        if (!indices) {
            fprintf(stderr, "[model] 内存不足\n");
            goto done;
        }
        for (size_t i = 0; i < index_count; ++i) {
            indices[i] = (uint32_t)i;
        }
    }

    glGenVertexArrays(1, &out->vao);
    glGenBuffers(1, &out->vbo);
    glGenBuffers(1, &out->ebo);

    glBindVertexArray(out->vao);

    glBindBuffer(GL_ARRAY_BUFFER, out->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * vertex_count),
                 vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * index_count),
                 indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (const void *)offsetof(Vertex, uv));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    out->index_count = (uint32_t)index_count;

    const cgltf_material *material = primitive->material;
    if (material && material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr = &material->pbr_metallic_roughness;
        memcpy(out->base_color, pbr->base_color_factor, sizeof(float) * 4);
        out->texture = upload_texture(&pbr->base_color_texture);
    }

done:
    free(positions);
    free(normals);
    free(uvs);
    free(vertices);
    free(indices);
}

int model_load(Model *model, const char *path)
{
    memset(model, 0, sizeof(*model));

    cgltf_options options;
    memset(&options, 0, sizeof(options));

    cgltf_data *data = NULL;
    vec3 *local_min = NULL;
    vec3 *local_max = NULL;
    size_t *mesh_first = NULL;
    int result = -1;

    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[model] 解析失败：%s\n", path);
        goto out;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        fprintf(stderr, "[model] 读取二进制缓冲区失败：%s\n", path);
        goto out;
    }

    size_t primitive_count = 0;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        primitive_count += (size_t)data->meshes[m].primitives_count;
    }

    model->primitives = calloc(primitive_count ? primitive_count : 1,
                               sizeof(ModelPrimitive));
    local_min = calloc(primitive_count ? primitive_count : 1, sizeof(vec3));
    local_max = calloc(primitive_count ? primitive_count : 1, sizeof(vec3));
    mesh_first = calloc(data->meshes_count ? (size_t)data->meshes_count : 1,
                        sizeof(size_t));
    if (!model->primitives || !local_min || !local_max || !mesh_first) {
        fprintf(stderr, "[model] 内存不足\n");
        goto out;
    }

    size_t next = 0;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        mesh_first[m] = next;
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            build_primitive(&data->meshes[m].primitives[p], &model->primitives[next],
                            local_min[next], local_max[next]);
            ++next;
        }
    }
    model->primitive_count = next;

    size_t item_count = 0;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        if (data->nodes[n].mesh) {
            item_count += (size_t)data->nodes[n].mesh->primitives_count;
        }
    }
    model->items = calloc(item_count ? item_count : 1, sizeof(ModelDrawItem));
    if (!model->items) {
        fprintf(stderr, "[model] 内存不足\n");
        goto out;
    }

    vec3 world_min;
    vec3 world_max;
    glm_vec3_fill(world_min, FLT_MAX);
    glm_vec3_fill(world_max, -FLT_MAX);

    size_t item = 0;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->mesh) {
            continue;
        }
        const size_t mesh_index = (size_t)(node->mesh - data->meshes);
        mat4 world;
        cgltf_node_transform_world(node, (cgltf_float *)world);

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const uint32_t primitive = (uint32_t)(mesh_first[mesh_index] + p);
            glm_mat4_copy(world, model->items[item].transform);
            model->items[item].primitive = primitive;
            ++item;

            if (model->primitives[primitive].index_count == 0) {
                continue;
            }
            /* 用局部包围盒的 8 个角点更新整体包围盒 */
            for (int corner = 0; corner < 8; ++corner) {
                vec3 point = {
                    (corner & 1) ? local_max[primitive][0] : local_min[primitive][0],
                    (corner & 2) ? local_max[primitive][1] : local_min[primitive][1],
                    (corner & 4) ? local_max[primitive][2] : local_min[primitive][2],
                };
                vec3 transformed;
                glm_mat4_mulv3(world, point, 1.0f, transformed);
                glm_vec3_minv(world_min, transformed, world_min);
                glm_vec3_maxv(world_max, transformed, world_max);
            }
        }
    }
    model->item_count = item;
    model->primitive_count = next;

    if (item > 0 && world_min[0] <= world_max[0]) {
        glm_vec3_add(world_min, world_max, model->center);
        glm_vec3_scale(model->center, 0.5f, model->center);

        vec3 extent;
        glm_vec3_sub(world_max, world_min, extent);
        model->radius = 0.5f * glm_vec3_norm(extent);
        if (model->radius <= 0.0f) {
            model->radius = 1.0f;
        }
    } else {
        glm_vec3_zero(model->center);
        model->radius = 1.0f;
    }

    result = 0;

out:
    free(local_min);
    free(local_max);
    free(mesh_first);
    if (data) {
        cgltf_free(data);
    }
    if (result != 0) {
        model_destroy(model);
    }
    return result;
}

void model_destroy(Model *model)
{
    for (size_t i = 0; i < model->primitive_count; ++i) {
        ModelPrimitive *primitive = &model->primitives[i];
        if (primitive->ebo) {
            glDeleteBuffers(1, &primitive->ebo);
        }
        if (primitive->vbo) {
            glDeleteBuffers(1, &primitive->vbo);
        }
        if (primitive->vao) {
            glDeleteVertexArrays(1, &primitive->vao);
        }
        if (primitive->texture) {
            glDeleteTextures(1, &primitive->texture);
        }
    }
    free(model->primitives);
    free(model->items);
    memset(model, 0, sizeof(*model));
}
