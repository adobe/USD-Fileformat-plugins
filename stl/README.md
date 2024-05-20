# USDSTL

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/windows-2022-2405-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/windows-2022-2311-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/windows-2022-2308-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/macOS-14-2405-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/macOS-13-2405-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/macOS-13-2311-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/macOS-13-2308-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/ubuntu-22.04-2405-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/ubuntu-22.04-2311-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/15b48d1a8211983471352e7f99f78127/raw/ubuntu-22.04-2308-STL.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

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
|Mesh uvs                 |⦸|⦸|
|Mesh vertex colors       |⦸|⦸|
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

**Import:**

The generated USD will be set with up axis = +z as that's the most common for stl files.

## File Format Arguments

**Export:**
* `exportAscii`: If true, the stl file will be in ascii format, otherwise in binary format.
Example:
```
#usda 1.0
(
    doc = "Blender v3.1.2"
    metersPerUnit = 1
    upAxis = "Z"
    customLayerData = {
  	bool exportASCII = false
  }
)

def Xform "Cube_001"
{
    matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1.4112299680709839, 0), (0.9483106136322021, 0, 1.1584662199020386, 1) )
    uniform token[] xformOpOrder = ["xformOp:transform"]

    def Mesh "Cube_002"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 7, 6, 6, 7, 5, 4, 4, 5, 1, 0, 2, 6, 4, 0, 7, 3, 1, 5]
        point3f[] points = [(-1, -1, -1), (-1, -1, 1), (-1, 1, -1), (-1, 1, 1), (1, -1, -1), (1, -1, 1), (1, 1, -1), (1, 1, 1)]
        uniform token subdivisionScheme = "none"
    }
}
```

## Debug codes
* `FILE_FORMAT_STL`: Common debug messages.





