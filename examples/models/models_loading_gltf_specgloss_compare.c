/*******************************************************************************************
*
*   raylib [models] example/test - specular/glossiness before/after rendering comparison
*
*   Loads the same KHR_materials_pbrSpecularGlossiness asset twice and renders both
*   copies with an identical camera and lighting:
*     - LEFT  : material forced onto the rlgl default shader (pre-support behavior,
*               the spec/gloss data was effectively ignored)
*     - RIGHT : material using the specular/glossiness PBR shader (new behavior)
*
*   A screenshot is written on frame 30 for offline comparison, then the window closes.
*
*   Test licensed under an unmodified zlib/libpng license.
*
********************************************************************************************/

#include "raylib.h"
#include "rlgl.h"

int main(int argc, char **argv)
{
    const char *asset = (argc > 1) ? argv[1] :
        "resources/models/gltf/specgloss/sg_textures_only.glb";
    const char *outShot = (argc > 2) ? argv[2] : "sg_before_after.png";

    const int screenWidth = 1000;
    const int screenHeight = 500;

    InitWindow(screenWidth, screenHeight, "spec/gloss before/after");

    Model before = LoadModel(asset);
    Model after = LoadModel(asset);

    // "Before": force the default shader and clear the SG uniform locs so the
    // material renders like it did before spec/gloss support existed
    if (before.materialCount > 1)
    {
        before.materials[1].shader.id = rlGetShaderIdDefault();
        before.materials[1].shader.locs = rlGetShaderLocsDefault();
    }

    Camera camera = { 0 };
    camera.position = (Vector3){ 3.2f, 2.2f, 3.2f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int frame = 0;
    bool shotTaken = false;

    while (!WindowShouldClose() && (frame < 45))
    {
        BeginDrawing();
            ClearBackground((Color){ 30, 30, 34, 255 });

            BeginMode3D(camera);
                // Same geometry, same camera/light; only the shader differs
                DrawModel(before, (Vector3){ -1.6f, 0.0f, 0.0f }, 1.0f, WHITE);
                DrawModel(after, (Vector3){ 1.6f, 0.0f, 0.0f }, 1.0f, WHITE);
            EndMode3D();

            DrawLine(screenWidth/2, 0, screenWidth/2, screenHeight, GRAY);
            DrawText("BEFORE: default shader (spec/gloss ignored)", 30, 24, 18, RED);
            DrawText("AFTER: specular/glossiness PBR shader", screenWidth/2 + 30, 24, 18, GREEN);
            DrawText(asset, 30, screenHeight - 28, 16, LIGHTGRAY);
        EndDrawing();

        if ((frame == 30) && !shotTaken)
        {
            TakeScreenshot(outShot);
            shotTaken = true;
        }
        frame++;
    }

    UnloadModel(before);
    UnloadModel(after);
    CloseWindow();

    return 0;
}
