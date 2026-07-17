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

#include <sandbox/resolver/inMemoryWritableAsset.h>

#include <pxr/base/tf/diagnostic.h>

#include <iostream>

using namespace PXR_NS;

namespace adobe::usd::sandbox {

InMemoryWritableAsset::InMemoryWritableAsset(std::vector<char>& data)
  : _buffer(data)
{}

InMemoryWritableAsset::~InMemoryWritableAsset() {}

bool
InMemoryWritableAsset::Close()
{
    return false;
    // TODO: investigate if changing this to true removes the layer->export failure. It may have
    // caused other issues. Do this alongside checking that layer->export returns true.
    // return true;
}

size_t
InMemoryWritableAsset::Write(const void* buffer, size_t count, size_t offset)
{
    // We don't know the size of the data beforehand, so as we write, we may need to resize. We
    // won't always need to, because chunks may come out of order, so we don't want to downsize
    // the buffer and lose chunks later on that have already been written.
    if (offset + count > _buffer.size()) {
        _buffer.resize(offset + count);
    }
    std::memcpy(_buffer.data() + offset, buffer, count);
    return count;
}

const std::vector<char>&
InMemoryWritableAsset::GetBuffer() const
{
    return _buffer;
}
}
