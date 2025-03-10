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
#include <fileformatutils/geometry.h>
#include <fileformatutils/debugCodes.h>

using namespace PXR_NS;

namespace adobe::usd {

bool
checkIndexRange(const VtIntArray& indices,
                int minValue,
                int maxValue, // range is [minValue, maxValue)
                int& foundMinValue,
                int& foundMaxValue,
                std::vector<size_t>& invalidIndices,
                size_t* indexSumOut = nullptr)
{
    invalidIndices.clear();
    foundMinValue = std::numeric_limits<int>::max();
    foundMaxValue = std::numeric_limits<int>::min();

    size_t numIndices = indices.size();
    size_t indexSum = 0;
    for (size_t i = 0; i < numIndices; ++i) {
        int idx = indices[i];
        if (idx < minValue || idx >= maxValue) {
            invalidIndices.push_back(i);
        }
        foundMinValue = std::min(idx, foundMinValue);
        foundMaxValue = std::max(idx, foundMaxValue);
        indexSum += idx;
    }

    if (indexSumOut != nullptr) {
        *indexSumOut = indexSum;
    }

    return invalidIndices.empty();
}

std::string
summarizeIndices(const std::vector<size_t>& indices, size_t maxIndexCount = 5)
{
    size_t count = std::min(indices.size(), maxIndexCount);

    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < count; ++i) {
        ss << indices[i];
        if (i + 1 < count) {
            ss << ", ";
        }
    }
    if (indices.size() > count) {
        ss << ", ...";
    }
    ss << "]";
    return ss.str();
}

template<typename T>
size_t
checkFiniteFloats(const VtArray<T>& array, std::vector<size_t>& invalidIndices)
{
    const float* floats = reinterpret_cast<const float*>(array.data());
    const size_t elementCount = sizeof(T) / sizeof(float);
    const size_t numFloats = array.size() * elementCount;

    for (size_t i = 0; i < numFloats; ++i) {
        if (!std::isfinite(floats[i])) {
            invalidIndices.push_back(i / elementCount);
        }
    }

    return invalidIndices.size();
}

