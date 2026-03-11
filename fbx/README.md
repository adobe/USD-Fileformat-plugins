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

- Only point, directional, and spot lights are exported. Other light types are exported as point lights.

- **OBS: The image files used by the UsdPreviewShader node will be extracted from the USDZ file and saved as PNG files in the same folder as the generated fbx. If the source file is USD the files should also be copied from the USD folder into the FBX folder.**

## File Format Arguments

**Import:**

* `assetsPath`: Filesystem path where image assets are saved to during import. Default is `""`

    By default image textures used by the asset are not copied during import, but are kept in memory and are available
    via an associated `ArResolver` plugin. By specifying a filesystem location via `assetsPath`, the import process will
    copy the image textures to that location and provide asset paths to those locations in the generated USD data. This
    file format argument allows an easy way to export associated images textures to disk when converting an asset to USD.

    This snippet saves image textures to the path at `exportPath` during `Usd.Stage.Open` and then also exports the stage
    to that same location, so that the USD data and the used images a co-located.
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("asset.fbx:SDF_FORMAT_ARGS:assetsPath=exportPath")
    stage.Export("exportPath/asset.usd")
    ```

* `fbxAssetsPath`: Deprecated in favor of `assetsPath`.

* `writeUsdPreviewSurface`: Generate a UsdPreviewSurface based network for each material. Default is `true`

    UsdPreviewSurface and its associated nodes are a universally understood USD material description
    and all application should support them. The PBR capabilities are limited.

* `writeASM`: Generate a ASM (Adobe Standard Material) based network for each material. Default is `true`

    ASM is a standard supported by many Adobe applications with richer support for PBR capabilities.
    It will be superseded by OpenPBR in the near future.

* `writeOpenPBR`: Generate a OpenPBR based material network for each material. Default is `false`

    OpenPBR is a new industry standard that will have wide spread support, but is still in its infancy.
    The material network uses `MaterialX` nodes to express individual operations and has an `OpenPBR` surface,
    which has rich support for PBR oriented materials.

* `preserveExtraMaterialInfo`: Generate shading networks with extra data for transcoding. Default is `true`
    When this is enabled, the generated shading networks might contain extra inputs that are outside of the respective
    material surface schema, that are useful for transcoding purposes. For example, the `OpenPBR` surface does not have
    an `occlusion` input for ambient occlusion, but we might want to express such a signal, if it was present in the
    source asset, so that an exporter can pick-up said signal and use it when generating an output asset.
    When `preserveExtraMaterialInfo` is `false`, the code will not generate these extra fields that are outside of the
    schema, which won't affect renders, but can affect the transcoding abilities.

* `fbxPhong`: Forces phong to PBR material conversion.
    By default turned off: the plugin imports the diffuse component only, without specularities.
    The following converts PBR to phong.
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.fbx:SDF_FORMAT_ARGS:fbxPhong=true")
    stage.Export("cube.usd")
    ```
    The phong to PBR conversion follows https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping.
    Keep in mind it is a lossy conversion.

* `fbxOriginalColorSpace`: Specify the color space of the FBX data. Default: `""` (no conversion)
    **Default behavior:** When not specified, the plugin performs **no color conversion** — color values
    are passed through as-is and annotated as `raw` (unknown colorspace). This allows the client
    application to handle color management according to its needs.
    **When set to `sRGB`:** The plugin converts sRGB color values to linear on import, as USD expects
    linear color data for rendering. The converted data is annotated as `raw` (linear).

    USD uses a linear colorspace, however, FBX colorspace could be either linear or sRGB.
    The user can set which one the data was in during import. Exporting will also consider the
    original color space. See Export -> `outputColorSpace` for details.

    ```
    from pxr import Usd
    # No conversion (default) - data passed through as-is
    stage = Usd.Stage.Open("cube.fbx")

    # Convert sRGB to linear
    stage = Usd.Stage.Open("cube.fbx:SDF_FORMAT_ARGS:fbxOriginalColorSpace=sRGB")
    ```

* `fbxAnimationStacks`: Import multiple animation stacks. Default is `false`

    By default only the first animation stack is imported.
    It is only recommended to use this parameter in order to convert from FBX to another format that supports multiple
    animation tracks, such as GLTF. It is not recommended to export a .usd file after importing a file with this parameter
    set, as there is no standard way to encode this information.

    The following allows additional animation stacks to be imported, and adds metadata to USD to encode where each stack
    begins and ends. The exporter can then read this metadata to export the stacks properly.
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.fbx:SDF_FORMAT_ARGS:fbxAnimationStacks=true")
    stage.Export("myPath/cube.gltf")
    ```
* `triangulateMeshes`: Use edge information if present to triangulate quads. Default is `true`

    FBX supports quad meshes and there may be additional edge information that can be used to guide the triangulation
    on import. The flag controls whether the triangulation should be done at all.

    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.fbx:SDF_FORMAT_ARGS:triangulateMeshes=false")
    ```

**Export:**

* `embedImages`: Embed images in the exported FBX file instead of as separate files. Default is `false`.

    The following exports to FBX and embeds images:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.usd");
    stage.Export("cube.fbx", args={ "embedImages": "true" });
    ```

* `outputColorSpace`: Convert colors from linear to sRGB. Default: `""`

    USD uses linear colorspace, however, the original FBX colorspace could be either linear or sRGB.
    If `fbxOriginalColorSpace` was set the fileformat plugin will use it when exporting unless outputColorSpace is specified.

    Order or precendence on export (Note: the plugin assumes USD data is linear)
    1. If `outputColorSpace=linear`, the USD color data is exported as is.
    2. If `outputColorSpace=sRGB`, the USD color data is converted to sRGB on export
    3. If `outputColorSpace` is not set and `fbxOriginalColorSpace` is known, it will export the color data in the original format
    4. If `outputColorSpace` is not set and `fbxOriginalColorSpace` is not known, it will export the color data as is.

    Example:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.fbx:SDF_FORMAT_ARGS:fbxOriginalColorSpace=sRGB")

    # round trip the asset using the original colorspace
    stage.Export("round_trip_original_cube_srgb.fbx")  // exported file will have sRGB colorspace

    # round trip the asset overriding the original colorspace
    # the exported file will have a linear colorspace
    stage.Export("round_trip_original_cube_linear.fbx", args={ "outputColorSpace": "linear" } )
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
