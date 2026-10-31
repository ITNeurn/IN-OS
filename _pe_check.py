import struct, sys
p = sys.argv[1]
d = open(p, 'rb').read()
print('size', len(d))
assert d[:2] == b'MZ', 'no MZ'
e = struct.unpack_from('<I', d, 0x3C)[0]
print('e_lfanew', hex(e))
assert d[e:e+4] == b'PE\0\0'
mach = struct.unpack_from('<H', d, e+4)[0]
nsec = struct.unpack_from('<H', d, e+6)[0]
opt = e + 24
magic = struct.unpack_from('<H', d, opt)[0]
aep = struct.unpack_from('<I', d, opt+16)[0]
imgbase = struct.unpack_from('<Q', d, opt+24)[0]
subsys = struct.unpack_from('<H', d, opt+68)[0]
print('machine', hex(mach), 'sections', nsec, 'optmagic', hex(magic),
      'entryRVA', hex(aep), 'imagebase', hex(imgbase), 'subsystem', subsys)
assert mach == 0x8664 and magic == 0x20B and subsys == 10
sec = opt + struct.unpack_from('<H', d, e+20)[0]
for i in range(nsec):
    o = sec + i*40
    name = d[o:o+8].rstrip(b'\0')
    vs, va, rs, rp = struct.unpack_from('<IIII', d, o+8)
    print(name, 'vsize', hex(vs), 'vaddr', hex(va),
          'rawsize', hex(rs), 'rawptr', hex(rp))
print('entry file off 0x%x' % (0x200 + (aep - 0x1000)))
print('first code bytes:', d[0x200:0x210].hex())
print('PE OK')
