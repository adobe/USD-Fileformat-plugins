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
#include "stlModel.h"
#include <filesystem>
#include <fstream>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/fileUtils.h>

using namespace PXR_NS;

namespace usdStl {

const int BINARY_HEADER_SIZE = 80;
const int ATTRIBUTE_COUNT_SIZE = 2;

void
StlModel::AddFacet(StlFacet facet)
{
    facets.push_back(facet);
}

StlFacet
StlModel::GetFacet(int facetIndex) const
{
    return facets[facetIndex];
}

int
StlModel::FacetCount() const
{
    return facets.size();
}

bool
StlModel::Populated() const
{
    return !facets.empty();
}

bool
StlModel::Write(const std::string& filename, StlFormat format)
{
    const std::string parentPath = TfGetPathName(filename);
    TfMakeDirs(parentPath, -1, true);

    const auto flags = format == StlFormat::Ascii ? std::ios_base::out
                                                  : (std::ios_base::out | std::ios_base::binary);
#if defined(_WIN32) // temporary shortcut, but consider replacing with ArchOpenFile
    std::fstream stlFile(ArchWindowsUtf8ToUtf16(filename), flags);
#else
    std::fstream stlFile(filename, flags);
#endif
    if (!stlFile.is_open()) {
        TF_WARN("Failed to open file \"%s\"", filename.c_str());
        return false;
    }

    if (format == StlFormat::Ascii) {
        stlFile << "solid\n";
        for (const StlFacet& facet : facets) {
            stlFile << std::scientific;
            stlFile << "facet normal " << facet.normal.x << " " << facet.normal.y << " "
                    << facet.normal.z << std::endl;
            stlFile << "outer loop" << std::endl;
            stlFile << "vertex " << facet.vertices[0].x << " " << facet.vertices[0].y << " "
                    << facet.vertices[0].z << std::endl;
            stlFile << "vertex " << facet.vertices[1].x << " " << facet.vertices[1].y << " "
                    << facet.vertices[1].z << std::endl;
            stlFile << "vertex " << facet.vertices[2].x << " " << facet.vertices[2].y << " "
                    << facet.vertices[2].z << std::endl;
            stlFile << "endloop" << std::endl;
            stlFile << "endfacet" << std::endl;
        }

        stlFile << "endsolid";
    } else {
        std::string header;
        header.resize(BINARY_HEADER_SIZE, '\0');
        stlFile.write(&header[0], BINARY_HEADER_SIZE);

        const int num_triangles = facets.size();
        stlFile.write(reinterpret_cast<const char*>(&num_triangles), sizeof(num_triangles));
        for (const StlFacet& facet : facets) {
            stlFile.write(reinterpret_cast<const char*>(&facet.normal), sizeof(float) * 3);
            stlFile.write(reinterpret_cast<const char*>(&facet.vertices[0]), sizeof(float) * 3);
            stlFile.write(reinterpret_cast<const char*>(&facet.vertices[1]), sizeof(float) * 3);
            stlFile.write(reinterpret_cast<const char*>(&facet.vertices[2]), sizeof(float) * 3);
            std::string attribute_count;
            attribute_count.resize(ATTRIBUTE_COUNT_SIZE, '\0');
            stlFile.write(&attribute_count[0], ATTRIBUTE_COUNT_SIZE);
        }
    }

    stlFile.close();

    return true;
}

StlNormal
crossProduct(StlVec3f a, StlVec3f b)
{
    StlNormal product;

    // Cross product formula
    product.x = (a.y * b.z) - (a.z * b.y);
    product.y = (a.z * b.x) - (a.x * b.z);
    product.z = (a.x * b.y) - (a.y * b.x);

    product.normalize();

    return product;
}

bool
isAsciiStl(std::ifstream& infile)
{
    // read the string at the start of the file
    infile.seekg(0, std::ios::beg);
    std::string startContents;
    if (infile >> startContents) {
        // ASCII STL files are expected to start with 'solid'. If it doesn't, we know it must be a
        // binary file.
        if (startContents != "solid") {
            return false;
        }
    }

    // Even though the file starts with 'solid', it might still be a binary file. We read the
    // triangle count and check if the file size matches the size expected for the triangle count.

    // skip 80 byte header
    infile.seekg(80, std::ios::beg);

    // read 4 byte triangle count
    int facetCount = 0;
    infile.read(reinterpret_cast<char*>(&facetCount), sizeof(int));

    // seek to end of file to determine file size
    infile.seekg(0, std::ios::end); // go to end of file
    auto size = infile.tellg();

    int expectedSize = 84 + 50 * facetCount;

    // reset position back to start
    infile.seekg(0, std::ios::beg);

    // is the file the expected size? if not, then it is assumed to be an ascii stl file
    return expectedSize != size;
}

void
StlModel::Read(const std::string& filename)
{
#if defined(_WIN32) // temporary shortcut, but consider replacing with ArchOpenFile
    std::ifstream stlFile(ArchWindowsUtf8ToUtf16(filename), std::ios_base::in | std::ios::binary);
#else
    std::ifstream stlFile(filename, std::ios_base::in | std::ios::binary);
#endif

    if (isAsciiStl(stlFile)) {
        // skip over 'solid' header
        stlFile.seekg(5, std::ios::beg);

        // read to first newline
        char ch;
        while (stlFile.read(reinterpret_cast<char*>(&ch), 1)) {
            if (ch == '\n')
                break;
        }

        std::string contents;
        while (stlFile >> contents && contents == "facet") {
            StlNormal normal;
            StlVec3f vertex0, vertex1, vertex2;

            stlFile >> contents;
            stlFile >> normal.x >> normal.y >> normal.z;
            stlFile >> contents >> contents;
            stlFile >> contents >> vertex0.x >> vertex0.y >> vertex0.z;
            stlFile >> contents >> vertex1.x >> vertex1.y >> vertex1.z;
            stlFile >> contents >> vertex2.x >> vertex2.y >> vertex2.z;
            stlFile >> contents >> contents;

            StlFacet facet;
            facet.normal = normal;
            facet.vertices[0] = vertex0;
            facet.vertices[1] = vertex1;
            facet.vertices[2] = vertex2;
            facets.push_back(facet);
        }
    } else {
        // skip header
        stlFile.seekg(BINARY_HEADER_SIZE, std::ios::beg);

        int facetCount = 0;
        stlFile.read(reinterpret_cast<char*>(&facetCount), sizeof(int));

        // buffer to hold attributes value
        char attributes[ATTRIBUTE_COUNT_SIZE];

        for (int i = 0; i < facetCount; i++) {
            StlFacet facet;
            stlFile.read(reinterpret_cast<char*>(&facet.normal), sizeof(float) * 3);
            stlFile.read(reinterpret_cast<char*>(&facet.vertices[0]), sizeof(float) * 3);
            stlFile.read(reinterpret_cast<char*>(&facet.vertices[1]), sizeof(float) * 3);
            stlFile.read(reinterpret_cast<char*>(&facet.vertices[2]), sizeof(float) * 3);

            // skip over attributes bytes
            stlFile.read(reinterpret_cast<char*>(&attributes), ATTRIBUTE_COUNT_SIZE);

            facets.push_back(facet);
        }
    }
}
}