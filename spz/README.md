# USDSPZ

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2411-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2411-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2408-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2411-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2411-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-SPZ.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

## Supported features

|Feature|Import|Export|
|--|--|--|
|Gaussian splats          |✅|✅|

## File Format Arguments

**Import:**

* `spzGsplatsClippingBox`: imported Gaussian splats will be clipped with the range specified by this box, where the value is a string in the form of `[-X, -Y, -Z, X, Y, Z]`, by default it is -2 to 2 on each axis.
* `spzGsplatsWithZup`: Whether the imported Gaussian splat is treated as a Z-up object. If so we apply a rotation
    to Y-up during importing. By default it is false.
    The following imports UsdGeomPoints instances as Gaussian splats without rotation (if the SPZ contains all the Gaussian-splat-related attributes).
    ```
    UsdStageRefPtr stage = UsdStage::Open("gsplat.spz:SDF_FORMAT_ARGS:spzGsplatsWithZup=false")
    stage->Export("gsplat.usd")
    ```

## Debug codes
* `FILE_FORMAT_SPZ`: Common debug messages.


