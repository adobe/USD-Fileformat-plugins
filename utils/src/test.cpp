/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <fileformatutils/test.h>

#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/material.h>

#include <type_traits>

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(TestTokens, TEST_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

using namespace PXR_NS;

const float LIGHT_INTENSITY_EPSILON = 0.1f;

/**
 * ASSERT_EQ_VAL(actual, expected, msg, ...)
 *
 * Checks that `actual` and `expected` are equal.
 * - For ints, floats, vectors uses `fuzzyEqual` for comparison.
 * - For other types, uses `operator==`.
 * - If an extra argument is provided, it's forwarded to `fuzzyEqual`.
 *
 * Returns:
 *  - success: execution continues.
 *  - failure: returns ::testing::AssertionFailure() with a detailed message.
 *
 * Example usage:
 *  ASSERT_EQ_VAL(value, expectedValue, "Value mismatch");
 *  ASSERT_EQ_VAL(floatValue, expectedFloatValue, "Float value mismatch", 0.001f);
 *  ASSERT_EQ_VAL(value, expectedValue, "Value mistach", &failingIndex);
 */

inline std::string
ToString(const VtValue& v)
{
    std::ostringstream ss;
    VtStreamOut(v, ss);
    return ss.str();
}

#define ASSERT_EQ_VAL(actual, expected, msg, ...)                                     \
    do {                                                                              \
        auto _res = AssertEqValHelper(                                                \
          (actual), (expected), std::string(msg), __FILE__, __LINE__, ##__VA_ARGS__); \
        if (!_res.first) {                                                            \
            ::testing::AssertionResult ar = ::testing::AssertionFailure();            \
            ar << _res.second;                                                        \
            return ar;                                                                \
        }                                                                             \
    } while (0)

/**
 * ASSERT_GE_VAL(actual, expected, msg)
 *
 * Checks that `actual` is greater than or equal to `expected`.
 * If the check fails, returns a ::testing::AssertionFailure()
 * with a detailed message including:
 *
 * Returns:
 *  - success: execution continues.
 *  - failure: returns ::testing::AssertionFailure() with a detailed message.
 *
 * Example usage:
 *   ASSERT_GE_VAL(value, 10, "Value should be at least 10");
 */
#define ASSERT_GE_VAL(actual, expected, msg)                                                   \
    do {                                                                                       \
        if (!((actual) >= (expected))) {                                                       \
            std::ostringstream oss;                                                            \
            oss << msg << ": " << actual << " is not >= " << expected << "\n  At " << __FILE__ \
                << ":" << __LINE__;                                                            \
            return ::testing::AssertionFailure() << oss.str();                                 \
        }                                                                                      \
    } while (0)

/**
 * ASSERT_CHECK(expr, ...)
 *
 * Macro wraps AssertCheckHelper.
 * Evaluates the expression and returns a GoogleTest AssertionResult
 * with an optional message.
 *
 * Returns:
 *  - success: execution continues.
 *  - failure: returns ::testing::AssertionFailure() with a detailed message.
 *
 * Example usage:
 * ASSERT_CHECK(9 != 8, "Expected valid pointer");
 * ASSERT_CHECK(assertVec(vecA, vecB), "Vector mismatch");
 * ASSERT_CHECK(assertVec(vecA, vecB));
 * ASSERT_CHECK(prim);
 * ASSERT_CHECK(prim, "Expected valid prim at path /World/MyPrim");
 */
#define ASSERT_CHECK(expr, ...)                                                   \
    do {                                                                          \
        auto _res = AssertCheckHelper((expr), __FILE__, __LINE__, ##__VA_ARGS__); \
        if (!_res.first) {                                                        \
            ::testing::AssertionResult ar = ::testing::AssertionFailure();        \
            ar << _res.second;                                                    \
            return ar;                                                            \
        }                                                                         \
    } while (0)

/**
 * AssertCheckHelper(T&& expr, Msg&&... msg)
 * Assertion helper that evaluates `expr` and returns a GoogleTest
 * `AssertionSuccess()` or `AssertionFailure()`. Supports:
 * 1. bool
 * 2. ::testing::AssertionResult
 * 3. Expressions returning bool (like x > y)
 * 4. USD objects/handles (e.g. UsdPrim)
 *
 * Behavior:
 *  - If `expr` is true, returns `AssertionSuccess()`.
 *  - If `expr` is false, returns `AssertionFailure()` with a detailed message.
 *  - If `expr` is an `AssertionResult`, its `.message()` is included automatically.
 *  - Optional `msg` arguments are streamed into the failure message.
 *  - File and line information is appended automatically.
 *
 * Returns:
 *  - `::testing::AssertionSuccess()` if the expression is true.
 *  - `::testing::AssertionFailure()` with optional message and location if false.
 */
template<typename T, typename... Msg>
std::pair<bool, std::string>
AssertCheckHelper(T&& expr, const char* file, int line, Msg&&... msg)
{
    using DecayedT = std::decay_t<T>;
    bool ok = false;

    // Handle void expressions
    if constexpr (std::is_void_v<DecayedT>) {
        std::ostringstream oss;
        ((oss << msg), ...);
        oss << "  (ASSERT_CHECK called with a void expression!)"
            << "  At " << file << ":" << line;
        return { false, oss.str() };
    }
    // Raw pointers
    else if constexpr (std::is_pointer_v<DecayedT>) {
        ok = (expr != nullptr);
    }
    // USD primitives
    else if constexpr (std::is_same_v<DecayedT, PXR_NS::UsdPrim>) {
        ok = expr.IsValid();
    }
    // (e.g expressions like x > y, smart pointers, ::testing::AssertionResult, etc)
    else {
        ok = static_cast<bool>(expr);
    }

    if (ok)
        return { true, "" };

    std::ostringstream oss;

    if constexpr (std::is_same_v<DecayedT, ::testing::AssertionResult>) {
        oss << expr.message() << " " << std::endl;
    }

    // Append any user-provided messages
    ((oss << msg), ...);
    oss << "  At " << file << ":" << line;

    return { false, oss.str() };
}

struct ArchSystemResult
{
    int exitCode;
    std::string output; // includes both stdout and stderr
};

/**
 * utf8ToWstring
 *
 * Converts a UTF-8 encoded std::string to a wide-character std::wstring.
 *
 * On Windows, many APIs dealing with file paths, environment variables,
 * or process creation (e.g., `_wfopen`, `_wstat`, `_wpopen`) expect
 * `wchar_t*` (UTF-16) strings. If a file path or argument contains non-ASCII
 *  chars then passing a UTF-8 `std::string` will lead to errors.
 *
 * */
inline std::wstring
utf8ToWstring(const std::string& str)
{
#if defined(_WIN32)
    if (str.empty())
        return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
#else
    return std::wstring(str.begin(), str.end());
#endif
}

ArchSystemResult
archSystem(const std::string& command)
/**
 * Executes a system command and captures both its output and exit code.
 *
 * This function runs a shell command using the native platform mechanism:
 * - On Windows, it uses `cmd /c` with `_popen()` to execute the command.
 * - On macOS and Linux, it uses `popen()` directly.
 *
 * In both cases, standard error (stderr) is redirected to standard output (stdout),
 * so the captured output includes all text printed by the command.
 *
 * @param command The system command to execute (e.g., "ls -l" or "dir").
 *
 * @return An `ArchSystemResult` struct containing:
 *   - `exitCode`: The exit code returned by the process (or -1 if execution failed).
 *   - `output`: The combined standard output and standard error of the command.
 *
 * @note The command runs synchronously — the function will block until it finishes.
 * @note On Windows, the exit code is obtained from `_pclose()`, and on Unix-like systems from
 * `pclose()`.
 *
 */
{
    ArchSystemResult result;
    result.exitCode = -1;
    std::array<char, 256> buffer{};
    std::string output;

#if defined(_WIN32)
    // convert command to wide string for UTF-8 support
    std::wstring wCommand = utf8ToWstring(command + " 2>&1");
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_wpopen(wCommand.c_str(), L"r"), _pclose);
    if (!pipe) {
        result.output = "Failed to open pipe";
        return result;
    }

    while (fgets(buffer.data(), (int)buffer.size(), pipe.get()) != nullptr)
        output += buffer.data();

    FILE* rawPipe = pipe.release();
    result.exitCode = _pclose(rawPipe);

#else
    std::string fullCmd = command + " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(fullCmd.c_str(), "r"), pclose);
    if (!pipe) {
        result.output = "Failed to open pipe";
        return result;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        output += buffer.data();

    FILE* rawPipe = pipe.release();
    result.exitCode = pclose(rawPipe);
#endif

    result.output = output;
    return result;
}

template<typename T>
struct Primvar
{
    PXR_NS::TfToken interpolation;
    PXR_NS::VtArray<T> values;
    PXR_NS::VtIntArray indices;
};

template<typename T>
static bool
readPrimvar(UsdGeomPrimvarsAPI& api, const TfToken& name, Primvar<T>& primvar)
{
    UsdGeomPrimvar pv = api.GetPrimvar(name);
    if (pv.IsDefined()) {
        pv.Get(&primvar.values);
        pv.GetIndices(&primvar.indices);
        primvar.interpolation = pv.GetInterpolation();
        return true;
    }
    return false;
}

/**
 * fuzzyEqual for integer types is just the == operator
 */
template<typename IntLike>
[[nodiscard]] constexpr typename std::enable_if<std::is_integral<IntLike>::value, bool>::type
fuzzyEqual(IntLike a, IntLike b)
{
    return a == b;
}

/**
 * fuzzyEqual for floating point types checks that the difference is less than an optional third
 * parameter, epsilon, that defaults to 1e-6
 */
template<typename FloatLike>
[[nodiscard]] constexpr
  typename std::enable_if<std::is_floating_point<FloatLike>::value, bool>::type
  fuzzyEqual(FloatLike a, FloatLike b, FloatLike epsilon = 1e-6)
{
    return std::abs(a - b) < epsilon;
}

/**
 * fuzzyEqual for GfVec types checks that each element is the same. An optional third parameter,
 * if not null, will be set to the index of the non equal elements if the GfVecs differ.
 *
 * Vec is a valid class for this function iff:
 * - Vec is a class
 * - Vec has elements accessible with []
 * - Each element is either an arithmetic type or a reference to one
 * - Vec has a static dimension field
 */
template<typename Vec>
[[nodiscard]] typename std::enable_if<std::is_class<Vec>::value &&
                                        std::is_arithmetic<typename std::remove_reference<
                                          decltype(std::declval<Vec>()[0])>::type>::value,
                                      bool>::type
fuzzyEqual(const Vec& a, const Vec& b, size_t* failingIndex = nullptr)
{
    for (size_t i = 0; i < Vec::dimension; ++i) {
        if (!fuzzyEqual(a[i], b[i])) {
            if (failingIndex) {
                *failingIndex = i;
            }
            return false;
        }
    }
    return true;
}

// Trait to detect GfVec types automatically
template<typename T, typename = void>
struct is_gfvec : std::false_type
{};

template<typename T>
struct is_gfvec<T, std::void_t<decltype(T::dimension), decltype(std::declval<T>()[0])>>
  : std::true_type
{};

/**
 * AssertEqValHelper
 *
 * Compares two values `actual` and `expected` and returns a ::testing::AssertionResult.
 * Different comparison methods are used based on the value types:
 * 1. Integer types (int):
 *      - Calls `fuzzyEqual for ints.
 * 2. Floating-point types (float):
 *      - Calls `fuzzyEqual` for aproximate comparison.
 *      - If an additional argument is passed, it's used as the epsilon for comparison.'
 * 3. Vector-like or container types (std::vector):
 *      - Calls 'fuzzyEqual' for element-wise comparison.
 *      - If an additional argument is passed, it's used as a failing index pointer.
 * Returns:
 *   - ::testing::AssertionSuccess() if values are equal according to the comparison rules.
 *   - ::testing::AssertionFailure() with a detailed message, failure location, and
 *     actual and expected values.
 */
template<typename T, typename U, typename... Args>
std::pair<bool, std::string>
AssertEqValHelper(const T& actual,
                  const U& expected,
                  const std::string& msg,
                  const char* file,
                  int line,
                  Args&&... args)
{
    bool equal = false;

    if constexpr ((std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) ||
                  (is_gfvec<T>::value && is_gfvec<U>::value)) {
        equal = fuzzyEqual(actual, expected, std::forward<Args>(args)...);
    } else {
        equal = (actual == expected);
    }

    if (equal) {
        return { true, "" }; // Success
    }

    std::ostringstream oss;
    oss << msg << " mismatch: " << actual << " vs " << expected << "\n"
        << "  At " << file << ":" << line;

    return { false, oss.str() };
}

template<typename T>
[[nodiscard]] ::testing::AssertionResult
assertArray(const pxr::VtArray<T>& actual,
            const ArrayData<T>& expected,
            const std::string& name) // test a subset of the array
{
    ASSERT_EQ_VAL(actual.size(), expected.size, name + " element count");
    ASSERT_GE_VAL(actual.size(),
                  expected.values.size(),
                  "There are fewer " + name + " than elements to be checked.");
    size_t i;
    for (i = 0; i < expected.values.size(); i++) {
        ASSERT_EQ_VAL(
          actual[i], expected.values[i], name + " element at index " + std::to_string(i));
    }

    return ::testing::AssertionSuccess();
}

template<typename GfVec>
[[nodiscard]] ::testing::AssertionResult
assertVec(const GfVec& actual, const GfVec& expected, std::string msg = "")
{
    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }

    // If the vectors are not equal, idx will be set to the index where they differ
    size_t idx = 0;

    ASSERT_EQ_VAL(actual, expected, msg + " Vector dimension", &idx);

    return ::testing::AssertionSuccess();
}

[[nodiscard]] ::testing::AssertionResult
assertQuatf(const PXR_NS::GfQuatf& actual,
            const PXR_NS::GfQuatf& expected,
            std::string msg = "") // test a quaternion of floats
{
    if (msg != "") {
        // Add a space after the message if it's not empty
        msg += ": ";
    }

    ASSERT_EQ_VAL(actual.GetReal(), expected.GetReal(), msg + "Real elements");
    ASSERT_CHECK(assertVec(
      actual.GetImaginary(), expected.GetImaginary(), msg + "GfQuatf imaginary component"));

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertPrim(PXR_NS::UsdStageRefPtr stage, const std::string& path)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertNode(UsdStageRefPtr stage, const std::string& path)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);
    UsdGeomXform xform(prim);
    ASSERT_CHECK(xform);

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertMesh(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MeshData& data)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);

    UsdGeomMesh geomMesh(prim);
    ASSERT_CHECK(geomMesh);

    UsdGeomPrimvarsAPI primvarsAPI(geomMesh);

    VtIntArray faceVertexCounts;
    VtIntArray faceVertexIndices;
    VtVec3fArray points;
    Primvar<GfVec3f> normals;
    Primvar<GfVec2f> uvs;
    Primvar<PXR_NS::GfVec3f> displayColor;
    Primvar<float> displayOpacity;
    geomMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts, 0);
    geomMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices, 0);
    geomMesh.GetPointsAttr().Get(&points, 0);
    UsdAttribute normalsAttr = geomMesh.GetNormalsAttr();
    if (readPrimvar(primvarsAPI, UsdGeomTokens->normals, normals)) {
    } else if (normalsAttr.IsAuthored()) {
        normalsAttr.Get(&normals.values, 0);
        normals.interpolation = geomMesh.GetNormalsInterpolation();
    }
    if (readPrimvar(primvarsAPI, TfToken("st"), uvs)) {
    } else if (readPrimvar(primvarsAPI, TfToken("st0"), uvs)) {
    } else if (readPrimvar(primvarsAPI, TfToken("UVMap"), uvs)) {
    }
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayColor, displayColor);
    readPrimvar(primvarsAPI, UsdGeomTokens->primvarsDisplayOpacity, displayOpacity);

    ASSERT_CHECK(assertArray(faceVertexCounts, data.faceVertexCounts, "faceVertexCounts"));
    ASSERT_CHECK(assertArray(faceVertexIndices, data.faceVertexIndices, "faceVertexIndices"));
    ASSERT_CHECK(assertArray(points, data.points, "points"));
    ASSERT_CHECK(assertArray(normals.values, data.normals.values, "normals.values"));
    ASSERT_CHECK(assertArray(normals.indices, data.normals.indices, "normals.indices"));
    ASSERT_CHECK(assertArray(uvs.values, data.uvs.values, "uvs.values"));
    ASSERT_CHECK(assertArray(uvs.indices, data.uvs.indices, "uvs.indices"));
    ASSERT_CHECK(assertArray(displayColor.values, data.displayColor.values, "displayColor.values"));
    ASSERT_CHECK(
      assertArray(displayColor.indices, data.displayColor.indices, "displayColor.indices"));
    ASSERT_CHECK(
      assertArray(displayOpacity.values, data.displayOpacity.values, "displayOpacity.values"));
    ASSERT_CHECK(
      assertArray(displayOpacity.indices, data.displayOpacity.indices, "displayOpacity.indices"));

    if (normals.indices.size()) {
        ASSERT_EQ_VAL(normals.interpolation, data.normals.interpolation, "Normals interpolation");
    }
    if (uvs.indices.size() && uvs.interpolation != data.uvs.interpolation) {
        ASSERT_EQ_VAL(uvs.interpolation, data.uvs.interpolation, "Normals interpolation");
    }
    if (displayColor.indices.size()) {
        ASSERT_EQ_VAL(displayColor.interpolation,
                      data.displayColor.interpolation,
                      "DisplayColor interpolation");
    }
    if (displayOpacity.indices.size()) {
        ASSERT_EQ_VAL(displayOpacity.interpolation,
                      data.displayOpacity.interpolation,
                      "DisplayOpacity interpolation");
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertAnimation(PXR_NS::UsdStageRefPtr stage, const std::string& path, const AnimationData& data)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);

    // We only test the animation samples given in the data. This means that the data can have fewer
    // samples than the actual USD, so that the entire animation doesn't have to be copied into the
    // test

    for (auto pair = data.orient.begin(); pair != data.orient.end(); pair++) {
        float time = pair->first;
        GfQuatf orientValue;
        extractUsdAttribute<GfQuatf>(
          prim, TfToken("xformOp:orient"), &orientValue, UsdTimeCode(time));
        ASSERT_CHECK(assertQuatf(orientValue,
                                 data.orient.at(time),
                                 std::string("xformOp:orient[") + std::to_string(time) + "]"));
    }

    for (auto pair = data.scale.begin(); pair != data.scale.end(); pair++) {
        float time = pair->first;
        GfVec3f scaleValue;
        extractUsdAttribute<GfVec3f>(
          prim, TfToken("xformOp:scale"), &scaleValue, UsdTimeCode(time));
        ASSERT_CHECK(assertVec(scaleValue,
                               data.scale.at(time),
                               std::string("xformOp:scale[") + std::to_string(time) + "]"));
    }

    for (auto pair = data.translate.begin(); pair != data.translate.end(); pair++) {
        float time = pair->first;
        GfVec3d translateValue;
        extractUsdAttribute<GfVec3d>(
          prim, TfToken("xformOp:translate"), &translateValue, UsdTimeCode(time));
        ASSERT_CHECK(assertVec(translateValue,
                               data.translate.at(time),
                               std::string("xformOp:translate[") + std::to_string(time) + "]"));
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertCamera(PXR_NS::UsdStageRefPtr stage, const std::string& path, const CameraData& cameraData)
{
    const bool WARN_IF_ATTRIBUTE_NOT_FOUND = false;

    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);

    // The transformations for cameras in our USD assets tend to be stored by a parent node. We
    // first verify that the camera's transform is correct by extracting it from the prim's parent

    UsdPrim parent = prim.GetParent();
    ASSERT_CHECK(parent);

    GfVec3d translation;
    GfQuatf rotation;
    GfVec3f scale;

    if (extractUsdAttribute<GfVec3d>(parent, TfToken("xformOp:translate"), &translation)) {
        ASSERT_CHECK(assertVec(
          translation, cameraData.translate, path + "'s parent translation does not match"));
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No translation attribute found for %s\n", path.c_str());
    }

    if (extractUsdAttribute<GfQuatf>(parent, TfToken("xformOp:orient"), &rotation)) {
        ASSERT_CHECK(
          assertQuatf(rotation, cameraData.orient, path + "'s parent rotation does not match"));
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No rotation attribute found for %s\n", path.c_str());
    }

    if (extractUsdAttribute<GfVec3f>(parent, TfToken("xformOp:scale"), &scale)) {
        ASSERT_CHECK(assertVec(scale, cameraData.scale, path + "'s parent scale does not match"));
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No scale attribute found for %s\n", path.c_str());
    }

    // Next, we check the camera data itself

    UsdGeomCamera camera(prim);
    ASSERT_CHECK(camera, "camera at path: " + path);

    GfVec2f clippingRange;
    if (camera.GetClippingRangeAttr().Get(&clippingRange)) {
        ASSERT_CHECK(assertVec(
          clippingRange, cameraData.clippingRange, path + "'s clipping range does not match"));
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No clipping range attribute found for %s\n", path.c_str());
    }

    float focalLength;
    if (camera.GetFocalLengthAttr().Get(&focalLength)) {
        ASSERT_EQ_VAL(focalLength, cameraData.focalLength, path + " focal length");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No focal length attribute found for %s\n", path.c_str());
    }

    float focusDistance;
    if (camera.GetFocusDistanceAttr().Get(&focusDistance)) {
        ASSERT_EQ_VAL(focusDistance, cameraData.focusDistance, path + "'s focus distance");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No focus distance attribute found for %s\n", path.c_str());
    }

    float fStop;
    if (camera.GetFStopAttr().Get(&fStop)) {
        ASSERT_EQ_VAL(fStop, cameraData.fStop, path + " fStop does not match");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No fStop attribute found for %s\n", path.c_str());
    }

    float horizontalAperture;
    if (camera.GetHorizontalApertureAttr().Get(&horizontalAperture)) {
        ASSERT_EQ_VAL(
          horizontalAperture, cameraData.horizontalAperture, path + "'s horizontal aperture");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No horizontal aperture attribute found for %s\n", path.c_str());
    }

    TfToken projection;
    if (camera.GetProjectionAttr().Get(&projection)) {
        ASSERT_EQ_VAL(projection, TfToken(cameraData.projection), path + "'s projection");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No projection attribute found for %s\n", path.c_str());
    }

    float verticalAperture;
    if (camera.GetVerticalApertureAttr().Get(&verticalAperture)) {
        ASSERT_EQ_VAL(verticalAperture, cameraData.verticalAperture, path + "'s vertical aperture");
    } else if (WARN_IF_ATTRIBUTE_NOT_FOUND) {
        TF_WARN("No vertical aperture attribute found for %s\n", path.c_str());
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertLight(PXR_NS::UsdStageRefPtr stage, const std::string& path, const LightData& lightData)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);

    // The transformations for lights in our USD assets tend to be stored by a parent node. We
    // first verify that the light's transform is correct by extracting it from the prim's parent

    UsdPrim parent = prim.GetParent();
    ASSERT_CHECK(parent);

    GfVec3d translation;
    GfQuatf rotation;
    GfVec3f scale;

    // The transformations for lights in our USD assets tend to be stored by a parent node
    if (lightData.translation) {
        ASSERT_CHECK(
          extractUsdAttribute<GfVec3d>(parent, TfToken("xformOp:translate"), &translation),
          "Expected translate attribute at " + path);
        ASSERT_CHECK(
          assertVec(translation, lightData.translation.value(), path + " parent translation"));
    }

    if (lightData.rotation) {
        ASSERT_CHECK(extractUsdAttribute<GfQuatf>(parent, TfToken("xformOp:orient"), &rotation),
                     "Expected orient attribute at " + path);
        ASSERT_CHECK(assertQuatf(rotation, lightData.rotation.value(), path + " parent rotation"));
    }

    if (lightData.scale) {
        ASSERT_CHECK(extractUsdAttribute<GfVec3f>(parent, TfToken("xformOp:scale"), &scale),
                     "Expected scale attribute not found for " + path);
        ASSERT_CHECK(assertVec(scale, lightData.scale.value(), path + " parent scale"));
    }

    // Next, we check the light data itself

    if (prim.IsA<UsdLuxSphereLight>()) {
        UsdLuxSphereLight sphereLight(prim);
        ASSERT_CHECK(sphereLight, "UsdLuxSphereLight for path " + path);

        if (lightData.color) {
            PXR_NS::GfVec3f color;
            ASSERT_CHECK(sphereLight.GetColorAttr().Get(&color),
                         "Expected color attribute at " + path);
            ASSERT_CHECK(assertVec(color, lightData.color.value(), path + " color"));
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_CHECK(sphereLight.GetIntensityAttr().Get(&intensity),
                         "Expected intensity attribute at " + path);
            ASSERT_EQ_VAL(
              intensity, lightData.intensity.value(), path + " intensity", LIGHT_INTENSITY_EPSILON);
        }

        if (lightData.radius) {
            float radius;
            ASSERT_CHECK(sphereLight.GetRadiusAttr().Get(&radius),
                         "Expected radius attribute at " + path);
            ASSERT_EQ_VAL(radius, lightData.radius.value(), path + "'s radius");
        }

    } else if (prim.IsA<UsdLuxDistantLight>()) {
        UsdLuxDistantLight distantLight(prim);
        ASSERT_CHECK(distantLight, "Distant light for path " + path);

        if (lightData.color) {
            PXR_NS::GfVec3f color;
            ASSERT_CHECK(distantLight.GetColorAttr().Get(&color),
                         "Expected color attribute at " + path);
            ASSERT_CHECK(
              assertVec(color, lightData.color.value(), path + " color does not match\n"));
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_CHECK(distantLight.GetIntensityAttr().Get(&intensity),
                         "Intensity attribute at " + path);
            ASSERT_EQ_VAL(intensity,
                          lightData.intensity.value(),
                          path + "'s intensity",
                          LIGHT_INTENSITY_EPSILON);
        }

        // Distant lights don't have a radius

    } else if (prim.IsA<UsdLuxDiskLight>()) {
        UsdLuxDiskLight diskLight(prim);
        ASSERT_CHECK(diskLight, "Disk light for path " + path);

        if (lightData.color) {
            GfVec3f color;
            ASSERT_CHECK(diskLight.GetColorAttr().Get(&color),
                         "Expected color attribute at " + path);
            ASSERT_CHECK(assertVec(color, lightData.color.value(), path + " color"));
        }

        if (lightData.intensity) {
            float intensity;
            ASSERT_CHECK(diskLight.GetIntensityAttr().Get(&intensity),
                         "Expected intensity attribute at " + path);
            ASSERT_EQ_VAL(intensity,
                          lightData.intensity.value(),
                          path + "'s light intensity",
                          LIGHT_INTENSITY_EPSILON);
        }

        if (lightData.radius) {
            float radius;
            ASSERT_CHECK(diskLight.GetRadiusAttr().Get(&radius),
                         "Expected radius attribute at " + path);
            ASSERT_EQ_VAL(radius, lightData.radius.value(), path + "'s radius");
        }

    } else if (prim.IsA<UsdLuxRectLight>()) {
        return ::testing::AssertionFailure()
               << "lights are not supported yet on import; Rectangle lights at " << path;

        /*
        Uncomment this once we support import of rectangle lights
        UsdLuxRectLight rectLight(prim);
        ASSERT_TRUE(rectLight);
        ASSERT_VEC(rectLight.color, lightData.color);
        ASSERT_FLOAT_EQ(rectLight.intensity, lightData.intensity);
        ADD_FAILURE() << "Rectangle lights not supported yet: " << path << "\n";
        return false;

        // Rectangle specific attributes
        ASSERT_FLOAT_EQ(rectLight.length[0], lightData.length[0]);
        ASSERT_FLOAT_EQ(rectLight.length[1], lightData.length[1]);
        */
    } else if (prim.IsA<UsdLuxDomeLight>()) {
        return ::testing::AssertionFailure()
               << "Dome lights are not supported yet on import; Dome lights at " << path;

        /*
        Uncomment this once we support import of dome lights
        UsdLuxDomeLight domeLight(prim);
        ASSERT_TRUE(domeLight);
        ASSERT_VEC(domeLight.color, lightData.color);
        ASSERT_FLOAT_EQ(domeLight.intensity, lightData.intensity);
         // Add texture test once we support this on import
        */
    } else {
        return ::testing::AssertionFailure()
               << "Encountered prim of type " << prim.GetTypeName().GetString() << " at " << path;
    }

    // Spotlights use shaping APIs
    if (prim.HasAPI<UsdLuxShapingAPI>()) {
        UsdLuxShapingAPI shapingAPI(prim);

        // Shaping API specific attributes

        if (lightData.coneAngle) {
            float coneAngle;
            ASSERT_CHECK(shapingAPI.GetShapingConeAngleAttr().Get(&coneAngle),
                         "Cone angle attribute at " + path);
            ASSERT_EQ_VAL(coneAngle, lightData.coneAngle.value(), path + "'s cone angle");
        }

        if (lightData.coneFalloff) {
            float coneFalloff;
            ASSERT_CHECK(shapingAPI.GetShapingConeSoftnessAttr().Get(&coneFalloff),
                         "Cone falloff attribute at " + path);
            ASSERT_EQ_VAL(coneFalloff, lightData.coneFalloff.value(), path + "'s cone falloff");
        }
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertDisplayName(PXR_NS::UsdStageRefPtr stage,
                  const std::string& primPath,
                  const std::string& displayName)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(primPath));
    ASSERT_CHECK(prim, "Proper dislay name for prim at " + primPath);
    ASSERT_EQ_VAL(prim.GetDisplayName(), displayName, primPath + " display name");

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertVisibility(PXR_NS::UsdStageRefPtr stage,
                 const std::string& path,
                 bool expectedVisibilityAttr,
                 bool expectedActualVisibility)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));

    UsdGeomImageable imageable(prim);
    ASSERT_CHECK(imageable, "imageable prim at path: " + path + " ");

    // Visibility attribute should always be present, even if it's not written explicitly
    ASSERT_CHECK(imageable.GetVisibilityAttr().HasValue(),
                 "Visibility attribute on prim at path: " + path);

    // Check visibility attribute
    TfToken visibility;
    imageable.GetVisibilityAttr().Get<TfToken>(&visibility);
    ASSERT_EQ_VAL(expectedVisibilityAttr,
                  visibility == UsdGeomTokens->inherited,
                  path + "'s visibility attribute");

    // Check computed (actual) visibility
    visibility = imageable.ComputeVisibility();
    ASSERT_EQ_VAL(expectedActualVisibility,
                  visibility == UsdGeomTokens->inherited,
                  path + "'s computed visibility");

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertPoints(PXR_NS::UsdStageRefPtr stage, const std::string& path, const PointsData& data)
{
    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(SdfPath(path));
    ASSERT_CHECK(prim);
    UsdGeomPoints geomPoints(prim);
    ASSERT_CHECK(geomPoints);

    VtVec3fArray points;
    geomPoints.GetPointsAttr().Get(&points, 0);
    ASSERT_EQ_VAL(points.size(), data.pointsCount, "points count");

    return ::testing::AssertionSuccess();
}

