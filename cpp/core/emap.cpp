// Mimic - inswapper's "emap": the 512 x 512 matrix (its graph's last initializer) that turns an ArcFace
// identity into what the swapper wants. Read straight from the .onnx file's protobuf, walking past everything else,
// so the 554 MB model isn't read twice.
#include "emap.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

struct Reader {
    FILE* f;
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const int c = fgetc(f);
            if (c == EOF) return false;
            v |= (uint64_t)(c & 0x7f) << shift;
            if (!(c & 0x80)) return true;
        }
        return false;
    }
    int64_t tell() { return _ftelli64(f); }
    bool seek(int64_t p) { return _fseeki64(f, p, SEEK_SET) == 0; }
    // skip a field of this wire type
    bool skip(int wire) {
        uint64_t v;
        if (wire == 0) return varint(v);
        if (wire == 1) return _fseeki64(f, 8, SEEK_CUR) == 0;
        if (wire == 5) return _fseeki64(f, 4, SEEK_CUR) == 0;
        if (wire == 2) return varint(v) && _fseeki64(f, (int64_t)v, SEEK_CUR) == 0;
        return false;
    }
};

}  // namespace

bool readEmap(const std::string& path, std::vector<float>& out, std::string& err) {
#ifdef _WIN32
    FILE* f = _wfopen(std::filesystem::u8path(path).wstring().c_str(), L"rb");
#else
    FILE* f = fopen(path.c_str(), "rb");
#endif
    if (!f) { err = "Can't open " + path; return false; }
    Reader r{f};
    _fseeki64(f, 0, SEEK_END);
    const int64_t size = r.tell();
    r.seek(0);
    // ModelProto: field 7 is the graph
    int64_t graphAt = -1, graphEnd = -1;
    while (r.tell() < size) {
        uint64_t tag;
        if (!r.varint(tag)) break;
        const int field = (int)(tag >> 3), wire = (int)(tag & 7);
        if (field == 7 && wire == 2) {
            uint64_t len; r.varint(len);
            graphAt = r.tell(); graphEnd = graphAt + (int64_t)len;
            break;
        }
        if (!r.skip(wire)) break;
    }
    if (graphAt < 0) { fclose(f); err = "No graph in " + path; return false; }
    // GraphProto: field 5 is each initializer; keep the last one's place
    int64_t lastAt = -1, lastEnd = -1;
    r.seek(graphAt);
    while (r.tell() < graphEnd) {
        uint64_t tag;
        if (!r.varint(tag)) break;
        const int field = (int)(tag >> 3), wire = (int)(tag & 7);
        if (field == 5 && wire == 2) {
            uint64_t len; r.varint(len);
            lastAt = r.tell(); lastEnd = lastAt + (int64_t)len;
            r.seek(lastEnd);
            continue;
        }
        if (!r.skip(wire)) break;
    }
    if (lastAt < 0) { fclose(f); err = "No initializers in " + path; return false; }
    // TensorProto: dims (1), data_type (2), float_data (4, packed), raw_data (9)
    std::vector<int64_t> dims;
    std::vector<uint8_t> raw;
    int dataType = 1;   // 1 float, 10 float16
    r.seek(lastAt);
    out.clear();
    while (r.tell() < lastEnd) {
        uint64_t tag;
        if (!r.varint(tag)) break;
        const int field = (int)(tag >> 3), wire = (int)(tag & 7);
        if (field == 2 && wire == 0) { uint64_t v; r.varint(v); dataType = (int)v; }
        else if (field == 1 && wire == 0) { uint64_t v; r.varint(v); dims.push_back((int64_t)v); }
        else if (field == 1 && wire == 2) { uint64_t len; r.varint(len); const int64_t end = r.tell() + (int64_t)len; while (r.tell() < end) { uint64_t v; r.varint(v); dims.push_back((int64_t)v); } }
        else if ((field == 9 || field == 4) && wire == 2) {
            uint64_t len; r.varint(len);
            raw.resize((size_t)len);
            if (fread(raw.data(), 1, raw.size(), f) != raw.size()) { raw.clear(); break; }
        } else if (!r.skip(wire)) break;
    }
    fclose(f);
    if (dataType == 10) {   // half floats (the fp16 inswapper): widened
        out.resize(raw.size() / 2);
        for (size_t i = 0; i < out.size(); ++i) {
            const uint16_t h = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
            const uint32_t sign = (h & 0x8000u) << 16, ex = (h >> 10) & 0x1f, man = h & 0x3ff;
            uint32_t bits;
            if (ex == 0) bits = sign;                                   // (subnormals to zero: none in an emap)
            else if (ex == 31) bits = sign | 0x7f800000u | (man << 13);
            else bits = sign | ((ex + 112) << 23) | (man << 13);
            memcpy(&out[i], &bits, 4);
        }
    } else {
        out.resize(raw.size() / 4);
        memcpy(out.data(), raw.data(), out.size() * 4);
    }
    if (out.size() != 512 * 512) { err = "inswapper's emap isn't 512 x 512 (" + std::to_string(out.size()) + " values)"; out.clear(); return false; }
    return true;
}
