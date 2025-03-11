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

/// \file obj.cpp
///
/// Obj read
/// Implements a multithreaded obj read, outlined as follows:
/// 1) Read file contents: First reads all the .obj file contents into a string buffer.
/// 2) Split work: Then splits the buffer into more or less equal chunks, each to be parsed by 1
///    thread.
/// 3) Parse into intermediates: As each thread parses its buffer, it fills a `ObjIntermediate`
///    struct. This struct acts as a registry of the different elements encountered as the buffer
///    is read.
/// 4) Join intermediates: When all threads are done, the various `ObjIntermediate` are joined,
///    i.e. they are stacked together into a global `ObjIntermediate`.
/// 5) Reindex intermediate: Finally the resulting global `ObjIntermediate` is traversed, and its
///    contents are translated to a `Obj` struct (proper obj objects and their associations are
///    spawned).
/// There is no multhreading for material reading.
/// Read more starting with the functions `readObj` (from file) and `readObj` (from string).
///
/// Obj write:
/// Implements single-threaded, buffered, obj write.
/// Read more starting with the functions `writeObj` (to file) and `writeObj` (to string).
///

#include "obj.h"
#include "debugCodes.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fast_float/fast_float.h>
#include <fileformatutils/common.h>
#include <fmt/compile.h>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <ostream>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/tf/enum.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/registryManager.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/work/loops.h>
#include <pxr/base/work/threadLimits.h>
#include <pxr/pxr.h>
#include <string>

using namespace PXR_NS;

