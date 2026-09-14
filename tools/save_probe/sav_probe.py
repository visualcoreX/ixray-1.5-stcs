# -*- coding: utf-8 -*-
# Read a Clear Sky .sav and report the Limansk quest-chain state.
#
# File layout (CALifeStorageManager::save): u32 0xFFFFFFFF, u32 ALIFE_VERSION, u32 source_count,
# then LZO1X-1 compressed data (rt_compressor.cpp: lzo1x_1_compress / lzo1x_decompress).
# The decompressor below is a straight port of src/utils/xrCompress/lzo/lzo1x_d.ch, non-dict build.
# COPY4 paths in the C code only run on non-overlapping 4-byte chunks, so a byte-wise copy is exact.
import struct
import sys


def lzo1x_decompress(src, out_len):
    out = bytearray()
    ip = 0

    def copy_back(m_pos, n):
        if m_pos < 0:
            raise ValueError("lookbehind overrun at op=%d" % len(out))
        for i in range(n):
            out.append(out[m_pos + i])

    if src[0] > 17:
        t = src[0] - 17
        ip = 1
        if t < 4:
            state = "match_next"
        else:
            out += src[ip:ip + t]; ip += t
            state = "first_literal_run"
    else:
        state = "loop"

    while True:
        if state == "loop":
            t = src[ip]; ip += 1
            if t >= 16:
                state = "match"; continue
            if t == 0:
                while src[ip] == 0:
                    t += 255; ip += 1
                t += 15 + src[ip]; ip += 1
            out += src[ip:ip + t + 3]; ip += t + 3
            state = "first_literal_run"

        elif state == "first_literal_run":
            t = src[ip]; ip += 1
            if t >= 16:
                state = "match"; continue
            m_pos = len(out) - (1 + 0x0800) - (t >> 2) - (src[ip] << 2); ip += 1
            copy_back(m_pos, 3)
            state = "match_done"

        elif state == "match":
            if t >= 64:                                   # M2
                m_pos = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1
                t = (t >> 5) - 1
                copy_back(m_pos, t + 2)
            elif t >= 32:                                 # M3
                t &= 31
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 31 + src[ip]; ip += 1
                m_pos = len(out) - 1 - ((src[ip] >> 2) + (src[ip + 1] << 6)); ip += 2
                copy_back(m_pos, t + 2)
            elif t >= 16:                                 # M4
                m_pos = len(out) - ((t & 8) << 11)
                t &= 7
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 7 + src[ip]; ip += 1
                m_pos -= (src[ip] >> 2) + (src[ip + 1] << 6); ip += 2
                if m_pos == len(out):
                    break                                 # EOF marker
                m_pos -= 0x4000
                copy_back(m_pos, t + 2)
            else:                                         # M1
                m_pos = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
                copy_back(m_pos, 2)
            state = "match_done"

        elif state == "match_done":
            t = src[ip - 2] & 3
            state = "loop" if t == 0 else "match_next"

        elif state == "match_next":
            out += src[ip:ip + t]; ip += t
            t = src[ip]; ip += 1
            state = "match"

    if len(out) != out_len:
        raise ValueError("size mismatch: got %d, header says %d" % (len(out), out_len))
    return bytes(out), ip, len(src)


def printable(b):
    return "".join(chr(c) if 32 <= c < 127 else "." for c in b)


def main(path):
    raw = open(path, "rb").read()
    marker, version, source_count = struct.unpack_from("<III", raw, 0)
    print("file %s\n  marker=%08X version=%d raw=%d compressed=%d" % (path, marker, version, source_count, len(raw) - 12))
    data, used, total = lzo1x_decompress(raw[12:], source_count)
    print("  decompressed OK, input consumed %d of %d" % (used, total))

    print("\n=== lim_quest_line states present (with context) ===")
    for s in ("first_task", "ambush_2_task", "go_to_canal", "find_comander", "destroy_military_minigun",
              "construction_task", "construction_task_complete", "cs_soldier_cover", "find_transformer",
              "shutdown_transformer"):
        key = ("sr_idle@" + s).encode() + b"\x00"
        i = data.find(key)
        hits = data.count(key)
        if hits:
            print("  %-40s x%d  ...%s" % ("sr_idle@" + s, hits, printable(data[max(0, i - 90):i + len(key) + 10])))

    print("\n=== info portions of the chain (exact, zero-terminated) ===")
    for info in ("lim_go_to_canal_complete", "lim_military_minigun_task", "lim_military_minigun_out",
                 "lim_actor_skip_military_minigun_task", "lim_construction_passed", "lim_final_scene_begin",
                 "lim_cs_recon_squad_alert", "lim_recon_squad_wait", "lim_recon_squad_go_1",
                 "lim_final_sniper_fire", "lim_final_sniper_fire2", "lim_recon_squad_order_1",
                 "lim_final_sniper_fire_actor", "lim_electro_fence_actor_come", "lim_electro_fence",
                 "lim_shut_transformer_task", "lim_final_allyes_cover_fire", "lim_final_actor_find_transformer",
                 "lim_electro_fence_off", "lim_final_sniper_off", "lim_csky_house_debrief_done"):
        print("  %-40s %s" % (info, "YES" if (info.encode() + b"\x00") in data else "-"))

    print("\n=== recon commander (work1) states present ===")
    for s in ("camper@wait_work1", "remark@threat_actor_work1", "remark@greet_actor_work1", "remark@greet2_actor_work1",
              "remark@greet_actor2_work1", "patrol@enter_work1", "camper@runner_work1", "camper@runner2_work1",
              "smartcover@hide_work1", "smartcover@fire_work1", "smartcover@wait2_work1", "smartcover@talk_work1",
              "smartcover@talk2_work1", "smartcover@talk3_work1", "smartcover@hide2_work1", "smartcover@fire2_work1"):
        key = s.encode() + b"\x00"
        n = data.count(key)
        if n:
            i = data.find(key)
            print("  %-32s x%d  ...%s" % (s, n, printable(data[max(0, i - 70):i + len(key) + 6])))

    print("\n=== commander restrictor (lim_final_squad_commander_restr) states ===")
    for s in ("sr_idle@wait_order", "sr_idle@wait", "sr_idle@check_actor", "sr_idle@check"):
        key = s.encode() + b"\x00"
        n = data.count(key)
        if n:
            print("  %-28s x%d" % (s, n))


if __name__ == "__main__":
    main(sys.argv[1])
