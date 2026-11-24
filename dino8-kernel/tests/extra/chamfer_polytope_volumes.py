#!/usr/bin/env python3
"""Independent oracle for tests/extra/chamfer_all_edges_corners.cpp (ChamferAllEdgesOfTetrahedraAndPrism).

The solid left by chamfering EVERY edge of a convex polytope with trihedral vertices by one setback d on both faces is the intersection of half-spaces:
the original faces, one plane per edge (through the two rails, the lines parallel to the edge at distance d inside each of its faces) and one plane per
vertex (through the three face corners where the rails of the vertex's three edges cross). Its volume is computed here by the divergence theorem over the
polygons those planes cut out, with no use of the kernel. Run: python3 -I tests/extra/chamfer_polytope_volumes.py
"""
import itertools, math
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(dot(a,a))
def unit(a): n=norm(a); return mul(a,1/n)

def hull_faces(verts):
    # faces of a convex polytope given vertices: planes through vertex triples with all vertices on one side; merged
    planes=[]
    for i,j,k in itertools.combinations(range(len(verts)),3):
        n=cross(sub(verts[j],verts[i]),sub(verts[k],verts[i]))
        if norm(n)<1e-12: continue
        n=unit(n); off=dot(n,verts[i])
        s=[dot(n,v)-off for v in verts]
        if all(x<=1e-9 for x in s): pass
        elif all(x>=-1e-9 for x in s): n=mul(n,-1); off=-off
        else: continue
        if not any(norm(sub(n,p[0]))<1e-9 and abs(off-p[1])<1e-9 for p in planes): planes.append((n,off))
    return planes

def edges_of(verts, planes):
    # edge = pair of vertices lying on two common planes
    on = {v:[pi for pi,(n,o) in enumerate(planes) if abs(dot(n,verts[v])-o)<1e-9] for v in range(len(verts))}
    es=[]
    for a,b in itertools.combinations(range(len(verts)),2):
        common=set(on[a])&set(on[b])
        if len(common)==2: es.append((a,b,tuple(sorted(common))))
    return es

def volume_of_halfspaces(planes):
    verts=[]
    for (n1,o1),(n2,o2),(n3,o3) in itertools.combinations(planes,3):
        # solve n x = o
        det=dot(n1,cross(n2,n3))
        if abs(det)<1e-12: continue
        x=mul(add(add(mul(cross(n2,n3),o1),mul(cross(n3,n1),o2)),mul(cross(n1,n2),o3)),1/det)
        if all(dot(n,x)<=o+1e-9 for n,o in planes) and not any(norm(sub(x,v))<1e-9 for v in verts): verts.append(x)
    c=mul(tuple(map(sum,zip(*verts))),1/len(verts))
    vol=0
    for n,o in planes:
        pts=[v for v in verts if abs(dot(n,v)-o)<1e-9]
        if len(pts)<3: continue
        fc=mul(tuple(map(sum,zip(*pts))),1/len(pts))
        u=unit(sub(pts[0],fc)); w=cross(n,u)
        pts.sort(key=lambda p: math.atan2(dot(sub(p,fc),w),dot(sub(p,fc),u)))
        area=0
        for i in range(len(pts)):
            area+=0.5*norm(cross(sub(pts[i],fc),sub(pts[(i+1)%len(pts)],fc)))
        h=o-dot(n,c)
        vol+=area*h/3
    return vol

def chamfer_planes(verts, d):
    planes=hull_faces(verts)
    es=edges_of(verts,planes)
    out=list(planes)
    # per face, adjacency
    rail_pts={}  # (face, vertex) -> corner point after insets: computed below via plane intersection of two rails
    # edge planes
    edge_planes=[]
    for a,b,(fi,gi) in es:
        nF,oF=planes[fi]; nG,oG=planes[gi]
        e=unit(sub(verts[b],verts[a]))
        # in-face inward directions perpendicular to the edge: mF = -(component of nF x e) oriented toward the face interior
        def inward(n):
            m=unit(cross(n,e))
            # pick sign pointing to the polytope centroid projected
            cen=mul(tuple(map(sum,zip(*verts))),1/len(verts))
            if dot(m,sub(cen,verts[a]))<0: m=mul(m,-1)
            return m
        mF=inward(nF); mG=inward(nG)
        p=verts[a]
        pF=add(p,mul(mF,d)); pG=add(p,mul(mG,d))
        nc=unit(cross(sub(pF,pG),e))
        if dot(nc,add(nF,nG))<0: nc=mul(nc,-1)
        o=dot(nc,pF)
        edge_planes.append((nc,o)); out.append((nc,o))
    # vertex planes: through the three face-corner points of each vertex = intersection of the inset rails on each face
    for vi,v in enumerate(verts):
        fs=[pi for pi,(n,o) in enumerate(planes) if abs(dot(n,v)-o)<1e-9]
        if len(fs)!=3: continue
        pts=[]
        for f in fs:
            # edges at v lying in face f: two edge planes
            eps=[ep for ep,(a,b,(fi,gi)) in zip(edge_planes,es) if (a==vi or b==vi) and f in (fi,gi)]
            assert len(eps)==2
            (n1,o1),(n2,o2)=eps; nf,of=planes[f]
            det=dot(n1,cross(n2,nf)); x=mul(add(add(mul(cross(n2,nf),o1),mul(cross(nf,n1),o2)),mul(cross(n1,n2),of)),1/det)
            pts.append(x)
        nc=unit(cross(sub(pts[1],pts[0]),sub(pts[2],pts[0])))
        cen=mul(tuple(map(sum,zip(*verts))),1/len(verts))
        if dot(nc,sub(v,cen))<0: nc=mul(nc,-1)
        out.append((nc,dot(nc,pts[0])))
    return out
if __name__=='__main__':
    shapes={'cube2':[(x,y,z) for x in (0,2) for y in (0,2) for z in (0,2)],
     'corner tetra':[(0,0,0),(3,0,0),(0,3,0),(0,0,3)],
     'regular tetra':[(1,1,1),(1,-1,-1),(-1,1,-1),(-1,-1,1)],
     'triangular prism':[(0,0,0),(4,0,0),(0,3,0),(0,0,2),(4,0,2),(0,3,2)]}
    for name,d in [('cube2',0.25),('cube2',0.5),('corner tetra',0.1),('corner tetra',0.3),('regular tetra',0.1),('regular tetra',0.3),('triangular prism',0.2),('triangular prism',0.5)]:
        v=[tuple(map(float,x)) for x in shapes[name]]
        print(name,d,'%.12f'%volume_of_halfspaces(chamfer_planes(v,d)))