// This macro expects to have a const size_t tempBufferSize and a char tempBuffer[tempBufferSize]
// in local scope to use as a reusable staging buffer for the message.
// It also expects a string path, which is used in the message.
// It also expects a bool foundError, which it sets to true if an Error is encountered.
#define LOG_ISSUE(level, format, ...)                                                              \
    if (issues != nullptr) {                                                                       \
        int n = snprintf(tempBuffer, tempBufferSize, format, ##__VA_ARGS__);                       \
        if (n > 0 && n < tempBufferSize) {                                                         \
            issues->push_back(Issue{ level, path, tempBuffer });                                   \
        }                                                                                          \
    }                                                                                              \
    if (level == Issue::Level::Error) {                                                            \
        foundError = true;                                                                         \
    }

bool
verifyTopology(const std::string& path,
               const size_t numPoints,
               const VtIntArray& faceVertexCounts,
               const VtIntArray& faceVertexIndices,
               IssueVector* issues,
               const MeshVerificationOptions& options)
{
    using Lvl = Issue::Level;
    const int tempBufferSize = 1024;
    char tempBuffer[tempBufferSize];
    bool foundError = false;

    size_t numFaces = faceVertexCounts.size();
    size_t numFaceVertices = faceVertexIndices.size();
    if (numPoints == 0) {
        LOG_ISSUE(Lvl::Warning, "No points on mesh");
    }
    if (numFaces == 0) {
        LOG_ISSUE(Lvl::Warning, "No faces on mesh");
    }

    std::vector<size_t> invalidIndices;
    int foundMinValue, foundMaxValue;

    // Verify face vertex count data
    size_t totalFaceVertices = 0;
    // We need at least triangles
    const int minFaceVertexCountThreshold = 3;
    if (!checkIndexRange(faceVertexCounts,
                         minFaceVertexCountThreshold,
                         std::numeric_limits<int>::max(),
                         foundMinValue,
                         foundMaxValue,
                         invalidIndices,
                         &totalFaceVertices)) {
        std::string indexSummary = summarizeIndices(invalidIndices);
        LOG_ISSUE(Lvl::Error,
                  "Invalid face vertex counts (%zu). Faces %s are out of range [%d, inf)",
                  invalidIndices.size(),
                  indexSummary.c_str(),
                  minFaceVertexCountThreshold);
    }
    if (options.highFaceVertexCountWarningThreshold > 0) {
        int threshold = options.highFaceVertexCountWarningThreshold;
        if (!checkIndexRange(faceVertexCounts,
                             std::numeric_limits<int>::min(),
                             threshold,
                             foundMinValue,
                             foundMaxValue,
                             invalidIndices)) {
            std::string indexSummary = summarizeIndices(invalidIndices);
            LOG_ISSUE(Lvl::Warning,
                      "Found (%zu) faces with high vertex counts. "
                      "Faces %s have more than %d vertices (max %d)",
                      invalidIndices.size(),
                      indexSummary.c_str(),
                      threshold,
                      foundMaxValue);
        }
    }
    if (totalFaceVertices != numFaceVertices) {
        LOG_ISSUE(Lvl::Error,
                  "Based on the vertex counts of %zu faces we expect %zu face vertices, "
                  "but we have %zu",
                  numFaces,
                  totalFaceVertices,
                  numFaceVertices);
    }

    // Verify face vertex data
    if (!checkIndexRange(
          faceVertexIndices, 0, numPoints, foundMinValue, foundMaxValue, invalidIndices)) {
        std::string indexSummary = summarizeIndices(invalidIndices);
        LOG_ISSUE(Lvl::Error,
                  "Invalid vertex indices (%zu). Indices %s are out of range [%d, numPoints=%zu)",
                  invalidIndices.size(),
                  indexSummary.c_str(),
                  0,
                  numPoints);
    }
    int maxVertexIndex = (int)(numPoints - 1);
    if (numPoints > 0 && (foundMinValue != 0 || foundMaxValue < maxVertexIndex)) {
        LOG_ISSUE(Lvl::Error,
                  "Vertex indices only cover range [%d, %d], which is not all points [0, %d]",
                  foundMinValue,
                  foundMaxValue,
                  maxVertexIndex);
    }

    return !foundError;
}

template<typename T>
bool
verifyPrimvar(const std::string& path,
              const size_t numPoints,
              const size_t numFaces,
              const size_t numFaceVertices,
              const Primvar<T>& primvar,
              const std::string& primvarName,
              bool checkForFiniteFloats,
              IssueVector* issues)
{
    using Lvl = Issue::Level;

    if (primvar.values.empty()) {
        return true;
    }

    const int tempBufferSize = 1024;
    char tempBuffer[tempBufferSize];
    bool foundError = false;

    size_t expectedNumValues = 0;
    if (primvar.interpolation == UsdGeomTokens->constant) {
        // Constant is a single value per mesh
        if (primvar.values.size() != 1) {
            LOG_ISSUE(Lvl::Error,
                      "Constant primvar '%s' has %zu values, but should have 1",
                      primvarName.c_str(),
                      primvar.values.size());
        }
        if (!primvar.indices.empty()) {
            LOG_ISSUE(Lvl::Error,
                      "Constant primvar '%s' has %zu indices, which should not be the case",
                      primvarName.c_str(),
                      primvar.indices.size());
        }
        return !foundError;
    } else if (primvar.interpolation == UsdGeomTokens->uniform) {
        // Uniform means a single value per face
        expectedNumValues = numFaces;
    } else if (primvar.interpolation == UsdGeomTokens->vertex) {
        // Vertex means a single value per point of the mesh
        expectedNumValues = numPoints;
    } else if (primvar.interpolation == UsdGeomTokens->varying ||
               primvar.interpolation == UsdGeomTokens->faceVarying) {
        // Varying and faceVarying means a single value per corner of every face
        expectedNumValues = numFaceVertices;
    }

    if (primvar.indices.empty()) {
        if (primvar.values.size() != expectedNumValues) {
            LOG_ISSUE(Lvl::Error,
                      "%s primvar '%s' has %zu values, but should have %zu",
                      primvar.interpolation.GetText(),
                      primvarName.c_str(),
                      primvar.values.size(),
                      expectedNumValues);
        }
    } else {
        if (primvar.indices.size() != expectedNumValues) {
            LOG_ISSUE(Lvl::Error,
                      "%s primvar '%s' has %zu indices, but should have %zu",
                      primvar.interpolation.GetText(),
                      primvarName.c_str(),
                      primvar.indices.size(),
                      expectedNumValues);
        }

        size_t numValues = primvar.values.size();
        int foundMinValue, foundMaxValue;
        std::vector<size_t> invalidIndices;
        if (!checkIndexRange(
              primvar.indices, 0, numValues, foundMinValue, foundMaxValue, invalidIndices)) {
            std::string indexSummary = summarizeIndices(invalidIndices);
            LOG_ISSUE(Lvl::Error,
                      "%s primvar '%s' has %zu invalid indices. "
                      "Indices %s are out of range [%d, numValues=%zu)",
                      primvar.interpolation.GetText(),
                      primvarName.c_str(),
                      invalidIndices.size(),
                      indexSummary.c_str(),
                      0,
                      numValues);
        }
        int maxValueIndex = (int)(numValues - 1);
        if (foundMinValue != 0 || foundMaxValue < maxValueIndex) {
            LOG_ISSUE(Lvl::Error,
                      "%s primvar '%s' has indices that only cover range [%d, %d], "
                      "which is not all values [0, %d]",
                      primvar.interpolation.GetText(),
                      primvarName.c_str(),
                      foundMinValue,
                      foundMaxValue,
                      maxValueIndex);
        }
    }

    if (checkForFiniteFloats) {
        std::vector<size_t> invalidIndices;
        size_t numInvalidIndices = checkFiniteFloats(primvar.values, invalidIndices);
        if (numInvalidIndices > 0) {
            std::string indexSummary = summarizeIndices(invalidIndices);
            LOG_ISSUE(Lvl::Error,
                      "%s primvar '%s' has %zu non-finite float values (inf, nan, etc.). Values %s "
                      "are non-finite",
                      primvar.interpolation.GetText(),
                      primvarName.c_str(),
                      numInvalidIndices,
                      indexSummary.c_str());
        }
    }

    return !foundError;
}

bool
verifyPoints(const std::string& path,
             const VtVec3fArray& points,
             bool checkForFiniteFloats,
             IssueVector* issues)
{
    using Lvl = Issue::Level;

    const int tempBufferSize = 1024;
    char tempBuffer[tempBufferSize];
    bool foundError = false;

    if (checkForFiniteFloats) {
        std::vector<size_t> invalidIndices;
        size_t numInvalidIndices = checkFiniteFloats(points, invalidIndices);
        if (numInvalidIndices > 0) {
            std::string indexSummary = summarizeIndices(invalidIndices);
            LOG_ISSUE(Lvl::Error,
                      "Points data has %zu non-finite float values (inf, nan, etc.). Points %s "
                      "are non-finite",
                      numInvalidIndices,
                      indexSummary.c_str());
            foundError = true;
        }
    }

    return !foundError;
}

bool
verifyMesh(const std::string& path,
           const Mesh& mesh,
           IssueVector* issues,
           const MeshVerificationOptions& options)
{
    size_t numPoints = mesh.points.size();
    size_t numFaces = mesh.faces.size();
    size_t numFaceVertices = mesh.indices.size();

    bool foundError = !verifyTopology(path, numPoints, mesh.faces, mesh.indices, issues, options);

    bool checkForFiniteFloats = options.checkForFiniteFloats;
    foundError |= !verifyPoints(path, mesh.points, checkForFiniteFloats, issues);
    foundError |= !verifyPrimvar(path,
                                 numPoints,
                                 numFaces,
                                 numFaceVertices,
                                 mesh.normals,
                                 "normals",
                                 checkForFiniteFloats,
                                 issues);
    foundError |= !verifyPrimvar(path,
                                 numPoints,
                                 numFaces,
                                 numFaceVertices,
                                 mesh.tangents,
                                 "tangents",
                                 checkForFiniteFloats,
                                 issues);
    foundError |= !verifyPrimvar(
      path, numPoints, numFaces, numFaceVertices, mesh.uvs, "uvs", checkForFiniteFloats, issues);
    int n = 1;
    for (const Primvar<GfVec2f>& uvs : mesh.extraUVSets) {
        foundError |= !verifyPrimvar(path,
                                     numPoints,
                                     numFaces,
                                     numFaceVertices,
                                     uvs,
                                     "uvs" + std::to_string(n),
                                     checkForFiniteFloats,
                                     issues);
        n++;
    }
    for (const Primvar<GfVec3f>& pv : mesh.colors) {
        foundError |= !verifyPrimvar(
          path, numPoints, numFaces, numFaceVertices, pv, "colors", checkForFiniteFloats, issues);
    }
    for (const Primvar<float>& pv : mesh.opacities) {
        foundError |= !verifyPrimvar(path,
                                     numPoints,
                                     numFaces,
                                     numFaceVertices,
                                     pv,
                                     "opacities",
                                     checkForFiniteFloats,
                                     issues);
    }

    // XXX TODO verify subsets

    return !foundError;
}

#undef LOG_ISSUE

bool
verifyMeshes(const UsdData& usdData, IssueVector* issues, const MeshVerificationOptions& options)
{
    bool foundError = false;
    for (const Node& node : usdData.nodes) {
        for (int meshIndex : node.staticMeshes) {
            const Mesh& mesh = usdData.meshes[meshIndex];
            std::string meshPath =
              node.path + "/" + (mesh.name.empty() ? "Mesh" : mesh.name.c_str());
            foundError |= !verifyMesh(meshPath, mesh, issues, options);
        }
        for (const auto& [skeletonIndex, meshIndices] : node.skinnedMeshes) {
            for (int meshIndex : meshIndices) {
                const Mesh& mesh = usdData.meshes[meshIndex];
                std::string meshPath =
                  node.path + "/" + (mesh.name.empty() ? "Mesh" : mesh.name.c_str());
                foundError |= !verifyMesh(meshPath, mesh, issues, options);
            }
        }
    }
    return !foundError;
}

void
printIssues(const IssueVector& issues)
{
    for (const Issue& issue : issues) {
        std::stringstream ss;
        ss << "  ";
        switch (issue.level) {
            case Issue::Level::Error:
                ss << "Error: ";
                break;
            case Issue::Level::Warning:
                ss << "Warning: ";
                break;
            case Issue::Level::Info:
                ss << "Info: ";
                break;
            case Issue::Level::Hint:
                ss << "Hint: ";
                break;
        }
        ss << issue.path << "\n    " << issue.message << "\n";

        TfDebug::Helper().Msg(ss.str());
    }
}

void
checkAndPrintMeshIssues(const UsdData& usdData)
{
    if (TfDebug::IsEnabled(FILE_FORMAT_UTIL)) {
        IssueVector issues;
        MeshVerificationOptions options;
        bool verified = verifyMeshes(usdData, &issues, options);
        if (!verified || !issues.empty()) {
            if (!verified) {
                TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%zu errors in parsed USD data!\n", issues.size());
            } else {
                TF_DEBUG_MSG(FILE_FORMAT_UTIL, "%zu issues in parsed USD data!\n", issues.size());
            }
            printIssues(issues);
        }
    }
}

void
createTriangulationIndices(Mesh& mesh)
{
    mesh.indices.resize(mesh.points.size());
    unsigned int size = mesh.indices.size();
    mesh.faces = PXR_NS::VtArray<int>(size / 3, 3);
    for (unsigned int i = 0; i < size; i++) {
        mesh.indices[i] = i;
    }
}

// Remove the indexing out of a set of values, but these values may have a number of components per
// element. For example, joint indices and joint weights, they are stored in plain arrays
// VtIntArray, VtFloatArray. But every n entries are associated with a single index entry / position
// entry in the mesh.
template<typename T>
void
expandIndexedValues(const VtIntArray& indices, VtArray<T>& values, int componentsPerElement)
{
    VtArray<T> temp = std::move(values);
    unsigned int size = indices.size();
    values.resize(size * componentsPerElement);
    for (unsigned int i = 0; i < size; i++) {
        for (int j = 0; j < componentsPerElement; j++) {
            values[i * componentsPerElement + j] = temp[indices[i] * componentsPerElement + j];
        }
    }
}

using ReverseIndex = std::vector<int>;

// Triangulate a mesh with various types of faces
// Assumes all faces are convex faces, since it does a simple fan triangulation
// Also, computes two reverse indices that map from the output mesh back to the original source
// mesh. These maps can be used to transfer primvars to the new topology
bool
fanTriangulate(const VtIntArray& srcFaceVertexCounts,
               const VtIntArray& srcFaceVertexIndices,
               ReverseIndex& reverseFaceIndex,
               ReverseIndex& reverseFaceIndexIndex,
               VtIntArray& dstFaceVertexCounts,
               VtIntArray& dstFaceVertexIndices)
{
    int oldMeshTriangleCount = 0;
    int oldMeshQuadCount = 0;
    int oldMeshNgonCount = 0;
    int totalTriangleCount = 0;
    for (size_t i = 0; i < srcFaceVertexCounts.size(); ++i) {
        int numFaceVertices = srcFaceVertexCounts[i];
        if (numFaceVertices < 3) {
            TF_WARN(
              FILE_FORMAT_UTIL,
              "fanTriangulate failed- Expected at least 3 face vertices, found: %d in array %zu",
              numFaceVertices,
              i);
            return false;
        }
        totalTriangleCount += numFaceVertices - 2;
        if (numFaceVertices == 3) {
            ++oldMeshTriangleCount;
        } else if (numFaceVertices == 4) {
            ++oldMeshQuadCount;
        } else {
            ++oldMeshNgonCount;
        }
    }

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Before triangulation mesh has %d triangles, %d quads, %d ngons\n",
                 oldMeshTriangleCount,
                 oldMeshQuadCount,
                 oldMeshNgonCount);

    // Allocate space for the new counts and indices
    reverseFaceIndex.resize(totalTriangleCount);
    reverseFaceIndexIndex.resize(totalTriangleCount * 3);
    dstFaceVertexCounts.resize(totalTriangleCount);
    dstFaceVertexIndices.resize(totalTriangleCount * 3);

    // Compute the new face vertex indices
    int srcFaceVertexOffset = 0;
    int dstFaceOffset = 0;
    int dstFaceVertexOffset = 0;
    size_t numFaces = srcFaceVertexCounts.size();
    for (size_t i = 0; i < numFaces; ++i) {
        int numFaceVertices = srcFaceVertexCounts[i];
        int numFaceTriangles = numFaceVertices - 2;
        // The center of the fan is the first vertex of the original face
        int centerVertexOffset = srcFaceVertexOffset;
        int centerVertex = srcFaceVertexIndices[centerVertexOffset];
        // The border vertex is the first additional vertex for the next triangle
        int borderVertexOffset = srcFaceVertexOffset + 1;
        int borderVertex = srcFaceVertexIndices[borderVertexOffset];

        for (int j = 0; j < numFaceTriangles; ++j) {
            dstFaceVertexCounts[dstFaceOffset] = 3;
            reverseFaceIndex[dstFaceOffset] = i;

            dstFaceVertexIndices[dstFaceVertexOffset + 0] = centerVertex;
            reverseFaceIndexIndex[dstFaceVertexOffset + 0] = centerVertexOffset;
            dstFaceVertexIndices[dstFaceVertexOffset + 1] = borderVertex;
            reverseFaceIndexIndex[dstFaceVertexOffset + 1] = borderVertexOffset;
            // Get the next vertex for this triangle, which is also the border vertex for the
            // following triangle
            borderVertexOffset = srcFaceVertexOffset + 2 + j;
            borderVertex = srcFaceVertexIndices[borderVertexOffset];
            dstFaceVertexIndices[dstFaceVertexOffset + 2] = borderVertex;
            reverseFaceIndexIndex[dstFaceVertexOffset + 2] = borderVertexOffset;

            dstFaceOffset += 1;
            dstFaceVertexOffset += 3;
        }
        srcFaceVertexOffset += numFaceVertices;
    }

    return true;
}