namespace adobe::usd {

///////////////////////////////////////////////////////////////////////////////////////////////////
/// OBJ READ //////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

static const int ZERO_INDEX = std::numeric_limits<int>::max();

// Helper enum for obj multithreaded parsing, encoding the type of element in the obj data.
enum EntryType
{
    EntryTypeNull,
    EntryTypeV,
    EntryTypeVc,
    EntryTypeVt,
    EntryTypeVn,
    EntryTypeVs,
    EntryTypeP,
    EntryTypeF,
    EntryTypeG,
    EntryTypeO,
    EntryTypeUsemtl,
    EntryTypeMtllib,
    EntryTypeMdllib,
    EntryTypeComment
};

/// Helper struct for obj multithreaded parsing, encoding the type of element in the obj data
/// encountered while parsing a chunk of the obj string buffer, together with offsets of vertex
/// elements stacked so far.
struct Entry
{
    EntryType type;
    size_t count;
    size_t vOffset;
    size_t vtOffset;
    size_t vnOffset;
};

/// Helper struct for obj multithreaded parsing.
/// When parsing a chunk of the obj string buffer, we only care about stacking all the different
/// elements encountered, and keeping an ordered registry of those elements as they are
/// encountered. That registry is encoded in `entries`.
struct ObjIntermediate
{
    int index = 0;
    const char* data = nullptr;
    size_t dataSize = 0;
    const char* begin = nullptr;
    const char* end = nullptr;
    bool error = false;
    std::string errorMsg;
    VtVec3fArray vertices;
    VtVec3fArray colors;
    VtVec2fArray uvs;
    VtVec3fArray normals;
    VtVec3fArray sVertices;
    VtVec3iArray points; // If an index is 0, then it's non-existent
    VtVec2iArray faces;
    std::vector<std::string> objects;
    std::vector<std::string> groups;
    std::vector<std::string> usemtls;
    std::vector<std::string> mtllibs;
    std::vector<std::string> mdllibs;
    std::vector<std::string> comments;
    std::vector<Entry> entries;
    int lineNum;
};

void
warnFromIntermediateAndCalculateLine(const ObjIntermediate& inter, const char* p)
{
    // If the data is empty, can't calculate the line
    if (inter.dataSize == 0) {
        TF_WARN("Error parsing OBJ: error calculating line number of empty data");
        return;
    }

    const char* dataEnd = inter.data + (sizeof(char) * (inter.dataSize));

    // Ensure p points to within the data block
    if (p >= dataEnd || p < inter.data) {
        TF_WARN("Error parsing OBJ: error calculating line number of invalid character");
        return;
    }

    size_t lineNum = 1;
    bool pFound = false;

    const char* lineBegin = inter.data;
    const char* it = inter.data;
    while (it < dataEnd) {
        // Convert the iterator to a pointer to compare with p, but get benefits of using iters
        if (it >= p) {
            // Found the line p is on, but keep parsing until we have the complete line for the
            // error message.
            pFound = true;
        }

        // Handle line breaks. Reads "\r" as a new line, but "\r\n" as only one new line
        //
        // \r may be a line break in legacy systems, or erroneous files where a \n is corrupted
        // but there is a \r will still count as a line break, so the line number in the error
        // message is consistent with the behavior of many other text editors
        if (*it == '\n' || *it == '\r') {
            if (pFound) {
                // Found the error char and got to the end of the line, so we can break
                break;
            } else {
                lineNum++;

                const char prevChar = *it;

                // The current line begins at the char after \n or \r
                lineBegin = ++it;

                // Don't count \r\n as two new lines, skip an extra character forward
                if (it < dataEnd && prevChar == '\r' && *it == '\n') {
                    lineBegin = ++it;
                }
            }
        } else {
            // Read the next char
            ++it;
        }
    }

    size_t lineSize = it - lineBegin;
    std::string line(lineBegin, lineSize);

    TF_WARN("Error parsing OBJ: Failed parsing line %zu:\n%s", lineNum, line.c_str());
}

/// Read an entire file to a buffer.
bool
readFileContents(const std::string& filename, std::vector<char>& buffer)
{
    FILE* file = ArchOpenFile(filename.c_str(), "rb");
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    if (length < 0) {
        TF_WARN("Unable to read file %s");
        return false;
    } else {
        fseek(file, 0, SEEK_SET);
        buffer.resize(length + 1);
        fread(buffer.data(), length, 1, file);
        buffer[length] = '\0';
        fclose(file);
        return true;
    }
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextLine(const char*& p, const char* end)
{
    while (p < end && *p != '\n')
        p++;
    p++;
}

/// Helper parsing function. `p` is the moving pointer into the data.
// Returns the length of the line.
int
countLineLen(const char* p, const char* end)
{
    int size = 0;
    while (p < end && *p != '\n') {
        ++p;
        ++size;
    }
    return size;
}

/// Helper parsing function. `p` is the moving pointer into the data.
// Returns true if it reached the end of file or line.
bool
skipWhitespace(const char*& p, const char* end)
{
    for (; p < end && *p == ' '; p++)
        ;
    return p >= end || *p == '\n' || *p == '\r' || *p == '\0';
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
nextFloat(const char*& p, const char* end, float& x)
{
    const char* q;
    if (p >= end || *p == '\n')
        return false;
    for (; p < end && *p == ' '; p++)
        ;
    for (q = p; q < end && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    fast_float::from_chars_result result = fast_float::from_chars(p, q, x);
    if (result.ec != std::errc())
        return false;
    p = q;
    return true;
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
nextFloat2(const char*& p, const char* end, GfVec2f& x)
{
    return nextFloat(p, end, x[0]) && nextFloat(p, end, x[1]);
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
nextFloat3(const char*& p, const char* end, GfVec3f& x)
{
    return nextFloat(p, end, x[0]) && nextFloat(p, end, x[1]) && nextFloat(p, end, x[2]);
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
nextInteger(const char*& p, const char* end, int& x)
{
    float f;
    if (!nextFloat(p, end, f))
        return false; // Change to actual integer parsing?
    x = f;
    return true;
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextText(const char*& p, const char* end, std::string& text)
{
    const char* q;
    for (; p < end && *p == ' '; p++)
        ;
    for (q = p; q < end && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    text.assign(p, q - p);
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextConcatenatedText(const char*& p, const char* end, std::string& text)
{
    const char* q;
    for (; p < end && *p == ' '; p++)
        ;
    while (p < end && *p != '\n' && *p != '\r' && *p != '\0') {
        for (q = p; q < end && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
            ;
        if (!text.empty()) {
            text.append("_");
        }
        text.append(p, q - p);
        p = q;
        for (; p < end && *p == ' '; p++)
            ;
    }
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextFilename(const char*& p, const char* end, std::string& text)
{
    const char* q;
    for (; p < end && (*p == ' ' || *p == '\t'); p++)
        ;
    for (q = p; q < end && *q != '.' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    for (; q < end && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    text.assign(p, q - p);
    p = q;
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextSpacedText(const char*& p, const char* end, std::string& text)
{
    const char* q;
    for (; p < end && *p == ' '; p++)
        ;
    for (q = p; q < end && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    text.assign(p, q - p);
    p = q;
}

/// Helper parsing function. `p` is the moving pointer into the data.
/// Returns true if 'on' or 'off' was found
bool
nextOnOrOff(const char*& p, const char* end, bool& isOn)
{
    std::string s;
    nextText(p, end, s);
    std::for_each(s.begin(), s.end(), [](char& c) { c = std::tolower(c); });
    if (s == "on") {
        isOn = true;
        return true;
    } else if (s == "off") {
        isOn = false;
        return true;
    }
    return false;
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
nextChannel(const char*& p, const char* end, ObjMapChannel& channel)
{
    std::string s;
    nextText(p, end, s);
    std::for_each(s.begin(), s.end(), [](char& c) { c = std::tolower(c); });
    if (s.size() == 1) {
        char c = s[0];
        switch (c) {
            case 'r':
                channel = ObjMapChannelR;
                return true;
            case 'g':
                channel = ObjMapChannelG;
                return true;
            case 'b':
                channel = ObjMapChannelB;
                return true;
            case 'm':
                channel = ObjMapChannelM;
                return true;
            case 'l':
                channel = ObjMapChannelL;
                return true;
            case 'z':
                channel = ObjMapChannelZ;
                return true;
            default:
                break;
        }
    }

    return false;
}

/// Helper parsing function. `p` is the moving pointer into the data.
bool
checkWord(const char*& p, const char* end, const std::string& word)
{
    const char* q = p;
    if (q + word.size() >= end)
        return false;
    for (size_t i = 0; i < word.size(); i++, q++) {
        if (std::tolower(*q) != word.c_str()[i]) {
            return false;
        }
    }
    p = q;
    return true;
}

/// Helper parsing function. `p` is the moving pointer into the data.
void
nextIndex(const char*& p, const char* end, bool& endOfLine, int& x)
{
    const char* q;
    if (p < end && *p == '/')
        p++;
    for (q = p; q < end && *q != ' ' && *q != '/' && *q != '\n' && *q != '\r' && *q != '\0'; q++)
        ;
    endOfLine = q >= end || *q == '\n' || (q + 1 < end && *(q + 1) == '\r');
    if (p == q)
        return; // this is the case for an empty index

    // strtol returns 0 on error. Coincidentally, we never expect an integer with value 0.
    char* qq;
    x = std::strtol(p, &qq, 10);
    if (x == 0)
        return;
    p = qq;
};

/// Helper parsing function. Add an entry to the intermediate's entries.
void
addEntry(ObjIntermediate& inter,
         EntryType type,
         size_t vCount = 0,
         size_t vtCount = 0,
         size_t vnCount = 0)
{
    Entry& e = inter.entries.back();
    if (e.type == type && e.type != EntryTypeG) {
        e.count++;
    } else {
        inter.entries.push_back({ type, 1, vCount, vtCount, vnCount });
    }
}

/// Splits the obj string buffer into `threadCount` chunks
/// and stores chunk's begin and end references into each intermediate.
/// Note the splitting takes care of not breaking lines, hence it's not a perfect split, but a
/// convenient one for later parsing.
void
splitObjIntermediates(const std::vector<char>& data,
                      int threadCount,
                      std::vector<ObjIntermediate>& intermediates)
{
    intermediates.resize(threadCount);
    size_t segmentSize = data.size() / threadCount;
    size_t filePointer = 0;
    for (int i = 0; i < threadCount; i++) {
        size_t begin = filePointer;

        // filepointer is shifted when looking for the end of the line
        // meaning begin + segmentSize can actually get larger than
        // size here, so we need to clamp it to size
        size_t end = std::min(begin + segmentSize, data.size());
        while (end < data.size() && data[end] != '\n')
            end++;
        if (end < data.size() && data[end] == '\n') {
            end++;
        }
        filePointer = end;
        // filePointer++;
        intermediates[i].index = i;
        intermediates[i].data = data.data();
        intermediates[i].dataSize = data.size();
        intermediates[i].begin = data.data() + begin;
        intermediates[i].end = data.data() + end;
    }
}

/// Read and parse a chunk of the obj string buffer and fill in a `ObjIntermediate` instance.
/// When parsing a chunk of the obj string buffer, we only care about stacking all the different
/// elements encountered in `ObjIntermediate`, and keeping an ordered registry of those elements as
/// they are encountered in `ObjIntermediate::entries`.
void
readObjIntermediate(ObjIntermediate& inter)
{
    // TF_DEBUG_MSG(FILE_FORMAT_OBJ, "read instance[%03d]: [%lld, %lld] %p <%p %p>\n", inter.index,
    // inter.begin - inter.data, inter.end - inter.data, inter.data, inter.begin, inter.end);
    inter.entries.push_back({ EntryTypeNull, 0 });
    bool endOfLine;
    int lineCount = 0;
    int vCount = 0;
    int vcCount = 0;
    int vtCount = 0;
    int vnCount = 0;
    const char* end = inter.end; // End of the obj string buffer.
    const char* p = inter.begin; // Moving pointer into the obj string buffer.

    while (p < end - 2) { // -2 ensures at least 2 characters per line
        float f0, f1, f2, f3, f4, f5;
        char c0 = *p;
        char c1 = *(p + 1);
        if (c0 == 'v' && c1 == ' ') {
            p += 2;
            bool s0 = nextFloat(p, end, f0);
            bool s1 = nextFloat(p, end, f1);
            bool s2 = nextFloat(p, end, f2);
            bool s3 = nextFloat(p, end, f3);
            bool s4 = nextFloat(p, end, f4);
            bool s5 = nextFloat(p, end, f5);
            if (s0 && s1 && s2 && s3 && s4 && s5) {
                vCount++;
                vcCount++;
                inter.vertices.push_back(GfVec3f(f0, f1, f2));
                inter.colors.push_back(GfVec3f(f3, f4, f5));
            } else if (s0 && s1 && s2) {
                vCount++;
                inter.vertices.push_back(GfVec3f(f0, f1, f2));
            } else {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
        } else if (c0 == 'v' && c1 == 't') {
            p += 3;
            if (nextFloat(p, end, f0) && nextFloat(p, end, f1)) {
                vtCount++;
                inter.uvs.push_back(GfVec2f(f0, f1));
            } else {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
        } else if (c0 == 'v' && c1 == 'n') {
            p += 3;
            if (nextFloat(p, end, f0) && nextFloat(p, end, f1) && nextFloat(p, end, f2)) {
                vnCount++;
                inter.normals.push_back(GfVec3f(f0, f1, f2));
            } else {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
        } else if (c0 == 'f' && c1 == ' ') {
            p += 2;
            GfVec2i f;
            f[0] = inter.points.size();
            endOfLine = false;
            while (!endOfLine) {
                int vIndex = 0, vtIndex = 0, vnIndex = 0;
                // No spaces allowed between indices of a point, only between points
                if (skipWhitespace(p, end))
                    break;
                nextIndex(p, end, endOfLine, vIndex);
                nextIndex(p, end, endOfLine, vtIndex);
                nextIndex(p, end, endOfLine, vnIndex);
                // vIndex needs to be valid, vtIndex and vnIndex are optional
                if (vIndex) {
                    inter.points.push_back(GfVec3i(vIndex, vtIndex, vnIndex));
                } else { // can't have all of them fail or being zero
                    inter.error = true;
                    warnFromIntermediateAndCalculateLine(inter, p);
                    return;
                }

                // printf("Point %zu: %d %d %d\n", inter.points.size(), inter.points.back()[0],
                // inter.points.back()[1], inter.points.back()[2]);
            }
            f[1] = inter.points.size();
            inter.faces.push_back(f);
            addEntry(inter, EntryTypeF, vCount, vtCount, vnCount);
        } else if (c0 == 'u' && c1 == 's') {
            if (!checkWord(p, end, "usemtl")) {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
            inter.usemtls.push_back(std::string());
            nextSpacedText(p, end, inter.usemtls.back());
            addEntry(inter, EntryTypeUsemtl);
        } else if (c0 == 'm' && c1 == 't') {
            if (!checkWord(p, end, "mtllib")) {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
            std::string temp;
            nextFilename(p, end, temp);
            inter.mtllibs.push_back(temp);
            addEntry(inter, EntryTypeMtllib);
        } else if (c0 == 'a' && c1 == 'd') {
            if (!checkWord(p, end, "adobe_mdllib")) {
                inter.error = true;
                warnFromIntermediateAndCalculateLine(inter, p);
                return;
            }
            std::string temp;
            nextFilename(p, end, temp);
            inter.mdllibs.push_back(temp);
            addEntry(inter, EntryTypeMdllib);
        } else if (c0 == 's' && c1 == ' ') {
        } else if (c0 == 'g' && c1 == ' ') {
            p += 2;
            inter.groups.push_back(std::string());
            nextConcatenatedText(p, end, inter.groups.back());
            addEntry(inter, EntryTypeG);
        } else if (c0 == 'o' && c1 == ' ') {
            p += 2;
            inter.objects.push_back(std::string());
            nextText(p, end, inter.objects.back());
            addEntry(inter, EntryTypeO);
        } else if (c0 == '#' && c1 == 'M') {
            // ZBrush vertex colors block
            size_t lineLen = countLineLen(p, end);
            if (checkWord(p, end, "#MRGB ") && (lineLen - 7) % 8 == 0) {

                // after the 6 char long header, the rest of the row should
                // be made of up to 64 hex colors values packed as
                // MMRRGGBBMMRRGGBBMMRRGGBB...
                size_t colorlen = (lineLen - 7) / 8;
                inter.colors.reserve(colorlen);
                for (size_t i = 0; i < colorlen; ++i) {
                    char rs[3], gs[3], bs[3];
                    p++; // skip MM
                    p++;
                    rs[0] = (*p++);
                    rs[1] = (*p++);
                    rs[2] = 0;
                    gs[0] = (*p++);
                    gs[1] = (*p++);
                    gs[2] = 0;
                    bs[0] = (*p++);
                    bs[1] = (*p++);
                    bs[2] = 0;
                    GfVec3f color;
                    color[0] = (float)strtol(rs, (char**)nullptr, 16) / 255.f;
                    color[1] = (float)strtol(gs, (char**)nullptr, 16) / 255.f;
                    color[2] = (float)strtol(bs, (char**)nullptr, 16) / 255.f;
                    inter.colors.emplace_back(color);
                }
            }
        } else if (c0 == '#' && c1 == ' ') {
            // Don't care about comments
            //    rep.comments.push_back(std::string());
            //    nextText(rep.comments.back());
        } else {
        }
        lineCount++;
        nextLine(p, end);
    }
}

/// Stacks together the various ObjIntermediates into a global ObjIntermdiate `sum`.
/// First sizes the global `sum` then taverses the intermediates and copies the data over.
/// Note this is just a clump of obj elements at this point, with no links or grouping between
/// them yet.
void
joinObjIntermediates(Obj& obj,
                     ObjIntermediate& sum,
                     std::vector<ObjIntermediate> intermediates,
                     std::unordered_map<std::string, int>& materialMap)
{
    int vertices = 0;
    int colors = 0;
    int uvs = 0;
    int normals = 0;
    int sVertices = 0;
    int points = 0;
    int faces = 0;
    int entries = 0;
    for (const ObjIntermediate& inter : intermediates) {
        vertices += inter.vertices.size();
        colors += inter.colors.size();
        uvs += inter.uvs.size();
        normals += inter.normals.size();
        sVertices += inter.sVertices.size();
        points += inter.points.size();
        faces += inter.faces.size();
        entries += inter.entries.size() - 1; // -1 accounts for the EntryTypeNull at the start
    }
    sum.vertices.resize(vertices);
    sum.colors.resize(colors);
    sum.uvs.resize(uvs);
    sum.normals.resize(normals);
    sum.sVertices.resize(sVertices);
    sum.points.resize(points);
    sum.faces.resize(faces);
    sum.entries.resize(entries);
    vertices = 0;
    colors = 0;
    uvs = 0;
    normals = 0;
    sVertices = 0;
    points = 0;
    faces = 0;
    entries = 0;
    for (const ObjIntermediate& inter : intermediates) {
        for (const std::string& mtllib : inter.mtllibs) {
            obj.libraries.push_back(ObjMaterialLibrary());
            obj.libraries.back().filename = mtllib;
            obj.libraries.back().isMdl = false;
        }
        for (const std::string& mdllib : inter.mdllibs) {
            obj.libraries.push_back(ObjMaterialLibrary());
            obj.libraries.back().filename = mdllib;
            obj.libraries.back().isMdl = true;
        }
        for (const std::string& usemtl : inter.usemtls) {
            int newIndex = obj.materials.size();
            auto entry = std::pair<std::string, int>(usemtl, newIndex);
            auto it = materialMap.insert(entry);
            if (it.second) {
                obj.materials.push_back(ObjMaterial(usemtl));
            } else {
                // TF_DEBUG(FILE_FORMAT_OBJ, "Already contained\n");
            }
        }
        memcpy(&sum.points[points], inter.points.data(), inter.points.size() * sizeof(GfVec3i));
        memcpy(
          &sum.vertices[vertices], inter.vertices.data(), inter.vertices.size() * sizeof(GfVec3f));
        memcpy(&sum.colors[colors], inter.colors.data(), inter.colors.size() * sizeof(GfVec3f));
        memcpy(&sum.uvs[uvs], inter.uvs.data(), inter.uvs.size() * sizeof(GfVec2f));
        memcpy(&sum.normals[normals], inter.normals.data(), inter.normals.size() * sizeof(GfVec3f));
        points += inter.points.size();
        vertices += inter.vertices.size();
        colors += inter.colors.size();
        uvs += inter.uvs.size();
        normals += inter.normals.size();
    }
    if (sum.colors.size() != sum.vertices.size()) {
        TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Color and vertex count differ, dropping colors\n");
        sum.colors.clear();
    }
    // Debug prints, dont erase yet
    // for (int i = 0; i < sum.points.size(); i++) {
    //     const auto& p = sum.points[i];
    //     // if (i > 1000) {
    //     if (p[0] > sum.vertices.size()) {
    //         TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Points[%d][0]=%d exceed %d\n", i, p[0],
    //         sum.vertices.size());
    //     } else {
    //         // printf("Points[%d][0]=%d\n", i, p[0]);
    //     }
    //     if (p[1] > sum.uvs.size()) {
    //         TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Points[%d][1]=%d exceed %d\n", i, p[1],
    //         sum.uvs.size());
    //     } else {
    //         // printf("Points[%d][1]=%d\n", i, p[1]);
    //     }
    //     if (p[2] > sum.normals.size()) {
    //         TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Points[%d][2]=%d exceed %d\n", i, p[2],
    //         sum.normals.size());
    //     } else {
    //         // printf("Points[%d][2]=%d\n", i, p[2]);
    //     }
    // }
    // TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Point size=%d\n", sum.points.size());
    // TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Vertices size=%d\n", sum.vertices.size());
    // TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Uvs size=%d\n", sum.uvs.size());
    // TF_DEBUG_MSG(FILE_FORMAT_OBJ, "normals size=%d\n", sum.normals.size());
}

/// After joining all intermediates into a single global instance `sum`, we now traverse the
/// registry of encountered obj elements `entries` and spawn objects, groups and their geometry
/// and material associations in the `Obj` struct.
void
reindexObjIntermediate(Obj& obj,
                       ObjIntermediate& sum,
                       std::vector<ObjIntermediate> intermediates,
                       std::unordered_map<std::string, int>& materialMap)
{
    ObjObject* o = nullptr;
    ObjGroup* g = nullptr;
    ObjSubset* s = nullptr;
    std::vector<uint8_t> verticesMap(sum.vertices.size());
    std::vector<uint8_t> uvsMap(sum.uvs.size());
    std::vector<uint8_t> normalsMap(sum.normals.size());
    std::vector<int> verticesIndexMap(sum.vertices.size());
    std::vector<int> uvsIndexMap(sum.uvs.size());
    std::vector<int> normalsIndexMap(sum.normals.size());
    size_t vOutOfRangeCount = 0;
    size_t vtOutOfRangeCount = 0;
    size_t vnOutOfRangeCount = 0;

    // This needs to be called when ever we start a new object or group
    auto checkOutOfRange = [&]() {
        if (g) {
            if (vOutOfRangeCount) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                             "Object %s, group %s: Invalid vertex indices: %lu\n",
                             o->name.c_str(),
                             g->name.c_str(),
                             vOutOfRangeCount);
            }
            size_t numVertexIndices = g->indices.size();
            if (vtOutOfRangeCount) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                             "Object %s, group %s: Invalid uv indices: %lu, dropping uvs\n",
                             o->name.c_str(),
                             g->name.c_str(),
                             vtOutOfRangeCount);
                g->uvs.clear();
                g->uvIndices.clear();
            }
            // This can happen when individual UV indices are missing or are invalid. To preserve
            // overall integrity we drop the UVs.
            if (!g->uvIndices.empty() && g->uvIndices.size() != numVertexIndices) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                             "Object %s, group %s: %lu UV indices do not match %lu vertex indices, "
                             "dropping uvs\n",
                             o->name.c_str(),
                             g->name.c_str(),
                             g->uvIndices.size(),
                             numVertexIndices);
                g->uvs.clear();
                g->uvIndices.clear();
            }
            if (vnOutOfRangeCount) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                             "Object %s, group %s: Invalid normal indices: %lu, dropping normals\n",
                             o->name.c_str(),
                             g->name.c_str(),
                             vnOutOfRangeCount);
                g->normals.clear();
                g->normalIndices.clear();
            }
            // This can happen when individual normal indices are missing or are invalid. To
            // preserve overall integrity we drop the normals.
            if (!g->normalIndices.empty() && g->normalIndices.size() != numVertexIndices) {
                TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                             "Object %s, group %s: %lu normal indices do not match %lu vertex "
                             "indices, dropping normals\n",
                             o->name.c_str(),
                             g->name.c_str(),
                             g->normalIndices.size(),
                             numVertexIndices);
                g->normals.clear();
                g->normalIndices.clear();
            }
        }

        vOutOfRangeCount = 0;
        vtOutOfRangeCount = 0;
        vnOutOfRangeCount = 0;
    };
    auto addObject = [&]() {
        checkOutOfRange();
        obj.objects.push_back(ObjObject());
        o = &obj.objects.back();
        g = nullptr;
        s = nullptr;
    };
    auto addGroup = [&]() {
        memset(verticesMap.data(), 0, sizeof(uint8_t) * verticesMap.size());
        memset(uvsMap.data(), 0, sizeof(uint8_t) * uvsMap.size());
        memset(normalsMap.data(), 0, sizeof(uint8_t) * normalsMap.size());
        checkOutOfRange();
        o->groups.push_back(ObjGroup());
        g = &o->groups.back();
        s = nullptr;
    };
    auto addSubset = [&]() {
        g->subsets.push_back(ObjSubset());
        s = &g->subsets.back();
    };
    size_t pOffset = 0;
    size_t vBaseOffset = 0;
    size_t vtBaseOffset = 0;
    size_t vnBaseOffset = 0;
    std::string lastGroupName = "";
    std::string lastMaterialName = "";
    for (const ObjIntermediate& inter : intermediates) {
        int faceOffset = 0;
        int objectOffset = 0;
        int groupOffset = 0;
        int usemtlOffset = 0;

        for (const Entry& e : inter.entries) {
            if (e.type == EntryTypeO) {
                addObject();
                o->name = inter.objects[objectOffset++];
            } else if (e.type == EntryTypeG) {
                g = nullptr;
                s = nullptr;
                lastGroupName = inter.groups[groupOffset++];
            } else if (e.type == EntryTypeUsemtl) {
                lastMaterialName = inter.usemtls[usemtlOffset++];
                s = nullptr;
            } else if (e.type == EntryTypeF) {
                if (!s) {
                    if (!g) {
                        if (!o) {
                            addObject();
                        }
                        if (!lastGroupName.empty()) {
                            addGroup();
                            g->name = lastGroupName;
                            lastGroupName = "";
                        } else {
                            addGroup();
                        }
                    }
                    addSubset();
                    if (lastMaterialName.empty()) {
                        s->material = -1;
                    } else {
                        if (materialMap.find(lastMaterialName) != materialMap.end()) {
                            s->material = materialMap[lastMaterialName];
                        } else {
                            s->material = -1;
                        }
                    }
                }
                size_t vOffset = vBaseOffset + e.vOffset;
                size_t vtOffset = vtBaseOffset + e.vtOffset;
                size_t vnOffset = vnBaseOffset + e.vnOffset;
                for (size_t faceId = 0; faceId < e.count; faceId++) {
                    const GfVec2i& f = inter.faces[faceOffset + faceId];
                    s->faces.push_back(g->faces.size());
                    g->faces.push_back(f[1] - f[0]);
                    for (int pointId = f[0]; pointId != f[1]; pointId++) {
                        const GfVec3i& p = sum.points[pOffset + pointId];
                        if (p[0] != 0) {
                            int index = p[0] > 0 ? p[0] - 1 : vOffset + p[0];
                            if (static_cast<size_t>(index) >= sum.vertices.size()) {
                                vOutOfRangeCount++;
                                continue;
                            }
                            if (verticesMap[index]) {
                                int existingIndex = verticesIndexMap[index];
                                g->indices.push_back(existingIndex);
                            } else {
                                int newIndex = g->vertices.size();
                                if (!sum.colors.empty()) {
                                    g->colors.push_back(sum.colors[index]);
                                }
                                g->vertices.push_back(sum.vertices[index]);
                                g->indices.push_back(newIndex);
                                verticesIndexMap[index] = newIndex;
                                verticesMap[index] = 1;
                            }
                        } else {
                            // This should never happen, since we filter out these indices when we
                            // add to the `points` array.
                            TF_CODING_ERROR("Vertex index of zero!");
                        }
                        if (p[1] != 0) {
                            int index = p[1] > 0 ? p[1] - 1 : vtOffset + p[1];
                            if (static_cast<size_t>(index) >= sum.uvs.size()) {
                                vtOutOfRangeCount++;
                                continue;
                            }
                            if (uvsMap[index]) {
                                int existingIndex = uvsIndexMap[index];
                                g->uvIndices.push_back(existingIndex);
                            } else {
                                int newIndex = g->uvs.size();
                                g->uvs.push_back(sum.uvs[index]);
                                g->uvIndices.push_back(newIndex);
                                uvsIndexMap[index] = newIndex;
                                uvsMap[index] = 1;
                            }
                        } else {
                            // This strategy of filling in missing indices if the mesh otherwise has
                            // data helps to bring in incorrect assets as much as possible, but
                            // fails if the first couple of indices are invalid. This scenario is
                            // detected together with out-of-bounds indices and the primvar is
                            // discarded for this mesh.
                            if (!g->uvs.empty()) {
                                TF_DEBUG_MSG(
                                  FILE_FORMAT_OBJ,
                                  "Vertex %d (of %d), Face %lu, group %s: invalid uv index: %d\n",
                                  pointId - f[0],
                                  f[1] - f[0],
                                  faceId,
                                  o->name.c_str(),
                                  p[1]);
                                // We need to push another index, otherwise the arrays are out of
                                // sync.
                                // We just reference the first UV coordinate, which can/will lead to
                                // garbage data, but is valid. Choosing another valid UV coordinate
                                // on the same face would be better.
                                g->uvIndices.push_back(0);
                            }
                        }
                        if (p[2] != 0) {
                            int index = p[2] > 0 ? p[2] - 1 : vnOffset + p[2];
                            if (static_cast<size_t>(index) >= sum.normals.size()) {
                                vnOutOfRangeCount++;
                                continue;
                            }
                            if (normalsMap[index]) {
                                int existingIndex = normalsIndexMap[index];
                                g->normalIndices.push_back(existingIndex);
                            } else {
                                int newIndex = g->normals.size();
                                g->normals.push_back(sum.normals[index]);
                                g->normalIndices.push_back(newIndex);
                                normalsIndexMap[index] = newIndex;
                                normalsMap[index] = 1;
                            }
                        } else {
                            // This strategy of filling in missing indices if the mesh otherwise has
                            // data helps to bring in incorrect assets as much as possible, but
                            // fails if the first couple of indices are invalid. This scenario is
                            // detected together with out-of-bounds indices and the primvar is
                            // discarded for this mesh.
                            if (!g->normals.empty()) {
                                TF_DEBUG_MSG(
                                  FILE_FORMAT_OBJ,
                                  "Vertex %d (of %d), Face %lu, group %s: invalid normal "
                                  "index: %d\n",
                                  pointId - f[0],
                                  f[1] - f[0],
                                  faceId,
                                  o->name.c_str(),
                                  p[2]);
                                // We need to push another index, otherwise the arrays are out of
                                // sync.
                                // We just reference the first normal, which can/will lead to
                                // garbage data, but is valid. Choosing another valid normal on the
                                // same face would be better.
                                g->normalIndices.push_back(0);
                            }
                        }
                    }
                }
                faceOffset += e.count;
            }
        }
        pOffset += inter.points.size();
        vBaseOffset += inter.vertices.size();
        vtBaseOffset += inter.uvs.size();
        vnBaseOffset += inter.normals.size();
    }
    checkOutOfRange();
}

/// Main multi-threaded implementation of obj reading, leveraging TBB.
/// As outlined above, the steps are:
/// - `splitObjIntermediates`
/// - `readObjIntermediate`
/// - `joinObjIntermediates`
/// - `reindexObjIntermediate`
///
/// Multithreaded work is really only invoked for `readObjIntermediate`.
bool
readObjInternal(Obj& obj,
                const std::vector<char>& data,
                std::unordered_map<std::string, int>& materialMap)
{
    TfStopwatch w;
    ObjIntermediate sum;
    std::vector<ObjIntermediate> intermediates;

    // You can debug single-threaded by setting threadCount = 1 instead
    int threadCount = WorkGetConcurrencyLimit();
    int realThreadCount = WorkGetPhysicalConcurrencyLimit();
    TF_DEBUG_MSG(
      FILE_FORMAT_OBJ, "Thread count: %d, Concurrency limit: %d\n", realThreadCount, threadCount);

    w.Start();
    splitObjIntermediates(data, threadCount, intermediates);
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "splitObjIntermediates time: %ld\n",
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();

    w.Start();
    WorkParallelForEach(intermediates.begin(), intermediates.end(), readObjIntermediate);
    for (const ObjIntermediate& inter : intermediates) {
        if (inter.error) {
            return false;
        }
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "readObjIntermediate time: %ld\n",
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();

    w.Start();
    joinObjIntermediates(obj, sum, intermediates, materialMap);
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "joinObjIntermediates time: %ld\n",
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();

    w.Start();
    reindexObjIntermediate(obj, sum, intermediates, materialMap);
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "reindexObjIntermediate time: %ld\n",
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();
    return true;
}

// Uniquely adds images, keyed by filename, into the Obj, only optionally reading in the actual
// pixel data from filesystem. Returns the (new or existing) image index on success or -1 on error,
// which happens if the format is unsupported.
int
addImage(Obj& obj,
         const std::string& filename,
         std::unordered_map<std::string, int>& imageMap,
         const std::string& parentPath,
         bool readImages)
{
    int imageIndex = obj.images.size();
    auto entry = std::pair<std::string, int>(filename, imageIndex);
    auto it = imageMap.insert(entry);
    if (it.second) { // image entry did not yet exist
        const std::string basename = TfGetBaseName(filename);
        const std::string extension = TfGetExtension(filename);
        obj.images.push_back(ImageAsset());
        ImageAsset& image = obj.images.back();
        image.uri = basename;
        image.name = TfStringGetBeforeSuffix(basename);
        image.format = getFormat(extension);
        obj.importedFilenames.insert(filename);
        if (readImages) {
            std::string fullFilename = parentPath + filename;
            if (!readFileContents(fullFilename,
                                  *(reinterpret_cast<std::vector<char>*>(&image.image)))) {
                TF_WARN("Failed to load image file \"%s\"", fullFilename.c_str());
            }
        }
    }
    return it.first->second;
};

/// Helper function used in `readObjMtl` and `readObjMdl`.
/// Retrieves an ObjMaterial stored in the `materialMap` map, by name.
/// Creates a new one if not found.
ObjMaterial*
getMaterial(Obj& obj, std::unordered_map<std::string, int>& materialMap, const std::string& name)
{
    int materialIndex = obj.materials.size();
    auto entry = std::pair<std::string, int>(name, materialIndex);
    auto it = materialMap.insert(entry);
    if (it.second) {
        obj.materials.push_back(ObjMaterial(name));
    }
    int realMaterialIndex = it.first->second;
    // XXX this is dangerous. This pointer is made invalid if a new material is added to the vector
    return &obj.materials[realMaterialIndex];
}

/// Single-threaded parsing of an mtl material encoded in a string buffer.
/// The materialMap is pre-filled in the obj main parsing, but new entries might be added.
bool
readObjMtl(Obj& obj,
           int i,
           const std::vector<char>& data,
           std::unordered_map<std::string, int>& materialMap,
           std::unordered_map<std::string, int>& imageMap,
           const std::string& parentPath,
           bool readImages)
{
    // Is there a simple way to get these sizes at compile-time?
    static std::string newmtl = "newmtl";
    static std::string ka = "ka";
    static std::string kd = "kd";
    static std::string ks = "ks";
    static std::string ke = "ke";
    static std::string tf = "tf";
    static std::string illum = "illum";
    static std::string d = "d";
    static std::string ns = "ns";
    static std::string sharpness = "sharpness";
    static std::string ni = "ni";
    static std::string pm = "pm";
    static std::string pr = "pr";
    static std::string mapKa = "map_ka";
    static std::string mapKd = "map_kd";
    static std::string mapKs = "map_ks";
    static std::string mapNs = "map_ns";
    static std::string mapKe = "map_ke";
    static std::string mapD = "map_d";
    static std::string mapPm = "map_pm";
    static std::string mapPr = "map_pr";
    static std::string norm = "norm";
    static std::string mapKn = "map_kn";
    static std::string decal = "decal";
    static std::string disp = "disp";
    static std::string bump = "bump";
    static std::string adobeMapNormal = "adobe_map_normal";
    static std::string adobeMapRoughness = "adobe_map_roughness";
    static std::string adobeMapMetallic = "adobe_map_metallic";
    static std::string adobeMapTranslucence = "adobe_map_translucence";
    static std::string adobeTranslucence = "adobe_translucence";
    static std::string adobeInteriorColor = "adobe_interior_color";
    static std::string adobeDensity = "adobe_density";
    static std::string adobeGlow = "adobe_glow";
    static std::string blendu = "-blendu";
    static std::string blendv = "-blendv";
    static std::string cc = "-cc";
    static std::string clamp = "-clamp";
    static std::string imfchan = "-imfchan";
    static std::string mm = "-mm";
    static std::string o = "-o";
    static std::string s = "-s";
    static std::string t = "-t";
    static std::string texres = "-texres";

    int line = 1;
    auto readMap = [&](const char*& p, const char* end, const std::string& mapName, ObjMap& map) {
        while (p < end && *p != '\n' && *p != '\r' && *p != '\0') {
            if (skipWhitespace(p, end))
                return;
            if (*p == '-') {
                if (checkWord(p, end, blendu)) {
                    if (!nextOnOrOff(p, end, map.blendu)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -blendu [on|off]",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, blendv)) {
                    if (!nextOnOrOff(p, end, map.blendv)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -blendv [on|off]",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, cc)) {
                    if (!nextOnOrOff(p, end, map.colorCorrection)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -cc [on|off]",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, clamp)) {
                    if (!nextOnOrOff(p, end, map.clamp)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -clamp [on|off]",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, imfchan)) {
                    if (!nextChannel(p, end, map.channel)) {
                        TF_WARN(
                          "MTL parsing error on line %d, for %s: -imfchan expects valid channel",
                          line,
                          mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, mm)) {
                    if (!nextFloat(p, end, map.base) | !nextFloat(p, end, map.gain)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -mm expects 2 floats",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, o)) {
                    if (!nextFloat3(p, end, map.origin)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -o expects 3 floats",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, s)) {
                    if (!nextFloat3(p, end, map.scale)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -s expects 3 floats",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, t)) {
                    if (!nextFloat3(p, end, map.turbulence)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -t expects 3 floats",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else if (checkWord(p, end, texres)) {
                    // XXX texres is unused
                    float texres;
                    if (!nextFloat(p, end, texres)) {
                        TF_WARN("MTL parsing error on line %d, for %s: -texres expects float",
                                line,
                                mapName.c_str());
                        break;
                    }
                } else {
                    // We don't know how many params are in an unrecognized map keyword,
                    // so best to break
                    TF_WARN("MTL parsing error on line %d, for %s: unrecognized map keyword",
                            line,
                            mapName.c_str());
                    break;
                }
            } else {
                nextSpacedText(p, end, map.filename);
                map.image = addImage(obj, map.filename, imageMap, parentPath, readImages);
                map.defined = map.image != -1;
                break;
            }
        }
    };

    TfStopwatch w;
    w.Start();
    ObjMaterialLibrary& materialLibrary = obj.libraries[i];
    ObjMaterial* m = nullptr;
    const char* p = data.data();
    const char* end = data.data() + data.size();
    while (p < end - 2) { // -2 ensures at least 2 characters per line
        for (; p < end && (*p == ' ' || *p == '\t'); p++)
            ;
        if (checkWord(p, end, newmtl)) {
            std::string materialName;
            nextSpacedText(p, end, materialName);
            m = getMaterial(obj, materialMap, materialName);
            m->defined = true;
        } else if (m && !m->mdlDefined) { // mdl material has preference
            // TODO replace switching with a map
            if (checkWord(p, end, ka)) {
                if (!nextFloat3(p, end, m->ka)) {
                    TF_WARN("MTL parsing error on line %d, after Ka: expected 3 floats", line);
                }
            } else if (checkWord(p, end, kd)) {
                if (!nextFloat3(p, end, m->kd)) {
                    TF_WARN("MTL parsing error on line %d, after Kd: expected 3 floats", line);
                }
            } else if (checkWord(p, end, ks)) {
                if (!nextFloat3(p, end, m->ks)) {
                    TF_WARN("MTL parsing error on line %d, after Ks: expected 3 floats", line);
                }
            } else if (checkWord(p, end, ke)) {
                if (!nextFloat3(p, end, m->ke)) {
                    TF_WARN("MTL parsing error on line %d, after Ke: expected 3 floats", line);
                }
            } else if (checkWord(p, end, tf)) {
                if (!nextFloat3(p, end, m->tf)) {
                    TF_WARN("MTL parsing error on line %d, after Tf: expected 3 floats", line);
                }
            } else if (checkWord(p, end, illum)) {
                if (!nextInteger(p, end, m->illum)) {
                    TF_WARN("MTL parsing error on line %d, after illum: expected integer", line);
                }
            } else if (checkWord(p, end, d)) {
                if (!nextFloat(p, end, m->d)) {
                    TF_WARN("MTL parsing error on line %d, after d: expected float", line);
                }
            } else if (checkWord(p, end, ns)) {
                if (!nextFloat(p, end, m->ns)) {
                    TF_WARN("MTL parsing error on line %d, after Ns: expected float", line);
                }
            } else if (checkWord(p, end, sharpness)) {
                if (!nextFloat(p, end, m->sharpness)) {
                    TF_WARN("MTL parsing error on line %d, after sharpness: expected float", line);
                }
            } else if (checkWord(p, end, ni)) {
                if (!nextFloat(p, end, m->ni)) {
                    TF_WARN("MTL parsing error on line %d, after Ni: expected float", line);
                }
            } else if (checkWord(p, end, pm)) {
                if (!nextFloat(p, end, m->metallic)) {
                    TF_WARN("MTL parsing error on line %d, after Pm: expected float", line);
                }
            } else if (checkWord(p, end, pr)) {
                if (!nextFloat(p, end, m->roughness)) {
                    TF_WARN("MTL parsing error on line %d, after Pr: expected float", line);
                }
            } else if (checkWord(p, end, mapKa)) {
                readMap(p, end, mapKa, m->mapKa);
            } else if (checkWord(p, end, mapKd)) {
                readMap(p, end, mapKd, m->mapKd);
            } else if (checkWord(p, end, mapKs)) {
                readMap(p, end, mapKs, m->mapKs);
            } else if (checkWord(p, end, mapNs)) {
                readMap(p, end, mapNs, m->mapNs);
            } else if (checkWord(p, end, mapKe)) {
                readMap(p, end, mapKe, m->mapKe);
            } else if (checkWord(p, end, mapD)) {
                readMap(p, end, mapD, m->mapD);
            } else if (checkWord(p, end, mapPr)) {
                readMap(p, end, mapPr, m->mapRoughness);
            } else if (checkWord(p, end, mapPm)) {
                readMap(p, end, mapPm, m->mapMetallic);
            } else if (checkWord(p, end, norm)) {
                readMap(p, end, norm, m->norm);
            } else if (checkWord(p, end, mapKn)) {
                readMap(p, end, mapKn, m->norm);
            } else if (checkWord(p, end, decal)) {
                readMap(p, end, decal, m->decal);
            } else if (checkWord(p, end, disp)) {
                readMap(p, end, disp, m->disp);
            } else if (checkWord(p, end, bump)) {
                readMap(p, end, bump, m->bump);
            } else if (checkWord(p, end, adobeMapNormal)) {
                obj.hasAdobeProperties = true;
                readMap(p, end, adobeMapNormal, m->norm);
            } else if (checkWord(p, end, adobeMapRoughness)) {
                obj.hasAdobeProperties = true;
                readMap(p, end, adobeMapRoughness, m->mapRoughness);
            } else if (checkWord(p, end, adobeMapMetallic)) {
                obj.hasAdobeProperties = true;
                readMap(p, end, adobeMapMetallic, m->mapMetallic);
            } else if (checkWord(p, end, adobeMapTranslucence)) {
                obj.hasAdobeProperties = true;
                readMap(p, end, adobeMapTranslucence, m->mapTranslucence);
            } else if (checkWord(p, end, adobeTranslucence)) {
                obj.hasAdobeProperties = true;
                if (!nextFloat(p, end, m->translucence)) {
                    TF_WARN(
                      "MTL parsing error on line %d, after adobe_translucence: expected float",
                      line);
                }
            } else if (checkWord(p, end, adobeInteriorColor)) {
                obj.hasAdobeProperties = true;
                if (!nextFloat3(p, end, m->interiorColor)) {
                    TF_WARN("MTL parsing error on line %d, after adobe_interior_color: expected 3 "
                            "floats",
                            line);
                }
            } else if (checkWord(p, end, adobeDensity)) {
                obj.hasAdobeProperties = true;
                if (!nextFloat(p, end, m->density)) {
                    TF_WARN("MTL parsing error on line %d, after adobe_density: expected float",
                            line);
                }
            } else if (checkWord(p, end, adobeGlow)) {
                obj.hasAdobeProperties = true;
                if (!nextFloat(p, end, m->glow)) {
                    TF_WARN("MTL parsing error on line %d, after adobe_glow: expected float", line);
                }
            }
        }
        nextLine(p, end);
        ++line;
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "Read mtl %s (%d lines) in %lu ms\n",
                 materialLibrary.filename.c_str(),
                 line,
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();
    return true;
}

/// Single-threaded parsing of an mdl material encoded in a string buffer.
/// Given mdl is script-generated and limited in scope we use a very simple sscanf approach here.
/// The materialMap is pre-filled in the obj main parsing, but new entries might be added.
bool
readObjMdl(Obj& obj,
           int i,
           const std::vector<char>& data,
           std::unordered_map<std::string, int>& materialMap,
           std::unordered_map<std::string, int>& imageMap,
           const std::string& parentPath,
           bool readImages)
{
    auto readMap = [&](ObjMap& map, std::string& filename) {
        map.filename = filename;
        map.image = addImage(obj, filename, imageMap, parentPath, readImages);
        map.defined = map.image != -1;
    };

    TfStopwatch w;
    w.Start();
    float x, r, g, b;
    std::string line, materialName, parameter, filename;
    ObjMaterial* m = nullptr;
    std::stringstream stream(data.data());
    int lineCount = 0;
    while (getline(stream, line)) {
        ++lineCount;
        if (line.empty())
            continue;
        materialName.resize(line.size(), '\0'); // resize guarantees no overflow
        parameter.resize(line.size(), '\0');
        filename.resize(line.size(), '\0');
        if (sscanf(line.c_str(), "export material %[^()]", &materialName[0]) >= 1) {
            materialName.resize(materialName.find('\0')); // must shrink now for proper comparison
            m = getMaterial(obj, materialMap, materialName);
            if (!m)
                continue;
            // overwrite anything that might have been written by mtl
            *m = ObjMaterial(materialName);
            m->defined = true;
            m->mdlDefined = true;
        } else if (m) {
            if (sscanf(line.c_str(), "    %s : float(%f)", &parameter[0], &x) == 2) {
                parameter.resize(parameter.find('\0'));
                if (parameter == "opacity") {
                    m->opacity = x;
                } else if (parameter == "metallic") {
                    m->metallic = x;
                } else if (parameter == "roughness") {
                    m->roughness = x;
                } else if (parameter == "height") {
                    m->height = x;
                } else if (parameter == "heightScale") {
                    m->heightScale = x;
                } else if (parameter == "indexOfRefraction") {
                    m->ni = x;
                } else if (parameter == "glow") {
                    m->glow = x;
                } else if (parameter == "translucence") {
                    m->translucence = x;
                } else if (parameter == "density") {
                    m->density = x;
                }
            } else if (sscanf(
                         line.c_str(), "    %s : color(%f, %f, %f)", &parameter[0], &r, &g, &b) ==
                       4) {
                parameter.resize(parameter.find('\0'));
                if (parameter == "baseColor") {
                    m->kd = GfVec3f(r, g, b);
                } else if (parameter == "interiorColor") {
                    m->interiorColor = GfVec3f(r, g, b);
                }
            } else if (sscanf(line.c_str(),
                              "    baseColor : adobe::util::color_texture( texture_2d(\"%[^\"]\"",
                              &filename[0]) == 1) {
                filename.resize(filename.find('\0'));
                readMap(m->mapKd, filename);
            } else if (sscanf(line.c_str(),
                              "    normal : adobe::util::normal_texture( texture_2d(\"%[^\"]\"",
                              &filename[0]) == 1) {
                filename.resize(filename.find('\0'));
                readMap(m->norm, filename);
            } else if (sscanf(line.c_str(),
                              "    %s : adobe::util::float_texture( texture_2d(\"%[^\"]\"",
                              &parameter[0],
                              &filename[0]) == 2) {
                parameter.resize(parameter.find('\0'));
                filename.resize(filename.find('\0'));
                if (parameter == "roughness") {
                    readMap(m->mapRoughness, filename);
                } else if (parameter == "metallic") {
                    readMap(m->mapMetallic, filename);
                } else if (parameter == "opacity") {
                    readMap(m->mapOpacity, filename);
                } else if (parameter == "glow") {
                    readMap(m->mapGlow, filename);
                } else if (parameter == "translucence") {
                    readMap(m->mapTranslucence, filename);
                } else {
                    TF_WARN("Unsupported MDL float_texture '%s' with file '%s'",
                            parameter.c_str(),
                            filename.c_str());
                }
            }
        }
    }
    w.Stop();
    TF_DEBUG_MSG(FILE_FORMAT_OBJ,
                 "Read mdl %s (%d lines) in %lu ms\n",
                 obj.libraries[i].filename.c_str(),
                 lineCount,
                 static_cast<long int>(w.GetMilliseconds()));
    w.Reset();
    return true;
}

/// Reads all file contents into a string buffer and hands off control to `readObjInternal`.
/// Only reads materials if some material was found in the main obj file.
/// For materials, again all contents are first read into a string buffer, and then fed
/// to `readObjMdl` or `readObjMtl`.
/// Note we keep track of the filenames composing the obj model.
bool
readObj(Obj& obj, const std::string& filename, bool readImages)
{
    TfStopwatch watch;
    watch.Start();
    std::string baseName = TfGetBaseName(filename);
    obj.importedFilenames.insert(baseName);
    std::vector<char> objBuffer;
    GUARD(readFileContents(filename, objBuffer), "Failed reading obj file");
    watch.Stop();
    TF_DEBUG_MSG(
      FILE_FORMAT_OBJ, "read obj time: %lu\n", static_cast<long int>(watch.GetMilliseconds()));
    std::unordered_map<std::string, int> materialMap;
    std::unordered_map<std::string, int> imageMap;
    GUARD(readObjInternal(obj, objBuffer, materialMap), "Failed parsing obj");

    if (obj.materials.size()) {
        const std::string parentPath = TfGetPathName(filename);
        for (size_t i = 0; i < obj.libraries.size(); i++) {
            ObjMaterialLibrary& library = obj.libraries[i];
            obj.importedFilenames.insert(library.filename);
            std::string materialFilename = parentPath + library.filename;
            std::vector<char> materialBuffer;
            if (!readFileContents(materialFilename, materialBuffer)) {
                TF_WARN("Failed to open material file \"%s\"", materialFilename.c_str());
                continue;
            }
            if (library.isMdl) {
                obj.hasAdobeProperties = true;
                GUARD(
                  readObjMdl(obj, i, materialBuffer, materialMap, imageMap, parentPath, readImages),
                  "Failed parsing mdl");
            } else {
                GUARD(
                  readObjMtl(obj, i, materialBuffer, materialMap, imageMap, parentPath, readImages),
                  "Failed parsing mtl");
            }
        }
    }
    return true;
}

/// Directly hands off control to `readObjInternal`.
/// No material reading is done here! unless we came up with a syntax to stack 1 or more
/// material library string buffers after the main obj string buffer.
/// Note no filename is associated to the obj model with this function.
bool
readObj(Obj& obj, const std::vector<char>& data)
{
    std::unordered_map<std::string, int> materialMap;
    readObjInternal(obj, data, materialMap);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
/// OBJ WRITE /////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// Control for buffered writes to a stream.
// Only when the buffer is full will it write the buffer contents to file, for reduced IO latency.
// Althought sometimes it's useful to directly write to the file. For that, use `directWrite`.
class BufferControl
{
    size_t bufferSize;
    char* buffer;
    const char* end;
    char* p;
    int flushCount;
    std::fstream& file;

  public:
    BufferControl(size_t bufferSize, std::fstream& file)
      : bufferSize(bufferSize)
      , flushCount(0)
      , file(file)
    {
        buffer = new char[bufferSize];
        end = buffer + bufferSize;
        p = buffer;
    }

    ~BufferControl()
    {
        TF_DEBUG_MSG(FILE_FORMAT_OBJ, "Destroying buffer. Flush count %d\n", flushCount);
        delete buffer;
    }

    template<typename... Args>
    bool write(const char* format, Args... args)
    {
        const int maxLineSize = 200;
        if (p + maxLineSize > end) {
            flushCount++;
            file.write(buffer, p - buffer);
            p = buffer;
        }
        auto result = fmt::format_to_n(p, maxLineSize, format, args...);
        if (result.size <= maxLineSize) {
            p += result.size;
            return true;
        } else {
            return false;
        }
    }

    void directWrite(const std::string& text) { file.write(text.data(), text.size()); }

    void flush()
    {
        flushCount++;
        file.write(buffer, p - buffer);
    }
};

void
writeObjHeader(const Obj& obj, std::fstream& file)
{
    BufferControl buffer(128000, file);

    buffer.directWrite("# Obj model");
    buffer.directWrite("\n# This model was generated by the USD fileformat plugin");
    for (const auto& comment : obj.comments)
        buffer.directWrite(std::string("\n") + comment);
    buffer.flush();
}

// Writes obj geometry to the stream `file` in a buffered way.
/// See `BufferControl`.
void
writeObjGeometry(const Obj& obj, std::fstream& file)
{
    BufferControl buffer(128000, file);

    if (obj.libraries.size()) {
        buffer.directWrite("\n\nmtllib");
        for (const ObjMaterialLibrary& m : obj.libraries) {
            buffer.directWrite(" " + m.filename);
        }
    }
    int vOffset = 1;
    int vtOffset = 1;
    int vnOffset = 1;
    for (size_t i = 0; i < obj.objects.size(); i++) {
        const ObjObject& o = obj.objects[i];
        buffer.write("\n\no {}", o.name);
        for (size_t j = 0; j < o.groups.size(); j++) {
            const ObjGroup& g = o.groups[j];
            buffer.write("\n\ng {}", g.name);
            if (g.colors.size()) {
                for (size_t i = 0; i < g.vertices.size(); i++) {
                    const GfVec3f& v = g.vertices[i];
                    const GfVec3f& c = g.colors[i];
                    buffer.write("\nv {} {} {} {} {} {}", v[0], v[1], v[2], c[0], c[1], c[2]);
                }
            } else {
                for (const GfVec3f& v : g.vertices) {
                    buffer.write("\nv {} {} {}", v[0], v[1], v[2]);
                }
            }
            for (const GfVec2f& v : g.uvs) {
                buffer.write("\nvt {} {}", v[0], v[1]);
            }
            for (const GfVec3f& v : g.normals) {
                buffer.write("\nvn {} {} {}", v[0], v[1], v[2]);
            }
            std::vector<int> faceOffsets(g.faces.size());
            size_t accumulated = 0;
            for (size_t j = 0; j < faceOffsets.size(); j++) {
                faceOffsets[j] = accumulated;
                accumulated += g.faces[j];
            }
            for (const ObjSubset& s : g.subsets) {
                if (s.material != -1) {
                    buffer.write("\n\nusemtl {}", obj.materials[s.material].name);
                }

                for (const int& faceId : s.faces) {
                    buffer.write("\nf");
                    for (int f = faceOffsets[faceId]; f < faceOffsets[faceId] + g.faces[faceId];
                         f++) {
                        const bool hasTextures = f < static_cast<int>(g.uvIndices.size());
                        const bool hasNormals = f < static_cast<int>(g.normalIndices.size());
                        if (hasTextures && hasNormals) {
                            int vIndex = g.indices[f] + vOffset;
                            int vtIndex = g.uvIndices[f] + vtOffset;
                            int vnIndex = g.normalIndices[f] + vnOffset;
                            buffer.write(" {}/{}/{}", vIndex, vtIndex, vnIndex);
                        } else if (!hasTextures && !hasNormals) {
                            int vIndex = g.indices[f] + vOffset;
                            buffer.write(" {}", vIndex);
                        } else if (hasTextures && !hasNormals) {
                            int vIndex = g.indices[f] + vOffset;
                            int vtIndex = g.uvIndices[f] + vtOffset;
                            buffer.write(" {}/{}", vIndex, vtIndex);
                        } else if (!hasTextures && hasNormals) {
                            int vIndex = g.indices[f] + vOffset;
                            int vnIndex = g.normalIndices[f] + vnOffset;
                            buffer.write(" {}//{}", vIndex, vnIndex);
                        }
                    }
                }
            }
            vOffset += g.vertices.size();
            vtOffset += g.uvs.size();
            vnOffset += g.normals.size();
        }
    }
    buffer.flush();
}

// Writes obj materials from `library` to the stream `file`.
void
writeObjMaterials(const Obj& obj, const ObjMaterialLibrary& library, std::fstream& file)
{
    std::stringstream ss;

    auto writeMap = [&](const std::string& name, const ObjMap& map) -> void {
        if (!map.filename.empty()) {
            ss << name;
            if (map.scale != GfVec3f(1.0f)) {
                ss << " -s " << map.scale[0] << " " << map.scale[1] << " 1.0";
            }
            if (map.origin != GfVec3f(0.0f)) {
                ss << " -o " << map.origin[0] << " " << map.origin[1] << " 0.0";
            }
            ss << " " << map.filename << "\n";
        }
    };

    for (int i : library.materials) {
        const ObjMaterial& m = obj.materials[i];
        ss << "\n";
        ss << "newmtl " << m.name << "\n";
        if (m.ka != PXR_NS::GfVec3f(-1)) {
            ss << "Ka " << m.ka[0] << " " << m.ka[1] << " " << m.ka[2] << "\n";
        }
        if (m.kd != PXR_NS::GfVec3f(-1)) {
            ss << "Kd " << m.kd[0] << " " << m.kd[1] << " " << m.kd[2] << "\n";
        }
        if (m.ks != PXR_NS::GfVec3f(-1)) {
            ss << "Ks " << m.ks[0] << " " << m.ks[1] << " " << m.ks[2] << "\n";
        }
        if (m.tf != PXR_NS::GfVec3f(-1)) {
            ss << "Tr " << m.tf[0] << " " << m.tf[1] << " " << m.tf[2] << "\n";
        }
        if (m.illum != -1) {
            ss << "illum " << m.illum << "\n";
        }
        if (m.d != -1) {
            if (m.hasHalo) {
                ss << "d -halo " << m.d << "\n";
            } else {
                ss << "d " << m.d << "\n";
            }
        }
        if (m.ns != -1) {
            ss << "Ns " << m.ns << "\n";
        }
        if (m.sharpness != -1) {
            ss << "sharpness " << m.sharpness << "\n";
        }
        if (m.ni != -1) {
            ss << "Ni " << m.ni << "\n";
        }
        writeMap("map_Ka", m.mapKa);
        writeMap("map_Kd", m.mapKd);
        writeMap("map_Ks", m.mapKs);
        writeMap("map_Ns", m.mapNs);
        writeMap("map_d", m.mapD);
        writeMap("norm", m.norm);
        writeMap("decal", m.decal);
        writeMap("disp", m.disp);
        writeMap("bump", m.bump);
    }
    const std::string& str = ss.str();
    file.write(str.data(), str.length());
    ;
}

/// Single-threaded implementation of obj write to file `filename` in 3 stages:
/// 1) write geometry
/// 2) write materials
/// 3) write images
bool
writeObj(const Obj& obj, const std::string& filename, bool sameMaterialName)
{
    const std::string& parentPath = TfGetPathName(filename);
    TfMakeDirs(parentPath, -1, true);
    std::fstream objFile(filename, std::ios_base::out);
    if (!objFile.is_open()) {
        TF_WARN("Failed to open obj file \"%s\"", filename.c_str());
        return false;
    }

    writeObjHeader(obj, objFile);

    writeObjGeometry(obj, objFile);

    for (const ObjMaterialLibrary& library : obj.libraries) {
        const std::string& mtlFilename = parentPath + library.filename;
        std::fstream mtlFile(mtlFilename, std::ios_base::out);
        if (!mtlFile.is_open()) {
            TF_WARN("Failed to open obj material library file \"%s\"", mtlFilename.c_str());
            return false;
        }
        writeObjMaterials(obj, library, mtlFile);
    }
    for (const ImageAsset& image : obj.images) {
        if (image.uri.empty()) { // can this actually happen?
            continue;
        }
        const std::string& imgFilename = parentPath + image.uri;
        const std::string& imgParentPath = TfGetPathName(imgFilename);
        TfMakeDirs(imgParentPath, -1, true);
        std::fstream textureFile(imgFilename, std::ios::out | std::ios::binary);
        if (!textureFile.is_open()) {
            TF_WARN("Failed to open texture file \"%s\"", imgFilename.c_str());
            return false;
        }
        textureFile.write(reinterpret_cast<const char*>(image.image.data()), image.image.size());
        textureFile.close();
    }
    return true;
}

/// TODO implement
bool
writeObj(const Obj& obj, std::string& output)
{
    return false;
}

}
