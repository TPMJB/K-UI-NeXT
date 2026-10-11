#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Continue a Freerouting run from its own session.

    tools/ses_to_dsn.py BASE.dsn ROUTED.ses OUT.dsn

Freerouting 1.9 joins copper only where ends coincide exactly. After a
session has been imported into KiCad and exported again, enough ends move by
rounding that the router saw 245 open connections where KiCad saw 21. This
appends the session's wires and vias to the DSN the session was routed from,
as ordinary (unfixed) wiring, so a new run starts from the router's own
geometry. Wiring the DSN already holds (locked stubs, fixed vias) is kept; a
session never repeats it. Plain Python, no pcbnew.
"""
import re
import sys


def tokens(text):
    return re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)


def parse(tok, i=0):
    if tok[i] != '(':
        raise ValueError('expected ( at token %d' % i)
    i += 1
    node = []
    while tok[i] != ')':
        if tok[i] == '(':
            sub, i = parse(tok, i)
            node.append(sub)
        else:
            node.append(tok[i])
            i += 1
    return node, i + 1


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    base, ses, out = sys.argv[1:]
    dsn = open(base).read()
    session = open(ses).read()
    if not re.search(r'\(unit um\)', dsn):
        raise SystemExit('BASE.dsn must use um units')
    res = int(re.search(r'\(routes\s*\(resolution um (\d+)\)', session).group(1))
    tree, _ = parse(tokens(session[session.index('(network_out'):]))

    def um(v):
        return ('%.4f' % (float(v) / res)).rstrip('0').rstrip('.')

    lines, wires, vias = [], 0, 0
    for net in tree[1:]:
        if not isinstance(net, list) or net[0] != 'net':
            continue
        name = net[1]
        for item in net[2:]:
            if item[0] == 'wire':
                path = item[1]
                if path[0] != 'path':
                    raise ValueError('%s: unsupported wire shape %s' % (name, path[0]))
                lines.append('    (wire (path %s %s  %s)(net %s)(type route))'
                             % (path[1], um(path[2]), ' '.join(um(v) for v in path[3:]), name))
                wires += 1
            elif item[0] == 'via':
                lines.append('    (via %s  %s %s (net %s)(type route))' % (item[1], um(item[2]), um(item[3]), name))
                vias += 1
    start = dsn.index('(wiring')
    depth = 0
    for end in range(start, len(dsn)):
        depth += dsn[end] == '('
        depth -= dsn[end] == ')'
        if depth == 0:
            break
    open(out, 'w').write(dsn[:end] + '\n'.join(lines) + '\n  ' + dsn[end:])
    print('appended %d wires and %d vias' % (wires, vias))


if __name__ == '__main__':
    main()
