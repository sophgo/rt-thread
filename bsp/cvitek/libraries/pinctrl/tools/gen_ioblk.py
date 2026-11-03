#!/usr/bin/env python3
"""Generate chip/<chip>/<chip>_reg_ioblk.h (IOBLK pad control registers).

Input : the CV180x/CV181x PINOUT_*.xlsx workbooks; sheet "3. 管脚控制寄存器*" is
        used, taking the union over every package of one chip family.
Usage : python3 gen_ioblk.py <dir with the PINOUT xlsx files> [<chip dir root>]
        (chip dir root defaults to ../chip, i.e. next to this tools/ directory)
Needs : openpyxl
"""
import sys, os, re
import openpyxl

SP = ''
CHIPS = {
    'cv180x': [('CV180xB_QFN68_PINOUT_V0.2_CN.xlsx', '3. 管脚控制寄存器(QFN68)'),
               ('CV180xC_QFN88_PINOUT_V0.1_CN.xlsx', '3. 管脚控制寄存器(QFN88)')],
    'cv181x': [('CV181xH__PINOUT_V0.1_CN.xlsx', '3. 管脚控制寄存器'),
               ('CV181xC__PINOUT_V0.1_CN.xlsx', '3. 管脚控制寄存器(QFN)'),
               ('CV1815J__PINOUT_V0.1_CN.xlsx', '3. 管脚控制寄存器')],
}
GROUP_BASE = {'G1': 0x03001800, 'G7': 0x03001900, 'G10': 0x03001A00,
              'G11': 0x03001B00, 'G12': 0x03001C00, 'GRTC': 0x05027000}


def load(chip):
    regs = {}    # reg name -> (group, addr, pin)
    fields = {}  # (reg, field) -> bit
    order = []   # reg names in discovery order
    for f, s in CHIPS[chip]:
        ws = openpyxl.load_workbook(SP + f, read_only=True, data_only=True)[s]
        cur = None
        for r in ws.iter_rows(values_only=True):
            if r[1] and r[2] and r[2] != 'Register Name':
                reg = str(r[2]).strip()
                addr = int(str(r[3]).replace('_', ''), 16)
                m = re.match(r'IOBLK_(G\d+|GRTC)_REG_(\w+)$', reg)
                assert m, reg
                cur = reg
                if reg in regs:
                    assert regs[reg][1] == addr, (chip, reg, hex(addr), hex(regs[reg][1]))
                else:
                    regs[reg] = (m.group(1), addr, m.group(2))
                    order.append(reg)
            if r[4] and cur and r[4] != 'Field Name':
                fld = str(r[4]).strip()
                assert fld.startswith(cur + '_'), (cur, fld)
                bit = int(r[5])
                if (cur, fld) in fields:
                    assert fields[(cur, fld)] == bit, (chip, fld)
                fields[(cur, fld)] = bit
    return regs, fields, order


def gen(chip, outdir):
    regs, fields, order = load(chip)
    order.sort(key=lambda n: (list(GROUP_BASE).index(regs[n][0]), regs[n][1]))
    guard = '__%s_REG_IOBLK_H__' % chip.upper()
    out = []
    w = out.append
    w('// IOBLK pad control registers (PU/PD/DS/ST/HE/SL) of %s' % chip.upper())
    w('//')
    w('// GENERATED from the PINOUT xlsx, sheet "3. 管脚控制寄存器" (union of all packages of')
    w('// this chip family). Do not edit by hand; regenerate with')
    w('// bsp/cvitek/libraries/pinctrl/tools/gen_ioblk.py')
    w('//')
    w('// IOBLK_<grp>_BASE         physical base address of the register group')
    w('// IOBLK_<grp>_REG_<pin>    register offset against the group base')
    w('// ..._<pin>_<field>        same offset, per field; _OFFSET = bit position, _MASK = bit mask')
    w('//')
    w('// Pin-name entry points (no need to know which group a pin lives in):')
    w('// IOBLK_PAD_<pin>_ADDR     physical address of the pin pad register')
    w('// IOBLK_PAD_<pin>_PULL_MASK  PU|PD bits of that register')
    w('// IOBLK_PAD_<pin>_PU_MASK    PU bit only (also _PD_MASK)')
    w('')
    w('#ifndef %s' % guard)
    w('#define %s' % guard)
    w('')
    used = []
    for g in GROUP_BASE:
        if any(regs[n][0] == g for n in order):
            used.append(g)
            w('#define  IOBLK_%s_BASE  0x%08x' % (g, GROUP_BASE[g]))
    w('')
    for g in used:
        w('// ---- IOBLK_%s ----' % g)
        gnames = [n for n in order if regs[n][0] == g]
        for n in gnames:
            off = regs[n][1] - GROUP_BASE[g]
            assert 0 <= off < 0x100, (n, hex(regs[n][1]))
            w('#define  %s  0x%x' % (n, off))
        for n in gnames:
            off = regs[n][1] - GROUP_BASE[g]
            for (reg, fld), bit in sorted(((k, v) for k, v in fields.items() if k[0] == n), key=lambda kv: kv[1]):
                w('#define  %s   0x%x' % (fld, off))
                w('#define  %s_OFFSET %d' % (fld, bit))
                w('#define  %s_MASK   0x%x' % (fld, 1 << bit))
        w('')
    # pin-name based entry points; pin names must be unique within one chip
    seen = {}
    for n in order:
        g, addr, pin = regs[n]
        assert pin not in seen, (chip, 'pin in two groups', pin, seen[pin], g)
        seen[pin] = g
    w('// ---- pin-name entry points ----')
    for n in order:
        g, addr, pin = regs[n]
        w('#define  IOBLK_PAD_%s_ADDR  (IOBLK_%s_BASE + IOBLK_%s_REG_%s)' % (pin, g, g, pin))
        # a pad without PU/PD (e.g. XTAL_XIN) gets no PULL_MASK: using it fails to compile
        if (n, n + '_PU') in fields and (n, n + '_PD') in fields:
            w('#define  IOBLK_PAD_%s_PULL_MASK  (IOBLK_%s_REG_%s_PU_MASK | IOBLK_%s_REG_%s_PD_MASK)' % (pin, g, pin, g, pin))
            w('#define  IOBLK_PAD_%s_PU_MASK    IOBLK_%s_REG_%s_PU_MASK' % (pin, g, pin))
            w('#define  IOBLK_PAD_%s_PD_MASK    IOBLK_%s_REG_%s_PD_MASK' % (pin, g, pin))
    w('')
    w('#endif /* %s */' % guard)
    path = os.path.join(outdir, chip, '%s_reg_ioblk.h' % chip)
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(out) + '\n')
    print(chip, 'regs', len(order), 'fields', len(fields), '->', path)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    SP = os.path.join(sys.argv[1], '')
    root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'chip')
    for c in CHIPS:
        gen(c, root)