template<typename T>
[[nodiscard]] ::testing::AssertionResult
assertInputField(const pxr::UsdShadeShader& shader, const std::string& name, const T& value)
{
    auto attr = shader.GetInput(pxr::TfToken(name));
    if (attr) {
        auto valueAttrs = attr.GetValueProducingAttributes();
        if (valueAttrs.size()) {
            T actual;
            valueAttrs.front().Get(&actual);
            ASSERT_EQ_VAL(
              actual, value, "Input field " + name + " for shader " + shader.GetPath().GetString());
        }
    }
    return ::testing::AssertionSuccess();
    // TODO check if attr missing
}

// Cannot reuse assertInputField for this since SdfAssetPath equality fails for ::resolvedPath,
// so here we match only the ::assetPath.
[[nodiscard]] ::testing::AssertionResult
assertInputPath(const pxr::UsdShadeShader& shader,
                const std::string& name,
                const std::string& value)
{
    auto attr = shader.GetInput(pxr::TfToken(name));

    if (attr) {
        auto valueAttrs = attr.GetValueProducingAttributes();
        if (valueAttrs.size()) {
            pxr::SdfAssetPath actualAssetPath;
            valueAttrs.front().Get(&actualAssetPath);
            const std::string actual = pxr::TfNormPath(actualAssetPath.GetAssetPath());
            ASSERT_EQ_VAL(
              actual, value, "Input path " + name + " for shader " + shader.GetPath().GetString());
        }
    }
    return ::testing::AssertionSuccess();
    // TODO check if attr missing
}

