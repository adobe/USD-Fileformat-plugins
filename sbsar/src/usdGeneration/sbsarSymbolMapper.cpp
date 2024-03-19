/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include <pxr/base/tf/diagnostic.h>
#include <pxr/pxr.h>
#include <usdGeneration/sbsarSymbolMapper.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {
namespace {
bool
forbiddenSymbol(char c)
{
    return !(isalnum(c) || c == '_');
}
std::string
cleanSubstanceName(std::string name)
{
    for (auto i = name.begin(); i != name.end(); ++i) {
        if (forbiddenSymbol(*i)) {
            *i = '_';
        }
    }
    if (isdigit(name[0]) != 0) {
        return "_" + name;
    }
    return name;
}
std::string
nudgeUsdName(const std::string& usdName)
{
    return usdName + "_";
}
} // namespace

bool
MappedSymbol::invalid()
{
    return substanceName.empty();
};
SymbolMapper::SymbolMapper() = default;
SymbolMapper::~SymbolMapper() = default;

MappedSymbol
SymbolMapper::GetSymbol(const std::string& substanceSymbol)
{
    auto it = mapped_symbols.find(substanceSymbol);
    if (it != mapped_symbols.end()) {
        return it->second;
    }
    std::string usdName = cleanSubstanceName(substanceSymbol);
    while (usd_symbols.find(usdName) != usd_symbols.end()) {
        usdName = nudgeUsdName(usdName);
    }
    usd_symbols.insert(usdName);
    MappedSymbol newSymbol = { substanceSymbol, usdName };
    mapped_symbols[substanceSymbol] = newSymbol;
    return newSymbol;
}
}
