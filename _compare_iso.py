# -*- coding: utf-8 -*-
# 对比 pycdlib 生成的 ISO 和我的 ISO 的字节布局
import struct

def analyze(iso_path, label):
    print(f"\n{'='*60}")
    print(f"=== {label}: {iso_path} ===")
    with open(iso_path, 'rb') as f:
        data = f.read()
    S = 2048
    print(f"ISO 大小: {len(data)} bytes = {len(data)//S} sectors")

    # 扫描 Volume Descriptors (LBA 16 开始)
    print("\n--- Volume Descriptors ---")
    lba = 16
    while lba < len(data)//S:
        vd = data[lba*S:(lba+1)*S]
        vtype = vd[0]
        ident = vd[1:6]
        if ident != b'CD001':
            break
        ver = vd[6]
        type_names = {0: 'Boot Record', 1: 'PVD', 2: 'SVD', 3: 'VPD', 255: 'Terminator'}
        tname = type_names.get(vtype, f'Unknown({vtype})')
        print(f"  LBA {lba}: Type={vtype} ({tname}), Ver={ver}")
        if vtype == 0:  # Boot Record
            boot_sys_id = vd[7:39].rstrip(b'\x00')
            boot_cat_lba = struct.unpack_from('<I', vd, 71)[0]
            print(f"    Boot System ID: {boot_sys_id!r}")
            print(f"    Boot Catalog LBA: {boot_cat_lba} (at offset 71)")
            # 检查 boot catalog
            if boot_cat_lba < len(data)//S:
                bc = data[boot_cat_lba*S:(boot_cat_lba+1)*S]
                print(f"    --- Boot Catalog (LBA {boot_cat_lba}) ---")
                # Validation Entry
                print(f"    Validation: Header={bc[0]:02x}, Platform={bc[1]:02x}, "
                      f"Manuf={bc[4:28]!r}, Key={bc[30]:02x}{bc[31]:02x}")
                chk = 0
                for i in range(0,32,2):
                    chk = (chk + (bc[i] | (bc[i+1]<<8))) & 0xFFFF
                print(f"    Validation checksum verify: {chk:04x} (expect 0000)")
                # Initial Entry
                ie = bc[32:64]
                print(f"    Initial Entry: Boot={ie[0]:02x}, Media={ie[1]:02x}, "
                      f"LoadSeg={struct.unpack_from('<H', ie, 2)[0]:04x}, "
                      f"SysType={ie[4]:02x}")
                print(f"    SectorCount={struct.unpack_from('<H', ie, 6)[0]}, "
                      f"LoadRBA={struct.unpack_from('<I', ie, 8)[0]}")
                # 检查 boot image
                load_rba = struct.unpack_from('<I', ie, 8)[0]
                if load_rba < len(data)//S:
                    bi = data[load_rba*S:load_rba*S+32]
                    print(f"    Boot Image (LBA {load_rba}): {bi.hex()}")
        elif vtype == 1:  # PVD
            vol_id = vd[40:72].rstrip(b' ')
            space_le = struct.unpack_from('<I', vd, 80)[0]
            space_be = struct.unpack_from('>I', vd, 84)[0]
            set_size_le = struct.unpack_from('<H', vd, 120)[0]
            set_size_be = struct.unpack_from('>H', vd, 122)[0]
            seqnum_le = struct.unpack_from('<H', vd, 124)[0]
            seqnum_be = struct.unpack_from('>H', vd, 126)[0]
            log_blk_le = struct.unpack_from('<H', vd, 128)[0]
            log_blk_be = struct.unpack_from('>H', vd, 130)[0]
            pt_size_le = struct.unpack_from('<I', vd, 132)[0]
            pt_size_be = struct.unpack_from('>I', vd, 136)[0]
            l_pt = struct.unpack_from('<I', vd, 140)[0]
            opt_l_pt = struct.unpack_from('<I', vd, 144)[0]
            m_pt = struct.unpack_from('>I', vd, 148)[0]
            opt_m_pt = struct.unpack_from('>I', vd, 152)[0]
            fsv = vd[881]
            # Root dir record
            rdr = vd[156:190]
            rdr_len = rdr[0]
            rdr_lba_le = struct.unpack_from('<I', rdr, 2)[0]
            rdr_lba_be = struct.unpack_from('>I', rdr, 6)[0]
            rdr_size_le = struct.unpack_from('<I', rdr, 10)[0]
            rdr_size_be = struct.unpack_from('>I', rdr, 14)[0]
            print(f"    Volume ID: {vol_id!r}")
            print(f"    Space Size: LE={space_le} BE={space_be} (match={space_le==space_be})")
            print(f"    Set Size: LE={set_size_le} BE={set_size_be} (match={set_size_le==set_size_be})")
            print(f"    SeqNum: LE={seqnum_le} BE(swab)={struct.unpack('>H', struct.pack('<H', seqnum_be))[0]} (match={seqnum_le==struct.unpack('>H', struct.pack('<H', seqnum_be))[0]})")
            print(f"    Log Block Size: LE={log_blk_le} BE={log_blk_be} (match={log_blk_le==log_blk_be})")
            print(f"    Path Table Size: LE={pt_size_le} BE={pt_size_be} (match={pt_size_le==pt_size_be})")
            print(f"    L Path Table LBA: {l_pt}, Opt: {opt_l_pt}")
            print(f"    M Path Table LBA: {m_pt}, Opt: {opt_m_pt}")
            print(f"    File Structure Version: {fsv}")
            print(f"    Root Dir: len={rdr_len}, LBA LE={rdr_lba_le} BE={rdr_lba_be}, "
                  f"size LE={rdr_size_le} BE={rdr_size_be}")
        if vtype == 255:
            break
        lba += 1

analyze(r'C:\qemu_iso\test_pycdlib.iso', 'pycdlib 生成的正确 ISO')
analyze(r'C:\qemu_iso\test_min.iso', '我手工构造的 ISO')
