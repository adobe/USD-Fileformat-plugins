# USDOBJ

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2411-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2408-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2405-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2311-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2411-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2408-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2405-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2411-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2408-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2405-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2311-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2411-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2408-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2405-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2311-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-OBJ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

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
|Nurbs                    |❌|❌|
||||
|Skeletons                |⦸|⦸|
|Skeleton Animations      |⦸|⦸|
||||
|Materials                |✅|✅|





## Translation Notes

**Import:**

The generated USD will keep default units and up axis (1cm, +y).

Allows importing OBJ files with vertex color (#MRGB tag)

* `objOriginalColorSpace`: USD uses linear colorspace, however, OBJ colorspace could be either linear or sRGB.
    The user can set which one the data was in during import.  If the data is in sRGB it will be converted to linear while in USD. Exporting will also consider the original color space. See Export -> outputColorSpace for details.

    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.obj:SDF_FORMAT_ARGS:objOriginalColorSpace=sRGB")
    ```

**Export:**

Meshes distributed in the node hierarchy in USD will be transformed by their global transform
during the export, since obj does not support nodes.
Also, the resulting meshes are unitless (obj does not support units). No adjustments will be applied to the scale based on the input usd units. This is because obj readers in the industry make different assumptions on the units.

* `outputColorSpace`: USD uses linear colorspace, however, the original OBJ colorspace could be either linear or sRGB.
    If objOriginalColorSpace was set the fileformat plugin will use it when exporting unless outputColorSpace is specified.

    Order or precendence on export (Note: the plugin assumes usd data is linear)
    1. If outputColorSpace=linear, the usd color data is exported as is.
    2. If outputColorSpace=sRGB, the usd color data is converted to sRGB on export
    3. If outputColorSpace is not set and objOriginalColorSpace is known, it will export the color data in the original format
    4. If outputColorSpace is not set and objOriginalColorSpace is not known, it will export the color data as is.

    Example:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.obj:SDF_FORMAT_ARGS:objOriginalColorSpace=sRGB")

    # round trip the asset using the original colorspace
    stage.Export("round_trip_original_cube_srgb.obj")  // exported file will have sRGB colorspace

    # round trip the asset overriding the original colorspace
    stage.Export("round_trip_original_cube_linear.obj:SDF_FORMAT_ARGS:outputColorSpace=linear")  // exported file will have linear colorspace
    ```

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
    stage = Usd.Stage.Open("asset.obj:SDF_FORMAT_ARGS:assetsPath=exportPath")
    stage.Export("exportPath/asset.usd")
    ```

* `objAssetsPath`: Deprecated in favor of `assetsPath`.

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

* `objPhong`: Turn on the full import of the Phong shading model. Default is `false`

    By default, the plugin imports the diffuse component only, without specularities, but you can force the import of the full phong model like this:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("asset.obj:SDF_FORMAT_ARGS:objPhong=true&assetsPath=exportPath")
    stage.Export("exportPath/asset.usd")
    ```
    The phong to PBR conversion follows https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping.
    Keep in mind it is a lossy conversion.
    > Note: currently this only works when also providing assetsPath (TODO fix).

* `computeNormals`: Generate smooth vertex normals for meshes that don't have explicit normals in the OBJ file. Default is `false`
    By default, the plugin only imports normals if they are present in the OBJ file (as `vn` lines). If an OBJ file has no explicit normals,
    meshes will be imported without normal data, and renderers will compute normals at render time. You can force the generation of smooth vertex normals during import:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("asset.obj:SDF_FORMAT_ARGS:computeNormals=true")
    stage.Export("asset.usda")
    ```
  The generated normals are vertex-interpolated and computed by averaging face normals at shared vertices. This is useful for OBJ files exported without normals,
  though be aware that smooth normals will not preserve high-frequency detail that would be captured in per-face-vertex normals.
* `groupOptions`: Control how OBJ groups are imported into USD. Default is `separateGroupsAsMeshes`
    OBJ files can contain groups (`g` lines) that organize faces into logical collections. The `groupOptions` argument controls how these groups are translated to USD:
    - `separateGroupsAsMeshes` (default): Each OBJ group becomes a separate USD Mesh prim. This preserves the original group structure but can be slow for files with many groups.

    - `combineGroups`: All groups are merged into a single USD Mesh. This is much faster for files with many groups and results in better rendering performance. Group information is discarded.

    - `separateGroupsAsSubsets`: All groups are merged into a single USD Mesh, but each group is preserved as a GeomSubset. This maintains group information while having a single mesh, though creating many subsets can still be slow.
    Example using `combineGroups` for a file with many groups:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("sculpt.obj:SDF_FORMAT_ARGS:groupOptions=combineGroups")
    stage.Export("sculpt.usda")
    ```
## Debug codes
* `FILE_FORMAT_OBJ`: Common debug messages.
* OBJ_PACKAGE_RESOLVER