::testing::AssertionResult
assertMaterial(PXR_NS::UsdStageRefPtr stage, const std::string& path, const MaterialData& data)
{
    const std::string currentDir = TfAbsPath(".");
    const SdfPath materialPath = SdfPath(path);

    ASSERT_CHECK(stage);
    UsdPrim prim = stage->GetPrimAtPath(materialPath);
    ASSERT_CHECK(prim);

    UsdShadeMaterial material(prim);
    ASSERT_CHECK(material);

    SdfPathVector connections;
    UsdShadeShader shader;
    UsdAttribute surface = material.GetSurfaceAttr();
    surface.GetConnections(&connections);
    ASSERT_CHECK(connections.size() > 0);

    const SdfPath shaderPath = connections[0].GetPrimPath();
    shader = UsdShadeShader(stage->GetPrimAtPath(shaderPath));
    TfToken shaderId;
    shader.GetShaderId(&shaderId);
    ASSERT_CHECK(shaderId == TfToken("UsdPreviewSurface"));

    auto assertInput = [&](const TfToken& name, const InputData& data) {
        UsdShadeInput shadeInput = shader.GetInput(name);
        if (!shadeInput)
            return ::testing::AssertionSuccess();
        if (shadeInput.HasConnectedSource()) {
            UsdShadeInput::SourceInfoVector sources = shadeInput.GetConnectedSources();
            for (UsdShadeConnectionSourceInfo source : sources) {
                const SdfPath sourcePath = source.source.GetPath();
                const UsdShadeShader textureShader =
                  UsdShadeShader(stage->GetPrimAtPath(sourcePath));
                if (sourcePath == materialPath) {
                    ASSERT_CHECK(assertInputField(textureShader, name, data.value));
                } else {
                    const SdfPath textureShaderPath = textureShader.GetPath();
                    TfToken uvTextureId;
                    textureShader.GetShaderId(&uvTextureId);
                    ASSERT_EQ_VAL(uvTextureId.GetString(),
                                  std::string("UsdUVTexture"),
                                  "Shader at path " + textureShaderPath.GetString() +
                                    " is not a UsdUVTexture");
                    const std::string assetPath = TfNormPath(currentDir + "/" + data.file);
                    ASSERT_CHECK(assertInputPath(textureShader, "file", assetPath));
                    // TODO? ASSERT_IMAGE(ctx, assetPath, input.image);
                    ASSERT_CHECK(assertInputField(textureShader, "wrapS", data.wrapS));
                    ASSERT_CHECK(assertInputField(textureShader, "wrapT", data.wrapT));
                    ASSERT_CHECK(assertInputField(textureShader, "scale", data.scale));
                    ASSERT_CHECK(assertInputField(textureShader, "bias", data.bias));
                    ASSERT_CHECK(assertInputField(textureShader, "fallback", data.value));
                    ASSERT_EQ_VAL(data.channel,
                                  source.sourceName,
                                  "Source name for shader at path " +
                                    textureShaderPath.GetString());

                    UsdShadeInput stInput = textureShader.GetInput(TestTokens->st);
                    if (!stInput)
                        return ::testing::AssertionSuccess();
                    if (stInput.HasConnectedSource()) {
                        UsdShadeInput::SourceInfoVector sources = stInput.GetConnectedSources();
                        for (UsdShadeConnectionSourceInfo source : sources) {
                            const SdfPath sourcePath = source.source.GetPath();
                            UsdShadeShader stShader =
                              UsdShadeShader(stage->GetPrimAtPath(sourcePath));
                            TfToken shaderId;
                            stShader.GetShaderId(&shaderId);
                            if (!data.uvRotation.IsEmpty() || !data.uvScale.IsEmpty() ||
                                !data.uvTranslation.IsEmpty()) {
                                ASSERT_CHECK(shaderId == TestTokens->UsdTransform2d);
                                ASSERT_CHECK(
                                  assertInputField(stShader, "rotation", data.uvRotation));
                                ASSERT_CHECK(assertInputField(stShader, "scale", data.uvScale));
                                ASSERT_CHECK(
                                  assertInputField(stShader, "translation", data.uvTranslation));
                            } else {
                                std::string shaderName = stShader.GetPrim().GetName().GetString();
                                if (shaderName == "texCoordReader") {
                                    ASSERT_CHECK(shaderId == TestTokens->UsdPrimvarReader_float2);
                                } else {
                                    ASSERT_CHECK(shaderId == TestTokens->UsdTransform2d);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            VtValue actualValue;
            shadeInput.Get(&actualValue);
            ASSERT_CHECK(actualValue == data.value,
                         "Actual value = " + TfStringify(actualValue) +
                           ", expected value = " + TfStringify(data.value));
        }
        return ::testing::AssertionSuccess();
    };

    ASSERT_CHECK(assertInput(TestTokens->useSpecularWorkflow, data.useSpecularWorkflow));
    ASSERT_CHECK(assertInput(TestTokens->diffuseColor, data.diffuseColor));
    ASSERT_CHECK(assertInput(TestTokens->emissiveColor, data.emissiveColor));
    ASSERT_CHECK(assertInput(TestTokens->specularColor, data.specularColor));
    ASSERT_CHECK(assertInput(TestTokens->normal, data.normal));
    ASSERT_CHECK(assertInput(TestTokens->metallic, data.metallic));
    ASSERT_CHECK(assertInput(TestTokens->roughness, data.roughness));
    ASSERT_CHECK(assertInput(TestTokens->clearcoat, data.clearcoat));
    ASSERT_CHECK(assertInput(TestTokens->clearcoatRoughness, data.clearcoatRoughness));
    ASSERT_CHECK(assertInput(TestTokens->opacity, data.opacity));
    ASSERT_CHECK(assertInput(TestTokens->opacityThreshold, data.opacityThreshold));
    ASSERT_CHECK(assertInput(TestTokens->displacement, data.displacement));
    ASSERT_CHECK(assertInput(TestTokens->occlusion, data.occlusion));
    ASSERT_CHECK(assertInput(TestTokens->ior, data.ior));

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertRender(const std::string& filename, const std::string& imageFilename)
{
    const std::string imageParentPath = TfGetPathName(imageFilename);
    TfMakeDirs(imageParentPath, -1, true);

    // Build the platform-appropriate command
#if defined(_WIN32)
    // On Windows, set env var via `set` and `&&`
    const std::string command =
      "set HYDRA_ENABLE_HGIGL=0 && usdrecord \"" + filename + "\" \"" + imageFilename + "\"";
#else
    // On Unix-like systems, use inline env var syntax
    const std::string command =
      "HYDRA_ENABLE_HGIGL=0 usdrecord \"" + filename + "\" \"" + imageFilename + "\"";
#endif

    ArchSystemResult result = archSystem(command);
    ASSERT_EQ_VAL(result.exitCode, 0, "usdrecord exit code:\n" + result.output);

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
assertUsda(const SdfLayerHandle& sdfLayer,
           const std::string& baselinePath,
           bool generateBaseline,
           bool dumpOnFailure)
{
    ASSERT_CHECK(sdfLayer, "SdfLayer is invalid");

    if (generateBaseline) {
        std::cout << "Updating USDA baseline " << baselinePath << std::endl;
        sdfLayer->Export(baselinePath);
    }

    SdfLayerRefPtr baselineLayer = SdfLayer::FindOrOpen(baselinePath);
    ASSERT_CHECK(baselineLayer, "Failed to load baseline layer from " + baselinePath);

    std::string layerStr;
    sdfLayer->ExportToString(&layerStr);
    std::string baselineStr;
    baselineLayer->ExportToString(&baselineStr);

    // Skip the header (up to and including the doc string) which contains version/date info
    auto skipHeader = [](const std::string& str) -> size_t {
        // Find the end of the doc string: look for closing '''
        size_t docStart = str.find("doc = '''");
        if (docStart != std::string::npos) {
            // Find the closing ''' (skip past the opening ''')
            size_t docEnd = str.find("'''", docStart + 9);
            if (docEnd != std::string::npos) {
                // Skip to next newline after closing '''
                docEnd = str.find('\n', docEnd + 3);
                if (docEnd != std::string::npos) {
                    return docEnd + 1;
                }
            }
        }
        return 0; // If we can't find the pattern, compare from start
    };

    // Normalize asset paths to ignore build-specific directories
    auto normalizeAssetPaths = [](const std::string& str) -> std::string {
        std::string result = str;
        size_t pos = 0;

        // Find patterns like: asset inputs:xxx = @/path/to/file.glb[texture.png]@ (
        // But exclude patterns with dots in the input name like: asset inputs:file.connect
        while ((pos = result.find("asset inputs:", pos)) != std::string::npos) {
            // Find the equals sign
            size_t equalsPos = result.find(" = ", pos);
            if (equalsPos == std::string::npos)
                break;

            // Check if there's a dot between "inputs:" and " = " (would indicate .connect, etc)
            std::string inputName =
              result.substr(pos + 13, equalsPos - (pos + 13)); // "inputs:" is 13 chars
            if (inputName.find('.') != std::string::npos) {
                pos = equalsPos + 3;
                continue; // Skip this one, it has a dot (like file.connect)
            }

            // Find the opening @
            size_t atStartPos = result.find("@", equalsPos);
            if (atStartPos == std::string::npos || atStartPos > equalsPos + 10) {
                pos = equalsPos + 3;
                continue;
            }

            // Find the closing @
            size_t atEndPos = result.find("@", atStartPos + 1);
            if (atEndPos == std::string::npos)
                break;

            // Extract the full path between the @ symbols
            std::string fullPath = result.substr(atStartPos + 1, atEndPos - atStartPos - 1);

            // Keep only the filename part (after last / or \)
            size_t lastSlash = fullPath.find_last_of("/\\");
            std::string normalizedPath =
              (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;

            // Replace the full path with just the filename
            result.replace(atStartPos + 1, atEndPos - atStartPos - 1, normalizedPath);

            pos = atStartPos + normalizedPath.length() + 2;
        }

        return result;
    };

    size_t layerStart = skipHeader(layerStr);
    size_t baselineStart = skipHeader(baselineStr);

    std::string layerContent = normalizeAssetPaths(layerStr.substr(layerStart));
    std::string baselineContent = normalizeAssetPaths(baselineStr.substr(baselineStart));

    if (layerContent != baselineContent) {
        if (dumpOnFailure) {
            std::cout << "Layer content has length: " << layerContent.size()
                      << "\nBaseline content has length: " << baselineContent.size()
                      << " (compared without header)" << std::endl;

            const std::string searchStr = "baseline_";
            const std::string replaceStr = "output_";
            std::string outputPath = baselinePath;
            size_t pos = outputPath.find(searchStr);
            if (pos != std::string::npos) {
                outputPath.replace(pos, searchStr.length(), replaceStr);
            } else {
                return ::testing::AssertionFailure()
                       << "Expected '" << searchStr << "' in baseline path '" << baselinePath
                       << "'";
            }
            std::fstream out(outputPath, std::ios::out);
            out << layerContent;
            out.close();
            std::cout << "Output dumped to " << outputPath << " (without header)" << std::endl;

            // Very poor person's diff operation. Can we do better without bringing
            // in a diff library?
            for (size_t i = 0; i < layerContent.size(); ++i) {
                if (i >= baselineContent.size()) {
                    std::cout << "Size difference. Output has more characters than baseline"
                              << std::endl;
                    break;
                }
                if (layerContent[i] != baselineContent[i]) {
                    std::cout << "Mismatch at char " << i << " (after header)" << std::endl;
                    std::cout << "Remainder in output:\n" << &layerContent[i] << std::endl;
                    std::cout << "Remainder in baseline:\n" << &baselineContent[i] << std::endl;
                    break;
                }
            }
        }

        return ::testing::AssertionFailure() << "Output of layer " << sdfLayer->GetIdentifier()
                                             << " does not match baseline " << baselinePath;
    }
    return ::testing::AssertionSuccess();
}
