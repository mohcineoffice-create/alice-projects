#!/usr/bin/env python3
"""Labo 3D XLab IC : compile un .scad, controle le maillage, genere des vues PNG.
Usage: labo3d.py modele.scad part1 [part2 ...] [--expect part=LxWxH ...]"""
import sys, subprocess, os, json, re
import numpy as np, trimesh
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

def build(scad, part, out):
    r = subprocess.run(["openscad","-o",out,"-D",f'part="{part}"',scad],capture_output=True,text=True)
    if r.returncode: raise SystemExit(r.stderr)

def check(path, expect=None):
    m = trimesh.load(path); b = m.bounds; d = b[1]-b[0]
    res = {"fichier":os.path.basename(path),"dimensions_mm":[round(float(x),2) for x in d],
           "etanche":bool(m.is_watertight),"volume_cm3":round(float(m.volume)/1000,2),
           "morceaux":len(m.split(only_watertight=False))}
    if expect:
        res["attendu_mm"]=expect; res["dimensions_ok"]=bool(np.allclose(d,expect,atol=0.2))
    return res

def render(scad, png, title, part="ensemble"):
    """Rendu OpenSCAD (OpenGL virtuel) sous 4 angles, assemble en une image."""
    from PIL import Image, ImageDraw
    views=[("Perspective","55,0,25","persp"),("Dessus","0,0,0","ortho"),("Face","90,0,0","ortho"),("Cote","90,0,90","ortho")]
    tiles=[]
    for n,rot,pr in views:
        t=f"/tmp/_v_{n}.png"
        subprocess.run(["xvfb-run","-a","openscad","-o",t,"--imgsize=700,520","--viewall","--autocenter",
            f"--camera=0,0,0,{rot},300","--projection="+("p" if pr=="persp" else "o"),"--colorscheme=Tomorrow",
            "-D",f'part="{part}"',scad],capture_output=True)
        im=Image.open(t).convert("RGB"); ImageDraw.Draw(im).text((10,8),n,fill=(0,0,0)); tiles.append(im)
    W,H=tiles[0].size; out=Image.new("RGB",(W*2,H*2),"white")
    for k,im in enumerate(tiles): out.paste(im,((k%2)*W,(k//2)*H))
    out.save(png)

if __name__=="__main__":
    a=sys.argv[1:]; scad=a[0]; exp={}
    parts=[x for x in a[1:] if "=" not in x]
    for x in a[1:]:
        if "=" in x: k,v=x.split("=");exp[k]=[float(t) for t in v.split("x")]
    base=os.path.splitext(os.path.basename(scad))[0]; out=os.path.dirname(scad)
    paths=[];rep=[]
    for p in parts:
        f=f"{out}/{base.split('_')[0]}_{p}.stl"; build(scad,p,f); paths.append(f); rep.append(check(f,exp.get(p)))
    render(scad,f"{out}/{base}_vues.png",base)
    print(json.dumps(rep,indent=1,ensure_ascii=False))
