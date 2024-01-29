# USDPLY

## Supported features

|Feature|Import|Export|
|--|--|--|
|Scene up axis            |⦸|⦸|
|Scene units              |⦸|⦸|
||||
|Node hierarchy           |⦸|⦸|
|Node transform matrix    |⦸|⦸|
|Node TRS                 |⦸|⦸|
|Node animations          |⦸|⦸|
||||
|Cameras                  |⦸|⦸|
||||
|Mesh positions           |✅|✅|
|Mesh normals             |✅|✅|
|Mesh uvs                 |✅|✅|
|Mesh vertex colors       |✅|✅|
|Mesh skinning            |⦸|⦸|
|Mesh blend shapes        |⦸|⦸|
|Mesh instancing          |⦸|⦸|
|Mesh bounding box        |⦸|⦸|
||||
|Nurbs                    |⦸|⦸|
||||
|Skeletons                |⦸|⦸|
|Skeleton Animations      |⦸|⦸|
||||
|Materials                |⦸|⦸|






## Translation Notes

**Export:**
The different static and skinned meshes from USD are transformed by their local to world transform 
and aggregated into a single mesh because Ply lacks support for multiple individual meshes.

## File Format Arguments

**Import:**
* `plyPoints`: Imports UsdGeomMesh instances as points or as mesh.
    The following imports UsdGeomMesh instances as points:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.ply:SDF_FORMAT_ARGS:plyPoints=true")
    stage->Export("cube.usd")
    ```
* `plyPointWidth`: Defines the point size, in case `plyPoints` is true.
    The following imports UsdGeomMesh instances as points with size `0.1`.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.ply:SDF_FORMAT_ARGS:plyPoints=true&plyPointWidth=0.1")
    stage->Export("cube.usd")
    ```

## Debug codes
* `FILE_FORMAT_PLY`: Common debug messages.


