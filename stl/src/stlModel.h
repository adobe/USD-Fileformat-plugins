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
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace usdStl {
enum class StlFormat
{
    Ascii,
    Binary
};

struct StlNormal
{
    float x, y, z;

    StlNormal()
    {
        x = 0.0;
        y = 0.0;
        z = 0.0;
    }

    void normalize()
    {
        float length = sqrt((x * x) + (y * y) + (z * z));
        x = x / length;
        y = y / length;
        z = z / length;
    }
};

struct StlVec3f
{
    float x, y, z;

    StlVec3f()
    {
        x = 0.0;
        y = 0.0;
        z = 0.0;
    }
};

struct StlFacet
{
    StlNormal normal;
    StlVec3f vertices[3];
};

class StlModel
{
  private:
    std::vector<StlFacet> facets;

  public:
    void AddFacet(StlFacet facet);
    StlFacet GetFacet(int facetIndex) const;
    int FacetCount() const;

    bool Write(const std::string& filename, StlFormat format = StlFormat::Binary);
    void Read(const std::string& filename);
    bool Populated() const;
};

StlNormal
crossProduct(StlVec3f a, StlVec3f b);
}