template<typename T>
void
mapPrimvarWithReverseIndex(const ReverseIndex& reverseIndices,
                           const PXR_NS::VtIntArray& origFaceVertexIndices,
                           const std::string& primvarName,
                           Primvar<T>& primvar)
{
    if (primvar.values.empty()) {
        return;
    }

    size_t numElements = reverseIndices.size();
    if (primvar.indices.empty()) {
        int numValues = static_cast<int>(primvar.values.size());
        VtArray<T> newValues(numElements);

        // if the original faceVertex indices is empty, we just use the reverse mapping of new index
        // to old index to locate the value.
        if (origFaceVertexIndices.empty()) {
            for (size_t i = 0; i < numElements; ++i) {
                int j = reverseIndices[i];
                if (j >= numValues) {
                    // If the mapping results in an array bounds error, we report an error and
                    // return
                    TF_WARN(FILE_FORMAT_UTIL,
                            "error trying to remap primvar '%s' with interpolation '%s', "
                            "reverseIndex[%lu] value is %d and is >= %d",
                            primvarName.c_str(),
                            primvar.interpolation.GetText(),
                            i,
                            j,
                            numValues);
                    return;
                }
                newValues[i] = primvar.values[j];
            }
        } else {
            int numOrigIndices = static_cast<int>(origFaceVertexIndices.size());
            for (size_t i = 0; i < numElements; ++i) {
                int j = reverseIndices[i];
                if (j >= numOrigIndices) {
                    // If the mapping results in an array bounds error, we report an error and
                    // return
                    TF_WARN(FILE_FORMAT_UTIL,
                            "error trying to remap primvar '%s' with interpolation '%s', "
                            "reverseIndex[%lu] value is %d and is >= %d",
                            primvarName.c_str(),
                            primvar.interpolation.GetText(),
                            i,
                            j,
                            numOrigIndices);
                    return;
                }
                int k = origFaceVertexIndices[j];
                if (k >= numValues) {
                    // If the mapping results in an array bounds error, we report an error and
                    // return
                    TF_WARN(FILE_FORMAT_UTIL,
                            "error trying to remap primvar '%s' with interpolation '%s', "
                            "origFaceVertexIndices[%d] value is %d and is >= %d",
                            primvarName.c_str(),
                            primvar.interpolation.GetText(),
                            j,
                            k,
                            numValues);
                    return;
                }
                newValues[i] = primvar.values[k];
            }
        }
        primvar.values = std::move(newValues);
    } else {
        // If we have indices we just remap the indices, the values referenced by the indices
        // stay the same
        VtIntArray newIndices(numElements);
        int numIndices = primvar.indices.size();
        for (size_t i = 0; i < numElements; ++i) {
            int idx = reverseIndices[i];
            if (idx >= numIndices) {
                TF_WARN("error trying to remap primvar '%s' with interpolation '%s', "
                        "remapping index at %zu references index %d >= %d primvar indices",
                        primvarName.c_str(),
                        primvar.interpolation.GetText(),
                        i,
                        idx,
                        numIndices);
                return;
            }
            newIndices[i] = primvar.indices[idx];
        }
        primvar.indices = std::move(newIndices);
    }
}

