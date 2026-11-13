/*
 * GPU-backend (OpenGL 3.3 core, headless EGL/llvmpipe) verification of the
 * morph target vertex-buffer path. The CPU/software pipeline is covered by
 * test_morph.c; this program focuses on the GL 3.3 code branches:
 *   - UploadMesh() sources morph runtime arrays into the VBOs
 *   - ApplyModelMorph() mirrors updates through rlUpdateVertexBuffer()
 *   - Default SUPPORT_GPU_SKINNING=1 build still loads skinned+morph models
 *
 * It reuses the same reference assets and tolerance.
 */

// EGL must be included before any GL header (it pulls in the Khronos platform types)
#include "egl_headless.h"

#include "raylib.h"
#include "rlgl.h"
#include "external/glad.h"   // GL 3.3 declarations only (no GLAD_GL_IMPLEMENTATION)
#include "mini_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TOLERANCE 1e-4f

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg, ...) \
    do { \
        g_checks++; \
        if (!(cond)) { g_failures++; printf("  [FAIL] " msg "\n", ##__VA_ARGS__); } \
    } while (0)

static const char *asset_dir(void)
{
    const char *dir = getenv("MORPH_ASSET_DIR");
    return (dir != NULL)? dir : "assets";
}

static char *asset_path(const char *name)
{
    static char path[1024];
    snprintf(path, sizeof(path), "%s/%s", asset_dir(), name);
    return path;
}

// Read back a vertex buffer through glGetBufferSubData and compare.
// GL types and entry points come from rlgl.h (which pulls in glad for GL 3.3).
static bool vbo_matches(unsigned int vboId, const float *expected, int floatCount, const char *label, const char *asset)
{
    if (vboId == 0) { printf("  [FAIL] %s: %s VBO id is 0\n", asset, label); g_failures++; g_checks++; return false; }

    float *readback = (float *)malloc((size_t)floatCount*sizeof(float));
    glBindBuffer(GL_ARRAY_BUFFER, vboId);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)floatCount*sizeof(float), readback);

    float maxErr = 0.0f;
    int bad = -1;
    for (int i = 0; i < floatCount; i++)
    {
        float err = fabsf(readback[i] - expected[i]);
        if (err > maxErr) { maxErr = err; bad = i; }
    }

    free(readback);

    if (maxErr > TEST_TOLERANCE)
    {
        g_checks++; g_failures++;
        printf("  [FAIL] %s: %s VBO max error %.8g at %d\n", asset, label, maxErr, bad);
        return false;
    }

    return true;
}

static void test_gpu_morph_vbo(void)
{
    const char *asset = "morph_single_linear.glb";
    printf("GPU TEST morph position/normal/tangent VBO updates (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: load failed", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: target missing", asset);

    // Upload after LoadModel already happens, but force it explicitly to validate sourcing
    if (mesh->vaoId == 0) UploadMesh(mesh, false);
    CHECK(mesh->vaoId > 0 && mesh->vboId != NULL, "%s: GPU upload failed", asset);

    // At load time default weights (0.25) must already be in the position VBO
    vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION],
                mesh->morphVertices, mesh->vertexCount*3, "position(default)", asset);
    vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL],
                mesh->morphNormals, mesh->vertexCount*3, "normal(default)", asset);
    vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT],
                mesh->morphTangents, mesh->vertexCount*4, "tangent(default)", asset);

    // Change weights and re-apply; the VBOs must track the CPU arrays without reallocation
    unsigned int posVboBefore = mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION];
    float *cpuPtrBefore = mesh->morphVertices;

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount >= 1 && anims != NULL, "%s: anim load failed", asset);
    if (anims != NULL && animCount >= 1)
    {
        UpdateModelMorph(model, anims[0], 60.0f); // t=1.0s -> weight 1.0
        vbo_matches(posVboBefore, mesh->morphVertices, mesh->vertexCount*3, "position(w=1)", asset);
        vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL],
                    mesh->morphNormals, mesh->vertexCount*3, "normal(w=1)", asset);
        vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT],
                    mesh->morphTangents, mesh->vertexCount*4, "tangent(w=1)", asset);

        CHECK(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION] == posVboBefore,
              "%s: position VBO was reallocated", asset);
        CHECK(mesh->morphVertices == cpuPtrBefore, "%s: CPU morph buffer was reallocated", asset);

        // First vertex at weight 1: base (-1,0,0) + delta (0,1,0) => y == 1
        CHECK(fabsf(mesh->morphVertices[1] - 1.0f) <= TEST_TOLERANCE,
              "%s: morphed y wrong (%.8g)", asset, mesh->morphVertices[1]);

        UnloadModelAnimations(anims, animCount);
    }

    UnloadModel(model);
}

static void test_gpu_skin_morph_build(void)
{
    const char *asset = "morph_skin_coexist.glb";
    printf("GPU TEST skinned+morph model loads on GPU-skinning build (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1 && model.skeleton.boneCount == 1, "%s: load/skin failed", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: morph target missing", asset);
    CHECK(mesh->boneIndices != NULL && mesh->boneWeights != NULL, "%s: skin attrs missing", asset);
    if (mesh->vaoId == 0) UploadMesh(mesh, false);
    CHECK(mesh->vaoId > 0, "%s: GPU upload failed", asset);

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 2, "%s: expected 2 anims, got %d", asset, animCount);
    if (anims != NULL && animCount == 2)
    {
        int morphAnim = -1, boneAnim = -1;
        for (int i = 0; i < animCount; i++)
        {
            if (anims[i].morphChannelCount > 0) morphAnim = i;
            if (anims[i].keyframeCount > 0) boneAnim = i;
        }
        CHECK(morphAnim >= 0 && boneAnim >= 0, "%s: anim kinds missing", asset);

        // Drive both; must not crash and morph CPU arrays stay finite
        UpdateModelMorph(model, anims[morphAnim], 30.0f);
        UpdateModelAnimation(model, anims[boneAnim], 30.0f);

        bool finite = true;
        for (int i = 0; i < mesh->vertexCount*3; i++)
            if (!isfinite(mesh->morphVertices[i])) { finite = false; break; }
        CHECK(finite, "%s: morph vertices non-finite after combined update", asset);

        UnloadModelAnimations(anims, animCount);
    }

    UnloadModel(model);
}

static void test_gpu_plain_regression(void)
{
    const char *asset = "plain_no_morph.glb";
    printf("GPU TEST plain model unaffected (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: load failed", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 0 && mesh->morphVertices == NULL, "%s: unexpected morph data", asset);
    if (mesh->vaoId == 0) UploadMesh(mesh, false);
    CHECK(mesh->vaoId > 0, "%s: GPU upload failed", asset);

    // Position VBO must contain the untouched base geometry
    vbo_matches(mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION],
                mesh->vertices, mesh->vertexCount*3, "position(plain)", asset);

    UnloadModel(model);
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);

    if (!egl_headless_init(64, 64))
    {
        fprintf(stderr, "EGL headless init failed, skipping GPU tests\n");
        return 2;
    }

    rlLoadExtensions(eglGetProcAddress);
    rlglInit(64, 64);
    rlSetFramebufferWidth(64);
    rlSetFramebufferHeight(64);

    printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));

    test_gpu_morph_vbo();
    test_gpu_skin_morph_build();
    test_gpu_plain_regression();

    rlglClose();
    egl_headless_shutdown();

    printf("\n========================================\n");
    printf("GPU Checks: %d | Failures: %d\n", g_checks, g_failures);
    printf("========================================\n");
    return (g_failures == 0)? 0 : 1;
}
