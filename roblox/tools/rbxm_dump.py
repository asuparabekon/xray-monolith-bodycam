"""Minimal Roblox binary model (.rbxm) reader: prints the instance tree and writes every
script's Source to a file.

Usage: python3 roblox/tools/rbxm_dump.py Model.rbxm out_dir
Supports LZ4-compressed and uncompressed chunks (not zstd). Pure Python, no packages.
"""
import struct, sys, os, json

def lz4_block(src, size):
    out = bytearray(); i = 0
    while i < len(src):
        token = src[i]; i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i+lit]; i += lit
        if i >= len(src): break
        off = src[i] | (src[i+1] << 8); i += 2
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(out) - off
        for k in range(ml):
            out.append(out[start + k])
    assert len(out) == size, (len(out), size)
    return bytes(out)

def untransform(x): return (x >> 1) ^ -(x & 1)

def interleaved_i32(data, pos, n):
    raw = data[pos:pos+4*n]
    vals = []
    for k in range(n):
        b = bytes(raw[j*n + k] for j in range(4))
        vals.append(untransform(struct.unpack(">I", b)[0]))
    return vals, pos + 4*n

def refs(data, pos, n):
    vals, pos = interleaved_i32(data, pos, n)
    acc = 0; out = []
    for v in vals:
        acc += v; out.append(acc)
    return out, pos

def read_string(data, pos):
    (n,) = struct.unpack_from("<I", data, pos); pos += 4
    return data[pos:pos+n], pos + n

def main(path, outdir):
    data = open(path, "rb").read()
    assert data[:8] == b"<roblox!"
    pos = 8 + 6 + 2
    num_types, num_inst = struct.unpack_from("<ii", data, pos); pos += 16
    classes = {}; props = {}; parents = {}
    while pos < len(data):
        name = data[pos:pos+4]; comp, uncomp = struct.unpack_from("<II", data, pos+4); pos += 16
        if comp == 0:
            chunk = data[pos:pos+uncomp]; pos += uncomp
        else:
            raw = data[pos:pos+comp]; pos += comp
            assert raw[:4] != b"\x28\xb5\x2f\xfd", "zstd not supported"
            chunk = lz4_block(raw, uncomp)
        if name == b"INST":
            cid, = struct.unpack_from("<I", chunk, 0)
            cname, p = read_string(chunk, 4)
            p += 1
            n, = struct.unpack_from("<I", chunk, p); p += 4
            ids, p = refs(chunk, p, n)
            classes[cid] = (cname.decode(), ids)
        elif name == b"PROP":
            cid, = struct.unpack_from("<I", chunk, 0)
            pname, p = read_string(chunk, 4)
            ptype = chunk[p]; p += 1
            ids = classes[cid][1]
            if ptype == 0x01:  # string
                for ref in ids:
                    s, p = read_string(chunk, p)
                    props.setdefault(ref, {})[pname.decode()] = s
        elif name == b"PRNT":
            p = 1
            n, = struct.unpack_from("<I", chunk, p); p += 4
            child, p = refs(chunk, p, n)
            par, p = refs(chunk, p, n)
            for c, q in zip(child, par): parents[c] = q
        elif name == b"END\0":
            break
    cls_of = {}
    for cname, ids in classes.values():
        for r in ids: cls_of[r] = cname
    def path_of(r):
        parts = []
        while r is not None and r != -1 and r in cls_of:
            parts.append(props.get(r, {}).get("Name", b"?").decode())
            r = parents.get(r, -1)
        return "/".join(reversed(parts))
    tree = []
    for r in sorted(cls_of):
        entry = {"ref": r, "class": cls_of[r], "path": path_of(r)}
        src = props.get(r, {}).get("Source")
        if src is not None:
            ext = {"Script": ".server.luau", "LocalScript": ".client.luau"}.get(cls_of[r], ".luau")
            fname = os.path.join(outdir, entry["path"] + ext)
            os.makedirs(os.path.dirname(fname), exist_ok=True)
            open(fname, "wb").write(src)
            entry["file"] = fname; entry["lines"] = src.count(b"\n") + 1
        tree.append(entry)
    for e in tree:
        print(e["class"], e["path"], e.get("lines", ""))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