template<typename T>
void
mapPrimvarToTriangulatedMesh(const ReverseIndex& reverseFaceIndices,
                             const ReverseIndex& reverseFaceIndexIndices,
                             const PXR_NS::VtIntArray& origFaceVertexIndices,
                             const std::string& primvarName,
                             Primvar<T>& primvar)
{
    if (primvar.values.empty()) {
        return;
    }

    if (primvar.interpolation == UsdGeomTokens->constant) {
        // Constant is a single value per mesh and doesn't need any transformation
    } else if (primvar.interpolation == UsdGeomTokens->uniform) {
        // Uniform means a single value per face, so we need to transfer the data from the old faces
        // to the new faces (triangles)
        // Here, we pass an empty list of faceVertexIndices to indicate we only use one level
        // of mapping from new index to old index.
        VtIntArray emptyFaceVertexIndices;
        mapPrimvarWithReverseIndex(
          reverseFaceIndices, emptyFaceVertexIndices, primvarName, primvar);
    } else if (primvar.interpolation == UsdGeomTokens->vertex) {
        // Vertex means a single value per point of the mesh. In triangulation we're not changing
        // the number or order of the points, so these primvars stay the same.
    } else if (primvar.interpolation == UsdGeomTokens->varying) {
        // Varying means a single value per corner of every face. We need to
        // transfer the values or indices.
        // We pass the original faceVertexIndices as we will need to first do the reverse mapping
        // and then use the original indices to get the values.
        mapPrimvarWithReverseIndex(
          reverseFaceIndexIndices, origFaceVertexIndices, primvarName, primvar);
    } else if (primvar.interpolation == UsdGeomTokens->faceVarying) {
        // faceVarying means a value per every index. We need to transfer the values or indices.
        // Here, we pass an empty list of original faceVertexIndices to indicate we only use one
        // level of mapping from new index to old index.
        VtIntArray emptyFaceVertexIndices;
        mapPrimvarWithReverseIndex(
          reverseFaceIndexIndices, emptyFaceVertexIndices, primvarName, primvar);
    }
}

