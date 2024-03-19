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
#pragma once

#include <map>
#include <set>
#include <string>

namespace adobe::usd::sbsar {
// Represents a mapped symbol, holds both the original
// substance symbol and the corresponding Usd symbol
struct MappedSymbol
{
    std::string substanceName;
    std::string usdName;
    bool invalid();
};

// Keeps track of mapping of names between substance and USD in a reversible
// way
// Guarantees the same Usd Symbol doesn't occur multiple times in the same
// mapper
class SymbolMapper
{
  public:
    SymbolMapper();
    virtual ~SymbolMapper();
    // Ask for a Usd symbol for the argument substance symbol
    // if the symbol is known, give back the existing mapping
    // if it's unknown, generate a new mapping with a Usd
    // compatible name that is not a duplicate of a previously
    // known usd name in the mapper
    MappedSymbol GetSymbol(const std::string& substanceSymbol);

  private:
    // Existing mappings
    std::map<std::string, MappedSymbol> mapped_symbols;

    // Existing usd symbols
    std::set<std::string> usd_symbols;
};
}
