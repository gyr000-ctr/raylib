/*
 * Regression guard: every pre-existing glTF/GLB asset in the examples must
 * still load, must not report any morph data when it has none, and must survive
 * a full load -> animation -> unload cycle without crashing.
 *
 * Headless software renderer, see run_tests.sh for the build.
 */
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg, ...) \
    do { g_checks++; if (!(cond)) { g_failures++; printf("  [FAIL] " msg "\n", ##__VA_ARGS__); } } while (0)

typedef struct {
    const char *path;
    int expectMeshAtLeast;
} RegressionAsset;

int main(void)
{
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(64, 64, "morph regression");

    const char *root = getenv("RAYLIB_REPO_ROOT");
    if (root == NULL) root = "../..";

    RegressionAsset assets[] = {
        { "examples/models/resources/models/gltf/greenman.glb", 1 },
        { "examples/models/resources/models/gltf/greenman_sword.glb", 1 },
        { "examples/models/resources/models/gltf/greenman_hat.glb", 1 },
        { "examples/models/resources/models/gltf/greenman_shield.glb", 1 },
        { "examples/models/resources/models/gltf/robot.glb", 1 },
        { "examples/models/resources/models/gltf/raylib_logo_3d.glb", 1 },
        { "examples/shaders/resources/models/plane.glb", 1 },
        { "examples/shaders/resources/models/robot.glb", 1 },
        { "examples/shaders/resources/models/old_car_new.glb", 1 },
    };
    const int count = (int)(sizeof(assets)/sizeof(assets[0]));

    for (int i = 0; i < count; i++)
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, assets[i].path);
        printf("REGRESSION %s\n", assets[i].path);

        Model model = LoadModel(path);
        CHECK(model.meshCount >= assets[i].expectMeshAtLeast, "%s: expected meshes, got %d",
              assets[i].path, model.meshCount);

        if (model.meshCount > 0)
        {
            // These legacy assets define no morph targets
            int totalTargets = 0;
            for (int m = 0; m < model.meshCount; m++)
                totalTargets += GetMeshMorphTargetCount(model.meshes[m]);

            CHECK(totalTargets == 0, "%s: legacy asset unexpectedly reports %d morph targets",
                  assets[i].path, totalTargets);

            // No morph runtime arrays may be allocated for plain meshes
            for (int m = 0; m < model.meshCount; m++)
            {
                CHECK(model.meshes[m].morphTargets == NULL &&
                      model.meshes[m].morphWeights == NULL &&
                      model.meshes[m].morphVertices == NULL,
                      "%s: mesh %d allocated morph data without targets", assets[i].path, m);
            }

            // A draw and a morph reset must be safe no-ops on a plain model
            BeginDrawing();
            DrawModel(model, (Vector3){ 0 }, 1.0f, WHITE);
            EndDrawing();
            ResetModelMorphWeights(model);

            // Existing bone animations (if any) must still load and validate
            int animCount = 0;
            ModelAnimation *anims = LoadModelAnimations(path, &animCount);
            if ((anims != NULL) && (animCount > 0))
            {
                for (int a = 0; a < animCount; a++)
                {
                    CHECK(anims[a].morphChannelCount == 0,
                          "%s: legacy anim %d unexpectedly has morph channels", assets[i].path, a);
                    if (anims[a].keyframeCount > 0)
                        UpdateModelAnimation(model, anims[a], 0.0f);
                }
            }
            UnloadModelAnimations(anims, animCount);
        }

        UnloadModel(model);
    }

    CloseWindow();

    printf("\n========================================\n");
    printf("Regression checks: %d | Failures: %d\n", g_checks, g_failures);
    printf("========================================\n");
    return (g_failures == 0)? 0 : 1;
}