void
computeSmoothNormals(Mesh& mesh)
{
    size_t vertexCount = mesh.points.size();
    size_t numFaces = mesh.faces.size();
    size_t numFaceVertices = mesh.indices.size();

    // Construct a normals array the same size as the vertex array
    mesh.normals.values.resize(vertexCount);
    std::uninitialized_fill(mesh.normals.values.begin(), mesh.normals.values.end(), GfVec3f(0.0f));
    mesh.normals.interpolation = UsdGeomTokens->vertex;
    VtArray<GfVec3f>& vertexNormals = mesh.normals.values;

    // Generate normals for each vertex of each quad face
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Generating smooth normals for mesh %s (%zu point, %zu faces, %zu indices)\n",
                 mesh.name.c_str(),
                 vertexCount,
                 numFaces,
                 numFaceVertices);
    int faceVertexIndex = 0;
    int numBadNormals = 0;
    for (size_t faceIdx = 0; faceIdx < numFaces; ++faceIdx) {
        int numFaceVertices = mesh.faces[faceIdx];
        // Starting index for the vertices of this face
        int faceVertexIndexBase = faceVertexIndex;
        faceVertexIndex += numFaceVertices;

        // We use the vertex indices of the current face vertex and the one before and after in the
        // natural order of the faces. To only compute one index and point position per-iteration,
        // we precompute the prev and current values and then move them forward by one in each
        // iteration
        int prevIndex = mesh.indices[faceVertexIndexBase + (numFaceVertices - 1)];
        GfVec3f prevP = mesh.points[prevIndex];
        int currentIndex = mesh.indices[faceVertexIndexBase];
        GfVec3f currentP = mesh.points[currentIndex];

        for (int i = 0; i < numFaceVertices; ++i) {
            // Compute the next index and position
            int nextIndex = mesh.indices[faceVertexIndexBase + (i + 1) % numFaceVertices];
            GfVec3f nextP = mesh.points[nextIndex];

            // Get vectors between the points, outwards from the current point
            GfVec3f prevV = prevP - currentP;
            GfVec3f nextV = nextP - currentP;

            // Compute a weighted normal (ie non-normalized cross-product)
            GfVec3f normal = PXR_NS::GfCross(nextV, prevV);

            // Before we add the weighted normal we check that the accumulated vertex normal is not
            // reverted to zero. We check that we had a value before and then check that the new
            // accumulated result is not becoming zero again. Skipping this value is better than
            // producing a zero normal.
            // Note, this can only happen with geometrically strange meshes. A vertex should not
            // be surounded by faces with normals that add up to zero. But we've encountered bad
            // meshes from scanned data with these kinds of issues.
            GfVec3f& vertexNormal = vertexNormals[currentIndex];
            GfVec3f newVertexNormal = vertexNormal + normal;
            if (vertexNormal.GetLengthSq() != 0.0f &&
                newVertexNormal.GetLengthSq() < (GF_MIN_VECTOR_LENGTH * GF_MIN_VECTOR_LENGTH)) {
                // Skip adding the new weighted normal
                ++numBadNormals;
            } else {
                // Add to accumulated normal for the shared vertex
                vertexNormal = newVertexNormal;
            }

            // Move the indices and positions forward
            currentIndex = nextIndex;
            prevP = currentP;
            currentP = nextP;
        }
    }

    // normalize the accumulated vertex normals
    for (unsigned int i = 0; i < vertexCount; ++i) {
        vertexNormals[i].Normalize();
    }

    if (numBadNormals > 0) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "Warning: computation of %d normals had numerical issues.\n",
                     numBadNormals);
    }
}

