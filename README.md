# USD File Format Plugins
These [USD file-format-plugins](https://graphics.pixar.com/usd/release/plugins.html) allow the interchange between [Pixar's USD](https://graphics.pixar.com/usd/release/index.html) (`.usd`, `.usda`, `.usdz`) and the following file formats, with cross platform support (`windows`, `macos`, and `linux`):


|Plugin|File Format|Extension|
|--|--|--|
| [usdfbx](fbx/README.md)      | [Autodesk's FBX](https://www.autodesk.com/products/fbx/overview)                     | `.fbx` |
| [usdgltf](gltf/README.md)    | [Khronos' glTF](https://www.khronos.org/gltf/)                                       | `.gtlf` `.glb` |
| [usdobj](obj/README.md)      | [Wavefront's obj](https://en.wikipedia.org/wiki/Wavefront_.obj_file)                 | `.obj` |
| [usdply](ply/README.md)      | [Polygon File Format](https://en.wikipedia.org/wiki/PLY_(file_format))               | `.ply` |
| [usdsbsar](sbsar/README.md)  | [SBSAR file format](https://developer.adobe.com/console/servicesandapis#)            | `.sbsar` |
| [usdstl](stl/README.md)      | [STL file format](https://en.wikipedia.org/wiki/STL_(file_format))                   | `.stl` |


> Each file format's readme contains what they support.<br>
> Supported features legend:<br>
> &nbsp;&nbsp;&nbsp;&nbsp;✅ Supported  
> &nbsp;&nbsp;&nbsp;&nbsp;⚠️ Supported with known issues  
> &nbsp;&nbsp;&nbsp;&nbsp;❌ Not supported  
> &nbsp;&nbsp;&nbsp;&nbsp;⦸ Not applicable/no support planned  


## Dependencies
The following tools are needed:
- C/C++ compiler ([MSVC 19](https://visualstudio.microsoft.com/vs/), [GCC](https://gcc.gnu.org/), [Clang](https://releases.llvm.org/download.html), [Xcode](https://developer.apple.com/xcode/))
- [clang-format 16.0.0](https://releases.llvm.org/download.html)
- [Python 3.10](https://www.python.org/)
- [CMake 3.24](https://cmake.org/)
- [Doxygen 1.9.8](https://www.doxygen.nl/)


The following dependencies are needed:
|Dependency|Version|Affects|Optional|
|--|--|--|--|
| [Pixar USD](https://github.com/PixarAnimationStudios/USD)         | 23.08       | all       | no  |
| [GTest](https://github.com/google/googletest.git)                 | 1.11.0      | all tests | yes |
| [FBX SDK](https://aps.autodesk.com/developer/overview/fbx-sdk)    | 2020.2.1    | usdfbx    | no  |
| [LibXml2](https://gitlab.gnome.org/GNOME/libxml2)                 | 2.10.0      | usdfbx    | no  |
| [Zlib](https://github.com/madler/zlib.git)                        | 1.2.11      | usdfbx    | no  |
| [TinyGltf](https://github.com/syoyo/tinygltf)                     | 2.8.21      | usdgltf   | no  |
| [Draco](https://github.com/google/draco.git)                      | 1.56        | usdgltf   | yes |
| [Fmt](https://github.com/fmtlib/fmt.git)                          | 10.1.1      | usdobj    | no  |
| [FastFloat](https://github.com/lemire/fast_float.git)             | 1.1.2       | usdobj    | no  |
| [Happly](https://github.com/nmwsharp/happly.git)                  | cfa2611     | usdply    | no |
| [Substance](https://developer.adobe.com/substance3d-sdk/)         | 9.1.2       | usdsbsar  | no |

## Build

### 1. Setup dependencies
* Install a C/C++ compiler.
* Install clang-format.
* Install cmake.
* Install python and the following pip components: `pyside6`, `pyopengl`.
* Build and install USD entering in a terminal (in windows a x64 Native Tools Command prompt):
    ```
    python <USD_SOURCE_PATH>/build_scripts/build_usd.py <USD_INSTALL_PATH> --draco --openimageio --build-variant release
    ```

    Add `--build-target universal` for universal binaries in macos.

    If adding `--openimageio` you may need these fixes:
    * https://github.com/PixarAnimationStudios/OpenUSD/pull/2517
    * https://github.com/PixarAnimationStudios/OpenUSD/pull/2079

    Setup USD environment variables:
    * `<USD_INSTALL_PATH>/bin` to `PATH`
    * `<USD_INSTALL_PATH>/lib` to `PATH` in windows, or to `LD_LIBRARY_PATH` in linux, mac
    * `<USD_INSTALL_PATH>/lib/python` to `PYTHONPATH`

    In linux you may need these other dependencies:
    ```
    sudo apt update
    sudo apt install libgl1-mesa-dev mesa-common-dev
    ```
* Install FBX SDK.
* You can install GTest, ZLIB, TinyGltf, Draco, fmt, FastFloat, Happly and OpenImageIO, or let cmake fetch them in the next steps (except for OpenImageIO). Also, you can leverage the installation of ZLIB, Draco and OpenImageIO included in USD.

* Substance SDK Integration
  1. Download the SDK: Visit the [Adobe Developer Console](https://developer.adobe.com/console/servicesandapis#) and log in or create an account if necessary.
  2. Locate the SDK: Use the search bar to find the ‘Adobe Substance 3D Materials SDK’.  Version 9.1.2

### 2. Get it
```
git clone https://github.com/adobe/USD-Fileformat-plugins
```
### 3. Configure, build and install it
To configure, build and install go to the project root folder and,
using a multi-configuration backend (MSVC, ...) enter:
```
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=bin <OPTIONS>
cmake --build   build --config release
cmake --install build --config release
```
or using a single-configuration backend (Make, ...) enter:
```
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=bin -DCMAKE_BUILD_TYPE=Release <OPTIONS>
cmake --build   build
cmake --install build
```
where:
* `<OPTIONS>` is a list of extra options, as follows:

|Option|Description|Default|Affects|
|---|---|---|---|
| -Dpxr_ROOT   | Points to the USD installation | empty | all |
| -DGTest_ROOT | Points to the GTest installation | empty | all tests |
| -DFBXSDK_ROOT | Points to the Fbx installation | empty | usdfbx |
| -Dsubstance_DIR | Points to the Substance SDK installation | empty | usdsbsar |
| -DZLIB_ROOT | Points to the ZLIB installation | empty | usdfbx |
| -DLibXml2_ROOT | Points to the LibXml2 installation | empty | usdfbx |
| -DTinyGLTF_ROOT | Points to the TinyGLTF installation | empty | usdgltf |
| -Ddraco_ROOT | Points to the draco installation | empty | usdgltf |
| -Dfmt_ROOT | Points to the fmt installation | empty | usdobj |
| -DFastFloat_ROOT | Points to the FastFloat installation | empty | usdobj |
| -DHapply_ROOT | Points to the Happly installation | empty | usdply |
| -DUSD_FILEFORMATS_BUILD_TESTS | Enables tests | ON | all tests |
| -DUSD_FILEFORMATS_ENABLE_FBX | Enables fbx plugin | ON | usdfbx |
| -DUSD_FILEFORMATS_ENABLE_GLTF | Enables gltf plugin | ON | usdgltf |
| -DUSD_FILEFORMATS_ENABLE_OBJ | Enables obj plugin | ON | usdobj |
| -DUSD_FILEFORMATS_ENABLE_PLY | Enables ply plugin | ON | usdply |
| -DUSD_FILEFORMATS_ENABLE_STL | Enables stl plugin |  ON | usdstl |
| -DUSD_FILEFORMATS_ENABLE_DRACO | Enables draco in usdgltf | OFF | usdgltf |
| -DUSD_FILEFORMATS_FORCE_FETCHCONTENT | Forces FetchContent for various packages | OFF | all |
| -DUSD_FILEFORMATS_FETCH_GTEST | Forces FetchContent for GTest | ON | all tests |
| -DUSD_FILEFORMATS_FETCH_TINYGLTF | Forces FetchContent for TinyGLTF | ON | usdgltf |
| -DUSD_FILEFORMATS_FETCH_ZLIB | Forces FetchContent for Zlib | OFF | usdfbx |
| -DUSD_FILEFORMATS_FETCH_LIBXML2 | Forces FetchContent for LibXml2 | OFF | usdfbx |
| -DUSD_FILEFORMATS_FETCH_HAPPLY | Forces FetchContent for Happly | ON | usdply |
| -DUSD_FILEFORMATS_FETCH_FMT | Forces FetchContent for Fmt | ON | usdobj |
| -DUSD_FILEFORMATS_FETCH_FASTFLOAT | Forces FetchContent for FastFLoat | ON | usdobj |
| -DUSD_FILEFORMATS_ENABLE_ASM | Generate a ASM based material network on layerwrite | OFF |

ZLIB, Draco and OpenImageIO packages are hinted to search into the USD installation by default. Override this by setting their ROOT or their FETCH variables (no fetch for OIIO).

The previous commands will place intermediate files into the folder `build` and install binaries into the folder `bin`.
Also, make the plugins discoverable by USD to complete installation, by adding the path `<INSTALL_PATH>/plugin/usd` to the `PXR_PLUGINPATH_NAME` environment variable (in this example: `USD-Fileformat-plugins/bin/plugin/usd`).

* Note when building on Linux: `-DUSD_FILEFORMATS_ENABLE_CXX11_ABI=ON`


## Test (https://github.com/pages/adobe/USD-Fileformat-plugins)
* Requires USD built with "--openimageio"
#### For Windows/Mac:
  ```bash
  python ./USD/build_scripts/build_usd.py ./usd-install  --build-shared --usd-imaging --tools --generator <GENERATOR> --openimageio --build-variant release
  ```
  #### For Linux/Mac:
  ```bash
  python ./USD/build_scripts/build_usd.py ./usd-install  --use-cxx11-abi=1 --build-shared --usd-imaging --tools --generator <GENERATOR> --openimageio --build-variant release
  ```

### 1. Install pip components
* Open your terminal and run the following commands to install the required Python packages:
  ```bash
  pip install -r scripts/requirements.txt
  ```
  
### 2. Install Plugins (Environment variables or Copy plugins to USD install)
Environment Variables
#### For Windows:
  ```bash
  set PATH=%PATH%;.\USD-Fileformat-plugins\bin\bin;.\USD-Fileformat-plugins\bin\plugin\usd
  set PXR_PLUGINPATH_NAME=%PXR_PLUGINPATH_NAME%;.\USD-Fileformat-plugins\bin\plugin\usd
  ```
#### For Linux/Mac
  ```bash
  export PATH=$PATH:./USD-Fileformat-plugins/bin/bin:./USD-Fileformat-plugins/bin/plugin/usd
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./USD-Fileformat-plugins/bin/lib:./USD-Fileformat-plugins/bin/lib64
  export PXR_PLUGINPATH_NAME=$PXR_PLUGINPATH_NAME:./USD-Fileformat-plugins/bin/plugin/usd
  ```

Or Copy plugins:
* Copy the installed plugins and dependent shared libraries to the specified folder:
  ```bash
  mkdir -p ./LOCAL_USD_INSTALL/plugin/usd
  cp -r ./USD-Fileformat-plugins/bin/plugin/usd/* ./LOCAL_USD_INSTALL/plugin/usd/
  cp ./USD-Fileformat-plugins/bin/bin/* ./LOCAL_USD_INSTALL/plugin/usd/
  ```


### 3. Run Tests
* Dependencies
  ```bash
  pip install -r ../USD-Fileformat-plugins/scripts/requirements.txt
  ```
* Use pytest to run the tests:
  ```bash
  pytest ./USD-Fileformat-plugins/test/test.py
  ```

### 4. (Optional) Update Tests
* To generate new baseline data for tests, run the following command:
  ```bash
  python ./USD-Fileformat-plugins/test/test.py --generate_baseline
  ```

## Usage
USD will now be able to work with the supported files, for example:
* Use the USD tools on fbx:
```
usdview <fbx>          # Converts FBX to USD
usdcat <fbx>           # Converts FBX to USD
usdcat <usd> -o <fbx>  # Converts USD to FBX
```

* Use the C++ USD API:
```
#include <pxr/usd/usd/stage.h>
UsdStageRefPtr stage = UsdStage::Open("cube.fbx")
stage->Export("cube.usd")
```

* Use the Python USD API:
```
from pxr import Usd
stage = Usd.Stage.Open("cube.fbx")
stage.Export("cube.usd")
```

Refer to each plugin's README for more details.

## Documentation

To generate the documentation go to the project root folder and enter:
```
doxygen
```
The resulting documentation will be placed at the `docs` folder.