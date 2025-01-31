# USDFBX

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2411-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)  [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2408-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)  [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2405-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2311-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2411-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)  [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2408-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2405-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2411-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2408-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2405-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2311-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2411-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2408-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2405-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2311-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-FBX.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

## Supported features

|Feature|Import|Export|
|--|--|--|
|Scene up axis            |✅|✅|
|Scene units              |✅|✅|
||||
|Node hierarchy           |✅|✅|
|Node transform matrix    |✅|✅|
|Node TRS                 |❌|❌|
|Node animations          |⚠️|⚠️|
||||
|Cameras                  |✅|✅|
||||
|Lights                   |⚠️|⚠️|
||||
|Mesh positions           |✅|✅|
|Mesh normals             |✅|✅|
|Mesh uvs                 |✅|✅|
|Mesh vertex colors       |✅|✅|
|Mesh skinning            |✅|✅|
|Mesh blend shapes        |❌|❌|
|Mesh instancing          |✅|✅|
|Mesh bounding box        |❌|❌|
||||
|Nurbs                    |❌|❌|
||||
|Skeletons                |✅|✅|
|Skeleton Animations      |✅|⚠️|
||||
|Materials                |✅|⚠️|




## Translation Notes

**Import:**


|Material Import|
|--|
|PhongSurface → UsdPreviewSurface|
|FbxSurfaceLambert → UsdPreviewSurface|
|Hardware material is not imported|

- Only point, directional, and spot lights are imported. Other light types are ignored.

**Export:**


|Material Export|
|--|
diffuseColor → phongSurface::Diffuse
emissiveColor →  phongSurface::Emissive and  phongSurface::EmissiveFactor = 1
normal → phongSurface::NormalMap
specularColor → phongSurface::Specular (if useSpecularWorkflow = on) (disabled temporarily)
metallic →  phongSurface::Specular (if useSpecularWorkflow = off) (disabled temporarily)
roughness →  1 - phongSurface::Shininess (experimental) (disabled temporarily)
occlusion →  phongSurface::AmbientOcclusion
clearcoat → phongSurface::Reflection
clearcoatRoughness → Not been used.
opacity → phongSurface::TransparentColor
ior → Not been used.
displacement → phongSurface::DisplacementColor

Note that PBR materials are not supported on export, only Phong

- Only point, directional, and spot lights are imported. Other light types are exported as point lights.

- **OBS: The image files used by the UsdPreviewShader node will be extracted from the USDZ file and saved as PNG files in the same folder as the generated fbx. If the source file is USD the files should also be copied from the USD folder into the FBX folder.**

## File Format Arguments
**Import:**

* `fbxAssetsPath`: Filesystem path where image assets are saved to.
    By default image assets are not copied, but the generated usd file will resolve them from the original file.
    The following saves images to the path `myPath` during `UsdStage::Open` and then exports the stage to that same path.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxAssetsPath=myPath")
    stage->Export("myPath/cube.usd")
    ```

* `fbxPhong`: Forces phong to PBR material conversion.
    By default turned off: the plugin imports the diffuse component only, without specularities.
    The following converts PBR to phong.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxPhong=true")
    stage.Export("cube.usd")
    ```
    The phong to PBR conversion follows https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping. Keep in mind it is a lossy conversion.

* `fbxOriginalColorSpace`: USD uses linear colorspace, however, FBX colorspace could be either linear or sRGB.
    The user can set which one the data was in during import.  If the data is in sRGB it will be converted to linear while in USD. Exporting will also consider the original color space. See Export -> outputColorSpace for details.

    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxOriginalColorSpace=sRGB")
    ```

* `fbxAnimationTracks`: Import multiple animation stacks. Default is `false`
    The default is that only the first animation stack is imported.
    It is only recommended to use this parameter in order to convert from FBX to another format, such as fbx.
    It is not recommended to export a .usd file after importing a file with this parameter set.
    ```
    The following allows additional animation stacks to be imported, and adds metadata to USD to encode where
    each stack begins and ends. The exporter can then read this metadata to export the stacks properly.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxAnimationStacks=true")
    stage->Export("myPath/cube.fbx")
    ```

**Export:**

* `embedImages` Embed images in the exported fbx file instead of as separate files. Default is `false`.
    The following exports to `fbx` and embeds images:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.usd");
    SdfLayer::FileFormatArguments args = { {"embedImages", "true"} };
    stage->Export("cube.fbx", false, args);
    ```
* `outputColorSpace`: USD uses linear colorspace, however, the original FBX colorspace could be either linear or sRGB.
    If fbxOriginalColorSpace was set the fileformat plugin will use it when exporting unless outputColorSpace is specified.

    Order or precendence on export (Note: the plugin assumes usd data is linear)
    1. If outputColorSpace=linear, the usd color data is exported as is.
    2. If outputColorSpace=sRGB, the usd color data is converted to sRGB on export
    3. If outputColorSpace is not set and fbxOriginalColorSpace is known, it will export the color data in the original format
    4. If outputColorSpace is not set and fbxOriginalColorSpace is not known, it will export the color data as is.

    Example:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxOriginalColorSpace=sRGB")

    # round trip the asset using the original colorspace
    stage.Export("round_trip_original_cube_srgb.fbx")  // exported file will have sRGB colorspace

    # round trip the asset overriding the original colorspace
    stage.Export("round_trip_original_cube_linear.fbx:SDF_FORMAT_ARGS:outputColorSpace=linear")  // exported file will have linear colorspace
    ```

## Debug codes
* `FILE_FORMAT_FBX`: Common debug messages.
* `FBX_PACKAGE_RESOLVER`: Asset resolution debug messages, when resolving images from the original
  FBX file.



## Known Issues

**Import:**
* Animations on non-skeletal nodes is not supported or in some cases is lost.
* Edge information is ignored which can lead to incorrect triangulation of quads
* Multiple animation tracks are not supported
* Issues with layered textures

**Export:**
* Skeletal animation is lost, due to problems with skeletons with negative scaling values.