bool
triangulateMesh(Mesh& mesh)
{
    // If the mesh has only triangle faces, we don't need to triangulate
    if (std::all_of(mesh.faces.cbegin(), mesh.faces.cend(), [](int faceVertexCount) {
            return faceVertexCount == 3;
        })) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "Mesh %s is already triangulated\n", mesh.name.c_str());
        return true;
    }

    size_t oldFaceCount = mesh.faces.size();

    // If we're triangulating the mesh and don't have normals yet, we generate smooth normals here
    // This is important, since we might also change the mesh topology in forceVertexInterpolation()
    // which would lead to faceting in the final mesh without explicit normals.
    if (mesh.normals.values.size() == 0) {
        computeSmoothNormals(mesh);
    }

    ReverseIndex reverseFaceIndex, reverseFaceIndexIndex;
    VtIntArray dstFaceVertexCounts;
    VtIntArray dstFaceVertexIndices;
    if (!fanTriangulate(mesh.faces,
                        mesh.indices,
                        reverseFaceIndex,
                        reverseFaceIndexIndex,
                        dstFaceVertexCounts,
                        dstFaceVertexIndices)) {
        return false;
    }

    // Update the original face vertex counts and indices but first keep the original mesh indices
    // as they are needed to remap the primvars
    VtIntArray origFaceVertexIndices = std::move(mesh.indices);
    mesh.faces = std::move(dstFaceVertexCounts);
    mesh.indices = std::move(dstFaceVertexIndices);

    mapPrimvarToTriangulatedMesh(
      reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "normals", mesh.normals);
    mapPrimvarToTriangulatedMesh(
      reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "tangents", mesh.tangents);
    mapPrimvarToTriangulatedMesh(
      reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "uvs", mesh.uvs);
    for (Primvar<GfVec2f>& pv : mesh.extraUVSets) {
        mapPrimvarToTriangulatedMesh(
          reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "uvs", pv);
    }
    for (Primvar<GfVec3f>& pv : mesh.colors) {
        mapPrimvarToTriangulatedMesh(
          reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "colors", pv);
    }
    for (Primvar<float>& pv : mesh.opacities) {
        mapPrimvarToTriangulatedMesh(
          reverseFaceIndex, reverseFaceIndexIndex, origFaceVertexIndices, "opacities", pv);
    }

    // Since we've changed the face count we need to update the subsets to match the new topology
    for (Subset& subset : mesh.subsets) {
        // Build a mask of all the faces from the old mesh that are in the subset
        std::vector<bool> facesMask(oldFaceCount, false);
        for (int srcFaceIndex : subset.faces) {
            facesMask[srcFaceIndex] = true;
        }

        // We don't know how many faces will be in the new subset, but we know we have at least as
        // many as in the original subset
        VtIntArray newFaceIndices;
        newFaceIndices.reserve(subset.faces.size());

        // Iterate over all faces in the new mesh
        size_t numFaces = mesh.faces.size();
        for (size_t faceIndex = 0; faceIndex < numFaces; ++faceIndex) {
            // Map the new face index to its original face and check if that face was part of the
            // subset. All (new) faces that map back to the original subset are part of the (new)
            // subset
            int srcFaceIndex = reverseFaceIndex[faceIndex];
            if (facesMask[srcFaceIndex]) {
                newFaceIndices.push_back(faceIndex);
            }
        }

        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "Map face subset: %zu faces -> %zu faces\n",
                     subset.faces.size(),
                     newFaceIndices.size());

        subset.faces = std::move(newFaceIndices);
    }

    return true;
}

