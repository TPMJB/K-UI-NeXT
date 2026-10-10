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

def make_pdf(board, filename, page, checksum):
    c = canvas.Canvas(str(filename), pagesize=page, invariant=1)
    c.setTitle('K-UI G1 Rev A - provisional carrier fit sheet - actual size')
    c.setAuthor('K-UI design work')
    w, h = page
    c.setFillColor(colors.HexColor('#172638'))
    c.setFont('KUI-Bold', 15)
    c.drawString(18*mm, h-20*mm, 'K-UI G1 carrier: actual-size fit sheet')
    c.setFont('KUI', 9)
    c.drawString(18*mm, h-27*mm, 'DRAFT - outline registration, support and installed fit are unqualified.')
    c.drawString(18*mm, h-33*mm, 'Print this PDF on matching paper: Actual size / 100%; turn off Fit and Shrink.')
    edges = [x for x in board.GetDrawings() if x.GetLayer() == pcbnew.Edge_Cuts]
    ox = min(pcbnew.ToMM(x.GetStart().x) for x in edges)
    oy = min(pcbnew.ToMM(x.GetStart().y) for x in edges)
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
    c.setDash(2,2); c.setStrokeColor(colors.HexColor('#267799'))
    rectangle(7,54,26.68,12.65)
    rectangle(45.68,47,6.56,38.67)
    rectangle(3.5733,1.0973,20.9804,17.8054)
    c.setDash()
    c.setFont('KUI-Bold',6.5); c.setFillColor(colors.HexColor('#267799'))
    for text,x,y in [('BIOS body reference',7,60),('CN503',46,66),('C5 module reference',4,10)]:
        c.drawString(*xy(x,y),text)
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
        ('Fit check, before cutting paper', True),
        ('1. Measure BOTH 50 mm bars.',False),
        ('2. Match the BIOS and CN503 outlines.',False),
        ('3. Check the complete perimeter and',False),
        ('   every existing part underneath it.',False),
        ('4. Keep both thermal-contact chips clear.',False),
        ('5. Check shield, SD access and case posts.',False),
        ('',False),
        ('Paper geometry is exact; registration is draft.',False),
        ('Black: native carrier edge and openings.',False),
        ('Gray: actual carrier component pads.',False),
        ('Blue dashed: provisional existing hardware.',False),
        ('',False),
        ('Carrier: 59 x 105 mm; PCB 0.8 mm.',False),
        ('Nominal underside: motherboard +2.5 mm.',False),
        ('Nominal top: motherboard +3.3 mm.',False),
        ('Tall existing capacitors need clear openings.',False),
        ('Paper fit does not establish stack height.',False),
        ('',False),
        ('C5 contacts need a supported 0.90 mm gap',False),
        ('and at least 6.24 N total clamp reaction.',False),
        ('Retention and support are not yet designed.',False),
    ]
    for value,bold in lines:
        c.setFont('KUI-Bold' if bold else 'KUI',8)
        c.drawString(textx,texty,value); texty-=4.5*mm
    c.setFont('KUI-Bold',8)
    c.drawString(25*mm, top-115*mm, '50 mm horizontal calibration')
    c.setLineWidth(.25*mm)
    line((0,121),(50,121))
    for x in range(0,51,10): line((x,119.5),(x,122.5))
    c.setFont('KUI',7)
    for x in range(0,51,10): c.drawCentredString(*xy(x,127),str(x))
    line((72,108),(72,158))
    for y in range(108,159,10): line((70.5,y),(73.5,y))
    c.saveState(); c.translate(*xy(78,133)); c.rotate(90)
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
        'board_envelope_mm':[59,105],'calibration_mm':[50,50],
        'motherboard_references':'Provisional registration from owner dimensions; not a measured complete obstacle map',
        'files':['carrier-fit-US-Letter.pdf','carrier-fit-A4.pdf']},indent=2)+'\n')
    print('Created exact-size Letter/A4 fit sheets from native PCB.')

if __name__=='__main__': main()
