# USDPLY

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2411-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2408-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2405-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2311-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2411-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2408-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2405-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2411-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2408-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2405-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2311-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2411-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2408-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2405-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2311-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-PLY.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

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
||||
|Point cloud positions    |✅|✅|
|Point cloud normals      |✅|✅|
|Point cloud uvs          |✅|✅|
|Gaussian splats          |✅|✅|

## Translation Notes

**Whether it is a mesh or a point cloud:**
We import UsdGeomPoints if the PLY file does not contain any facets, or the `plyPoints` option is on.

**Whether it is a Gaussian splat:**
Whether the imported instance is a Gaussian splat is determined by if it contains all the Gaussian-splat-related attributes, namely,
the `opacity` attribute specifying float-point opacities, the `f_dc_*` attributes specifying the float-point colors, the `rot` attribute specifying
splats' orientations, the `scale_*` attributes specifying the splats' object-space sizes, and the `f_rest_*` specifying the splats' spherical harmonics
coefficients up to 3rd orders (and thus there are `f_rest_0` to `f_rest_44`). We map the `opacity` to `displayOpacity`, `f_dc_*` to `displayColor` and `scale_0`
to `widths` of UsdGeomPoints.

Similarly, we detect whether the exported USD file has `rot`, `fRest*`, `widths1` (corresponding to `scale_1`) and `widths2` (corresponding to `scale_2`)
to determine if we should export a Gaussian splat.

**Export:**
The different static and skinned meshes from USD are transformed by their local to world transform 
and aggregated into a single mesh because Ply lacks support for multiple individual meshes.

## File Format Arguments

**Import:**
* `plyGsplatsClippingBox`: imported Gaussian splats will be clipped with the range specified by this box, where the value is a string in the form of `[-X, -Y, -Z, X, Y, Z]`, by default it is -2 to 2 on each axis.
* `plyPoints`: Forces importing UsdGeomMesh instances as points if true.
    The following imports UsdGeomMesh instances as points:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.ply:SDF_FORMAT_ARGS:plyPoints=true")
    stage->Export("cube.usd")
    ```
* `plyPointWidth`: Defines the default point size, in case the instance is treated as a point cloud.
    The following imports UsdGeomMesh instances as points with size `0.1`.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.ply:SDF_FORMAT_ARGS:plyPoints=true&plyPointWidth=0.1")
    stage->Export("cube.usd")
    ```
* `plyWithUpAxisCorrection`: Whether the imported PLY will have its axis corrected based on its comment (either explicitly has
    "Z-axis up" in the comment or is exported from a specific software that uses Z-axis up).
    If so we apply a rotation to Y-up during importing. By default it is true.
    The following imports UsdGeomPoints instances as Gaussian splats without axis correction (if the PLY contains all the Gaussian-splat-related attributes).
    ```
    UsdStageRefPtr stage = UsdStage::Open("gsplat.ply:SDF_FORMAT_ARGS:plyWithUpAxisCorrection=false")
    stage->Export("gsplat.usd")
    ```

## Debug codes
* `FILE_FORMAT_PLY`: Common debug messages.