// Note, this function also converts constant primvars to vertex interpolating ones
template<typename T>
bool
flattenPrimvarAndCheckForExpansionNeed(const std::string& primvarName,
                                       Primvar<T>& primvar,
                                       size_t numPoints)
{
    // If invalid skip
    if (primvar.values.empty()) {
        return false;
    }

    // Flatten the primvar values, removing the need for indices
    if (!primvar.indices.empty()) {
        std::string error;
        VtValue value;
        if (UsdGeomPrimvar::ComputeFlattened(
              &value, VtValue(primvar.values), primvar.indices, &error)) {
            value.Swap(primvar.values);
            primvar.indices.clear();
        } else {
            TF_WARN("Flattening of primvar '%s' failed: %s", primvarName.c_str(), error.c_str());
        }
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "Flatten primvar '%s' (%s): %zu values\n",
                     primvarName.c_str(),
                     primvar.interpolation.GetText(),
                     primvar.values.size());
    }

    // If we encounter a constant primvar we expand it to vertex interpolation
    if (primvar.interpolation == UsdGeomTokens->constant && primvar.values.size() >= 1) {
        primvar.values.assign(numPoints, primvar.values.front());
        primvar.interpolation = UsdGeomTokens->vertex;
        TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                     "Expand constant primvar %s (%s): %zu values\n",
                     primvarName.c_str(),
                     primvar.interpolation.GetText(),
                     primvar.values.size());
    }

    // Vertex interpolation is fine
    if (primvar.interpolation == UsdGeomTokens->vertex) {
        return false;
    }

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Mesh will need expansion because of primvar %s (%s): %zu values\n",
                 primvarName.c_str(),
                 primvar.interpolation.GetText(),
                 primvar.values.size());

    // Everything else will need an expansion
    return true;
}

template<typename T>
void
expandPrimvarToFaceVarying(const VtIntArray& faceVertexCounts,
                           const VtIntArray& faceVertexIndices,
                           const std::string& primvarName,
                           Primvar<T>& primvar)
{
    if (primvar.values.empty()) {
        return;
    }

    if (!primvar.indices.empty()) {
        TF_WARN("Expected flattened primvars only (%s)", primvarName.c_str());
        return;
    }

    size_t numFaces = faceVertexCounts.size();
    size_t numFaceVertices = faceVertexIndices.size();
    if (primvar.interpolation == UsdGeomTokens->constant) {
        TF_WARN("Unexpected 'constant' interpolation mode for primvar %s", primvarName.c_str());
    } else if (primvar.interpolation == UsdGeomTokens->uniform) {
        // Uniform means a single value per face, so we need to transfer the data from the old faces
        // to the new per-vertex values in the expanded form, which matches face vertex layout.
        VtArray<T> newValues(numFaceVertices);
        int idx = 0;
        for (size_t i = 0; i < numFaces; ++i) {
            int numFaceVertices = faceVertexCounts[i];
            T faceValue = primvar.values[i];
            for (int j = 0; j < numFaceVertices; ++j) {
                newValues[idx++] = faceValue;
            }
        }
        primvar.values = std::move(newValues);
        // Note, faceVarying and vertex interpolation are equivalent in this context
        primvar.interpolation = UsdGeomTokens->vertex;
    } else if (primvar.interpolation == UsdGeomTokens->vertex) {
        expandIndexedValues(faceVertexIndices, primvar.values);
    } else if (primvar.interpolation == UsdGeomTokens->varying ||
               primvar.interpolation == UsdGeomTokens->faceVarying) {
        // Varying and faceVarying means a single value per corner of every face. Which is what
        // we're expanding to, so this is already right.

        // Note, faceVarying and vertex interpolation are equivalent in this context
        primvar.interpolation = UsdGeomTokens->vertex;
    } else {
        TF_WARN("Unexpected interpolation mode '%s' for primvar %s",
                primvar.interpolation.GetText(),
                primvarName.c_str());
    }

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Expanded primvar '%s' (%s): %zu values\n",
                 primvarName.c_str(),
                 primvar.interpolation.GetText(),
                 primvar.values.size());
}

// Expand joint data.
// Joint indices/weights are stored flat in arrays, not as primvars.
// But they have vertex interpolation, and are currently clamped at 4 components per vertex entry!
template<typename T>
void
expandJointDataToFaceVarying(const VtIntArray& faceVertexCounts,
                             const VtIntArray& faceVertexIndices,
                             const std::string& name,
                             VtArray<T>& values,
                             int influenceCount)
{
    if (values.empty()) {
        return;
    }
    expandIndexedValues(faceVertexIndices, values, influenceCount);
    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Expanded '%s' (%s): %zu values\n",
                 name.c_str(),
                 UsdGeomTokens->vertex.GetText(),
                 values.size());
}

