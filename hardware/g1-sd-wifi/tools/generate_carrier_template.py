#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-size carrier fit sheets from the native board, without print scaling.

Run with /usr/bin/python3 (pcbnew and ReportLab). This is a fit-check draft,
not an installation or assembly drawing. Native Edge.Cuts and pad positions
are read directly; measured motherboard references remain provisional.
"""
import hashlib
import json
from pathlib import Path

import pcbnew
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, letter
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = Path(__file__).resolve().parents[1]
PCB = ROOT / 'controller/KUI-G1-Bridge-RevA.kicad_pcb'
OUT = ROOT / 'mechanical'
# Board walls (tools/generate_board.py outline: 59 x 115 mm, BIOS opening
# x 6-35 / y 61-80, CN503 notch x 43.8-59 / y 52-99), each with the side the
# laminate is on, for the support-band overlay.
BAND_MM = 3.0
WALLS = [('y', 0, 0, 115, +1), ('y', 59, 0, 52, -1), ('y', 59, 99, 115, -1),
         ('x', 0, 0, 59, +1), ('x', 115, 0, 59, -1),
         ('y', 43.8, 52, 99, -1), ('x', 52, 43.8, 59, -1), ('x', 99, 43.8, 59, +1),
         ('y', 6, 61, 80, -1), ('y', 35, 61, 80, +1), ('x', 61, 6, 35, -1), ('x', 80, 6, 35, +1)]
# microSD withdrawal corridor: the card's width north from its tail inside
# J201 (y 1.84) to 10 mm beyond the board edge, for J201 at (47, 11.587).
SD_CORRIDOR = (41.3, -10.0, 52.9, 1.84)
J201_AT = (47.0, 11.587, 0.0)
# The C5 module reference plus a 3 mm clamp envelope.
C5_ENVELOPE = (3.5733-3, 1.0973-3, 3.5733+20.9804+3, 1.0973+17.8054+3)


def check_geometry(board, ox, oy, edges):
    """Fail if the outline or J201 no longer match the overlay constants."""
    segs = [(round(pcbnew.ToMM(e.GetStart().x)-ox, 3), round(pcbnew.ToMM(e.GetStart().y)-oy, 3),
             round(pcbnew.ToMM(e.GetEnd().x)-ox, 3), round(pcbnew.ToMM(e.GetEnd().y)-oy, 3)) for e in edges]
    for axis, at, lo, hi, side in WALLS:
        def covers(x0, y0, x1, y1):
            if axis == 'y':
                return abs(x0-at) < .01 and abs(x1-at) < .01 and min(y0, y1) <= lo+.01 and max(y0, y1) >= hi-.01
            return abs(y0-at) < .01 and abs(y1-at) < .01 and min(x0, x1) <= lo+.01 and max(x0, x1) >= hi-.01
        if not any(covers(*sg) for sg in segs):
            raise ValueError('Edge.Cuts no longer match WALLS at %s=%s; update the overlay constants' % (axis, at))
    j = board.FindFootprintByReference('J201')
    at = (round(pcbnew.ToMM(j.GetPosition().x)-ox, 3), round(pcbnew.ToMM(j.GetPosition().y)-oy, 3),
          round(j.GetOrientationDegrees(), 3))
    if at != J201_AT:
        raise ValueError('J201 moved to %s; update SD_CORRIDOR' % (at,))


def support_bands(board, ox, oy):
    """Wall bands BAND_MM deep clear of every pad, courtyard and the C5
    clamp envelope, at least 3 mm long."""
    boxes = [C5_ENVELOPE]
    for fp in board.GetFootprints():
        shapes = [p.GetBoundingBox() for p in fp.Pads()]
        if fp.GetCourtyard(pcbnew.F_CrtYd).OutlineCount():
            shapes.append(fp.GetCourtyard(pcbnew.F_CrtYd).BBox())
        for bb in shapes:
            boxes.append((pcbnew.ToMM(bb.GetX())-ox, pcbnew.ToMM(bb.GetY())-oy,
                          pcbnew.ToMM(bb.GetRight())-ox, pcbnew.ToMM(bb.GetBottom())-oy))
    bands = []
    for axis, at, lo, hi, side in WALLS:
        d0, d1 = sorted((at, at+side*BAND_MM))
        run, v = None, lo
        while v <= hi+1e-9:
            r = (d0, v-.05, d1, v+.05) if axis == 'y' else (v-.05, d0, v+.05, d1)
            hit = any(not (b[2] < r[0] or b[0] > r[2] or b[3] < r[1] or b[1] > r[3]) for b in boxes)
            if not hit and run is None: run = max(lo, v-.05)
            if (hit or v+.1 > hi+1e-9) and run is not None:
                end = v-.05 if hit else hi
                if end-run >= 3: bands.append((d0, run, d1, end) if axis == 'y' else (run, d0, end, d1))
                run = None
            v += .1
    return bands

def make_pdf(board, filename, page, checksum):
    c = canvas.Canvas(str(filename), pagesize=page, invariant=1)
    c.setTitle('K-UI G1 Rev A - routed carrier fit check - actual size')
    c.setAuthor('K-UI design work')
    w, h = page
    c.setFillColor(colors.HexColor('#172638'))
    c.setFont('KUI-Bold', 15)
    c.drawString(18*mm, h-20*mm, 'K-UI G1 carrier: actual-size fit sheet')
    c.setFont('KUI', 9)
    c.drawString(18*mm, h-27*mm, 'ROUTED BOARD - physical fit and mounting still need your Dreamcast check.')
    c.drawString(18*mm, h-33*mm, 'Print this PDF on matching paper: Actual size / 100%; turn off Fit and Shrink.')
    edges = [x for x in board.GetDrawings() if x.GetLayer() == pcbnew.Edge_Cuts]
    ox = min(pcbnew.ToMM(x.GetStart().x) for x in edges)
    oy = min(pcbnew.ToMM(x.GetStart().y) for x in edges)
    check_geometry(board, ox, oy, edges)
    left, top = 25*mm, h-52*mm
    def xy(x,y): return left+x*mm, top-y*mm
    def line(a,b): c.line(*xy(*a), *xy(*b))
    def rectangle(x,y,dx,dy,fill=0):
        c.rect(left+x*mm, top-(y+dy)*mm, dx*mm, dy*mm, stroke=1, fill=fill)
    c.setStrokeColor(colors.HexColor('#aeb9c4')); c.setFillColor(colors.HexColor('#e2e7eb'))
    c.setLineWidth(.12*mm)
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            x=pcbnew.ToMM(pad.GetPosition().x)-ox
            y=pcbnew.ToMM(pad.GetPosition().y)-oy
            sx,sy=pcbnew.ToMM(pad.GetSize().x),pcbnew.ToMM(pad.GetSize().y)
            c.saveState(); c.translate(*xy(x,y)); c.rotate(pad.GetOrientationDegrees())
            c.rect(-sx*mm/2,-sy*mm/2,sx*mm,sy*mm,stroke=1,fill=1); c.restoreState()
    c.setStrokeColor(colors.black); c.setLineWidth(.25*mm)
    for edge in edges:
        if edge.GetShape() != pcbnew.SHAPE_T_SEGMENT:
            raise ValueError('Template currently requires native straight Edge.Cuts')
        a=edge.GetStart(); b=edge.GetEnd()
        line((pcbnew.ToMM(a.x)-ox,pcbnew.ToMM(a.y)-oy),
             (pcbnew.ToMM(b.x)-ox,pcbnew.ToMM(b.y)-oy))
    # Measurement aids for docs/OWNER-NEXT-STEPS.md item 3, drawn under the
    # reference outlines so those stay visible.
    c.setStrokeColor(colors.HexColor('#8e44ad')); c.setFillColor(colors.HexColor('#e8d5f0'))
    for x0,y0,x1,y1 in support_bands(board, ox, oy):
        rectangle(x0,y0,x1-x0,y1-y0,fill=1)
    c.setDash(2,2); c.setStrokeColor(colors.HexColor('#267799'))
    # Motherboard references register to the BIOS opening and CN503 notch,
    # which tools/stretch_board.py moved 10 mm south with everything below
    # y = 41.4 mm; the C5 module reference sits in the unmoved north part.
    rectangle(7,64,26.68,12.65)
    rectangle(45.68,57,6.56,38.67)
    rectangle(3.5733,1.0973,20.9804,17.8054)
    c.setDash(1,2)
    x0,y0,x1,y1=C5_ENVELOPE
    rectangle(x0,y0,x1-x0,y1-y0)
    c.setDash()
    c.setFont('KUI-Bold',6.5); c.setFillColor(colors.HexColor('#267799'))
    for text,x,y in [('BIOS body reference',7,70),('CN503',46,76),('C5 module',4,10)]:
        c.drawString(*xy(x,y),text)
    c.setStrokeColor(colors.HexColor('#d35400')); c.setLineWidth(.3*mm)
    for ref in ('J50','J51'):
        bb=board.FindFootprintByReference(ref).GetCourtyard(pcbnew.F_CrtYd).BBox()
        x0,y0=pcbnew.ToMM(bb.GetX())-ox,pcbnew.ToMM(bb.GetY())-oy
        rectangle(x0,y0,pcbnew.ToMM(bb.GetWidth()),pcbnew.ToMM(bb.GetHeight()))
    c.setStrokeColor(colors.HexColor('#1e8449')); c.setDash(3,1.5)
    x0,y0,x1,y1=SD_CORRIDOR
    rectangle(x0,y0,x1-x0,y1-y0)
    c.setDash(); c.setLineWidth(.12*mm)
    c.setFont('KUI-Bold',5.5); c.setFillColor(colors.HexColor('#1e8449'))
    c.drawString(*xy(x0+1.2,y0+5.5),'SD out')
    c.setFillColor(colors.HexColor('#172638'))
    c.setFont('KUI-Bold',6)
    focus={'U10','U201','U202','U203','J201','J50','J51','U40','J20'}
    for fp in board.GetFootprints():
        if fp.GetReference() in focus:
            pos=fp.GetPosition()
            c.drawCentredString(*xy(pcbnew.ToMM(pos.x)-ox,pcbnew.ToMM(pos.y)-oy),fp.GetReference())
    c.setLineWidth(.2*mm); c.setStrokeColor(colors.HexColor('#172638'))
    line((65,9),(65,0)); line((65,0),(63.7,2)); line((65,0),(66.3,2))
    c.setFont('KUI-Bold',8); c.drawString(*xy(63.5,-2),'N')
    textx=102*mm; texty=top-8*mm
    lines=[
        ('Fit check (OWNER-NEXT-STEPS item 3)', True),
        ('1. Measure BOTH 50 mm bars.',False),
        ('2. Centre IC501 in the opening; match CN503.',False),
        ('3. Edges: west >=1 mm from the thermal chip',False),
        ('   and its pad imprint; east >=0.5 mm inside',False),
        ('   the motherboard edge; north >=1 mm from AV.',False),
        ('4. Mark CE501/502/504 and every part >2 mm',False),
        ('   tall under solid laminate.',False),
        ('5. Trace the 6 mm / 8 mm shield boundary.',False),
        ('6. Mark case posts; note heights in purple.',False),
        ('7. Green box: microSD pull-out needs 10 mm',False),
        ('   clear past the edge, nothing over 3.3 mm.',False),
        ('',False),
        ('Paper geometry is exact; registration is draft.',False),
        ('Black: native carrier edge and openings.',False),
        ('Gray: actual carrier component pads.',False),
        ('Blue dashed: provisional existing hardware;',False),
        ('   dotted: C5 plus 3 mm clamp envelope.',False),
        ('Orange: J50/J51 flex sockets (2.2 mm tall).',False),
        ('Purple: 3 mm edge bands free for supports.',False),
        ('',False),
        ('Carrier: 59 x 115 mm; PCB 0.8 mm.',False),
        ('Nominal underside: motherboard +2.5 mm.',False),
        ('Nominal top: motherboard +3.3 mm.',False),
        ('6 mm zone: connector stack up to 5.6 mm.',False),
        ('Only 0.4 mm before other tolerances.',False),
        ('8 mm zone: C5 plus clamp <=3.7 mm',False),
        ('above seating plane at max PCB thickness.',False),
        ('Tall existing capacitors need clear openings.',False),
        ('Paper fit does not establish stack height.',False),
        ('',False),
        ('C5 contacts need a supported 0.90 mm gap',False),
        ('and at least 6.24 N total clamp reaction.',False),
        ('Support and clamp are not yet qualified.',False),
    ]
    for value,bold in lines:
        c.setFont('KUI-Bold' if bold else 'KUI',8)
        c.drawString(textx,texty,value); texty-=4.5*mm
    c.setFont('KUI-Bold',8)
    c.drawString(25*mm, top-119*mm, '50 mm horizontal calibration')
    c.setLineWidth(.25*mm)
    line((0,124),(50,124))
    for x in range(0,51,10): line((x,122.5),(x,125.5))
    c.setFont('KUI',7)
    for x in range(0,51,10): c.drawCentredString(*xy(x,130),str(x))
    line((72,108),(72,158))
    for y in range(108,159,10): line((70.5,y),(73.5,y))
    c.saveState(); c.translate(*xy(66,133)); c.rotate(90)
    c.setFont('KUI-Bold',8); c.drawCentredString(0,0,'50 mm vertical calibration'); c.restoreState()
    c.setFont('KUI',8)
    c.drawString(18*mm,34*mm,'If either bar is wrong, the printer scaled the page. Do not resize board geometry.')
    c.drawString(18*mm,28*mm,'No shield cutting is assumed. This sheet is not a fabrication release.')
    c.setFont('KUI',6.5)
    c.drawString(18*mm,20*mm,'PCB SHA-256: '+checksum)
    c.showPage(); c.save()

def main():
    pdfmetrics.registerFont(TTFont('KUI','/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'))
    pdfmetrics.registerFont(TTFont('KUI-Bold','/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'))
    OUT.mkdir(exist_ok=True)
    board=pcbnew.LoadBoard(str(PCB))
    if board is None: raise ValueError('Cannot load native carrier')
    checksum=hashlib.sha256(PCB.read_bytes()).hexdigest()
    for name,page in [('carrier-fit-US-Letter.pdf',letter),('carrier-fit-A4.pdf',A4)]:
        make_pdf(board,OUT/name,page,checksum)
    (OUT/'template-provenance.json').write_text(json.dumps({
        'pcb_sha256':checksum,'status':'unqualified_fit_check_draft',
        'native_edges_and_pad_positions':True,'scale':1,
        'board_envelope_mm':[59,115],'calibration_mm':[50,50],
        'motherboard_references':'Provisional registration from owner dimensions; not a measured complete obstacle map',
        'files':['carrier-fit-US-Letter.pdf','carrier-fit-A4.pdf']},indent=2)+'\n')
    print('Created exact-size Letter/A4 fit sheets from native PCB.')

if __name__=='__main__': main()
