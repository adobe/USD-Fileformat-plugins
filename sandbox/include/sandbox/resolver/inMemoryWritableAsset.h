/*
Copyright 2025 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#pragma once

#include <pxr/usd/ar/writableAsset.h>

#include <memory>
#include <vector>

namespace adobe::usd::sandbox {

class InMemoryWritableAsset : public PXR_NS::ArWritableAsset
{
public:
    InMemoryWritableAsset(std::vector<char>& data);
    virtual ~InMemoryWritableAsset();

    // Close doesn't do anything for the in memory buffer
    bool Close() override;

    // Override Write method for writing data
    size_t Write(const void* buffer, size_t count, size_t offset) override;

    // Retrieve the in-memory buffer
    const std::vector<char>& GetBuffer() const;

private:
    // This is a reference so that it modifies the resolver's storage
    // TODO: Replace with shared ptr
    std::vector<char>& _buffer;
};
}