// Some formats like GLTF can't handle the complex mesh representation that is USD native,
// especially with regard to primvar interpolation.
// For these formats we expand the mesh into a version where all data can be vertex interpolated,
// which means a point and all its associated primvars (normals, tangent, uvs, colors, opacities)
// are aligned.
// Note, this current version is very crude. If a non-vertex interpolating primvar is detected, we
// expand the whole mesh into essentially a face varying mesh, which is the most complicated
// interpolation, since that is usually needed to represent UV coordinates. A more sophisticated
// algorithm could generate a smaller subset of unique vertices.
void
forceVertexInterpolation(Mesh& mesh)
{
    size_t numPoints = mesh.points.size();

    bool needToExpandMesh =
      flattenPrimvarAndCheckForExpansionNeed("normals", mesh.normals, numPoints) |
      flattenPrimvarAndCheckForExpansionNeed("tangents", mesh.tangents, numPoints) |
      flattenPrimvarAndCheckForExpansionNeed("uvs", mesh.uvs, numPoints);
    for (Primvar<GfVec3f>& pv : mesh.colors) {
        needToExpandMesh |= flattenPrimvarAndCheckForExpansionNeed("colors", pv, numPoints);
    }
    for (Primvar<float>& pv : mesh.opacities) {
        needToExpandMesh |= flattenPrimvarAndCheckForExpansionNeed("opacities", pv, numPoints);
    }

    if (!needToExpandMesh) {
        TF_DEBUG_MSG(FILE_FORMAT_UTIL, "No need to expand mesh %s\n", mesh.name.c_str());
        return;
    }

    // This expands the points to match the face vertex indices, it essentially makes vertex and
    // face varying interpolation identical. It duplicates all shared vertex positions, which is
    // HIGHLY inefficient.
    // Note that after this step, the mesh.indices are matching the old unexpanded points, until
    // they are fixed with the iota function below. This is needed to expand the primvars.
    expandIndexedValues(mesh.indices, mesh.points);

    TF_DEBUG_MSG(FILE_FORMAT_UTIL,
                 "Expand mesh %s to vertex interpolation: %zu points -> %zu points\n",
                 mesh.name.c_str(),
                 numPoints,
                 mesh.points.size());

    // Check and potentially expand all primvars to match the indices
    expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "normals", mesh.normals);
    expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "tangents", mesh.tangents);
    expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "uvs", mesh.uvs);
    for (Primvar<GfVec2f>& pv : mesh.extraUVSets) {
        expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "uvs", pv);
    }
    expandJointDataToFaceVarying(
      mesh.faces, mesh.indices, "joints", mesh.joints, mesh.influenceCount);
    expandJointDataToFaceVarying(
      mesh.faces, mesh.indices, "weights", mesh.weights, mesh.influenceCount);
    for (Primvar<GfVec3f>& pv : mesh.colors) {
        expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "colors", pv);
    }
    for (Primvar<float>& pv : mesh.opacities) {
        expandPrimvarToFaceVarying(mesh.faces, mesh.indices, "opacities", pv);
    }

    // Fill the indices with a simple increasing index to match the expanded points
    std::iota(mesh.indices.begin(), mesh.indices.end(), 0);

    // Note, since the face count and order hasn't been changed, this operation does not affect any
    // subsets of this mesh.
}

// Given the topology of a complete mesh and a subset of face indices into that mesh, compute the
// corresponding face vertex indices
void
computeFaceVertexIndicesForSubset(const VtIntArray& faceVertexCounts,
                                  const VtIntArray& faceVertexIndices,
                                  const VtIntArray& subsetFaceIndices,
                                  VtIntArray& subsetFaceVertexIndices)
{
    std::vector<bool> facesMask(faceVertexCounts.size(), false);
    int totalSubsetIndicesCount = 0;
    for (size_t i = 0; i < subsetFaceIndices.size(); i++) {
        int faceIndex = subsetFaceIndices[i];
        facesMask[faceIndex] = true;
        totalSubsetIndicesCount += faceVertexCounts[faceIndex];
    }
    subsetFaceVertexIndices.resize(totalSubsetIndicesCount);
    size_t q = 0, k = 0;
    for (size_t i = 0; i < faceVertexCounts.size(); i++) {
        int numFaceVertices = faceVertexCounts[i];
        if (facesMask[i]) {
            for (int j = 0; j < numFaceVertices; j++) {
                subsetFaceVertexIndices[k++] = faceVertexIndices[q + j];
            }
        }
        q += numFaceVertices;
    }
}

/// \fn Transform a mesh with a model matrix.
// Only points, normals and tangents are affected.
void
transformMesh(Mesh& mesh, const GfMatrix4d& modelMatrix)
{
    if (GfIsClose(modelMatrix, PXR_NS::GfMatrix4d(1.0), 1e-8))
        return;

    unsigned int pointsSize = mesh.points.size();
    for (unsigned int i = 0; i < pointsSize; i++) {
        mesh.points[i] = GfVec3f(modelMatrix.Transform(mesh.points[i]));
    }
    const GfMatrix3d rotMatrix = modelMatrix.ExtractRotationMatrix();
    const GfMatrix3d normalMatrix = rotMatrix.GetInverse().GetTranspose();

    unsigned int normalsSize = mesh.normals.values.size();
    for (unsigned int i = 0; i < normalsSize; i++) {
        mesh.normals.values[i] = GfVec3f(normalMatrix * mesh.normals.values[i]);
        mesh.normals.values[i].Normalize();
    }
    unsigned int tangentsSize = mesh.tangents.values.size();
    for (unsigned int i = 0; i < tangentsSize; i++) {
        // tangent.w does not require transformation
        GfVec4f& tangent = mesh.tangents.values[i];
        GfVec3f tangentXYZ = GfVec3f(rotMatrix * GfVec3f(tangent[0], tangent[1], tangent[2]));
        tangentXYZ.Normalize();
        tangent = GfVec4f(tangentXYZ[0], tangentXYZ[1], tangentXYZ[2], tangent[3]);
    }
}

}
