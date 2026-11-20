"""Extract root-directory files from a FAT12 floppy image (PC-98 .hdm etc.). usage: fatx.py image outdir"""
import os
import struct
import sys

img = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
bps, spc, rsv, nfat, nroot, tot16, media, spf = struct.unpack_from('<HBHBHHBH', img, 11)
print('bytes/sector', bps, 'sec/cluster', spc, 'reserved', rsv, 'fats', nfat, 'root', nroot, 'sec/fat', spf)
fat = img[rsv * bps:(rsv + spf) * bps]
root_off = (rsv + nfat * spf) * bps
data_off = root_off + ((nroot * 32 + bps - 1) // bps) * bps
csize = spc * bps


def fat12(n):
    o = n * 3 // 2
    v = fat[o] | (fat[o + 1] << 8)
    return (v >> 4) if (n & 1) else (v & 0xfff)


for i in range(nroot):
    e = img[root_off + i * 32:root_off + i * 32 + 32]
    if e[0] == 0:
        break
    if e[0] == 0xe5 or e[11] & 0x08 or e[11] & 0x10:
        continue
    name = e[0:8].decode('ascii').strip()
    ext = e[8:11].decode('ascii').strip()
    fn = name + ('.' + ext if ext else '')
    clus = struct.unpack_from('<H', e, 26)[0]
    size = struct.unpack_from('<I', e, 28)[0]
    data = b''
    while 2 <= clus < 0xff0 and len(data) < size:
        o = data_off + (clus - 2) * csize
        data += img[o:o + csize]
        clus = fat12(clus)
    data = data[:size]
    open(os.path.join(out, fn), 'wb').write(data)
    print(fn, size, len(data))
