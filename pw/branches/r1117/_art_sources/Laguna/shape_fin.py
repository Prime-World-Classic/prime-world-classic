# Плавник акулы из спинного плавника дельфина: профиль (Y-Z) перестраивается по аналитическому контуру,
# толщина (X) и UV берутся от исходной вершины.
import bpy, bmesh, json, math, sys
from mathutils import Vector
a=sys.argv[sys.argv.index('--')+1:]; OUT=a[0]
HK=float(a[1]) if len(a)>1 else 0.95   # высота / длина основания
OVER=float(a[2]) if len(a)>2 else 0.06 # нависание кончика назад (доля основания)
src=bpy.data.objects['_']; me=src.data
bm=bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
seed=max((v for v in bm.verts if abs(v.co.x)<3.2 and 15.9<v.co.y<37.7), key=lambda v:v.co.z)
comp={seed.index}; st=[seed]
while st:
    x=st.pop()
    for e in x.link_edges:
        y=e.other_vert(x)
        if y.index not in comp: comp.add(y.index); st.append(y)
bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.index not in comp],context='VERTS')
fm=bpy.data.meshes.new('SharkFin'); bm.to_mesh(fm); bm.free()
for m in me.materials: fm.materials.append(m)
fin=bpy.data.objects.new('SharkFin',fm); bpy.context.scene.collection.objects.link(fin)
vs=fm.vertices
Z0=-135.5; ZT=max(v.co.z for v in vs); H0=ZT-Z0
# исходный профиль: края по Y в слоях по высоте
NB=24; lo=[1e9]*NB; hi=[-1e9]*NB
def tb(z): return min(NB-1,max(0,int((z-Z0)/H0*NB)))
for v in vs:
    b=tb(v.co.z); lo[b]=min(lo[b],v.co.y); hi[b]=max(hi[b],v.co.y)
import numpy as np
P=np.array([(v.co.y,v.co.z) for v in vs])
NS=48; win=H0/NS*2.0
tg=np.linspace(0,1,NS); LO=[];HI=[]
for t in tg:
    zc=Z0+t*H0; m=np.abs(P[:,1]-zc)<win
    if t<0.02: m=np.abs(P[:,1]-zc)<win  # у основания
    LO.append(P[m,0].min() if m.any() else np.nan); HI.append(P[m,0].max() if m.any() else np.nan)
LO=np.array(LO); HI=np.array(HI)
ok=~np.isnan(LO); LO=np.interp(tg,tg[ok],LO[ok]); HI=np.interp(tg,tg[ok],HI[ok])
k=np.ones(5)/5
def sm(a): return np.convolve(np.pad(a,2,mode='edge'),k,mode='valid')
LO=sm(sm(LO)); HI=sm(sm(HI))
def edges0(t):
    t=min(max(t,0.0),1.0); return float(np.interp(t,tg,LO)), float(np.interp(t,tg,HI))
lo0,hi0=edges0(0.0); lo=[lo0]; hi=[hi0]
B=hi[0]-lo[0]; Yfront=lo[0]; Yback=hi[0]   # голова к -Y, хвост к +Y
H=HK*B
# целевой контур, s: 0 = задний край основания, 1 = передний; t: 0 основание, 1 кончик
def target(t):
    tt=min(t,1.0)
    back=0.04*(1-tt) - OVER*tt**1.5 + 0.07*math.sin(math.pi*tt)*(1-tt)   # почти вертикальный, слегка вогнутый
    front=-OVER + (1+OVER)*(1-tt)**0.55                                    # выпуклая передняя кромка
    front=max(front, back+0.035)                                         # кончик острый, но не нулевой
    return back, front
def s2y(s): return Yback - s*B
for v in vs:
    z=v.co.z; t=max(0.0,(z-Z0)/H0)
    l,h=edges0(t)
    u=(h-v.co.y)/(h-l) if h-l>0.3 else 0.5
    u=min(1.03,max(-0.03,u))   # 0 у заднего края, 1 у переднего
    bk,fr=target(t)
    v.co.y=s2y(bk+u*(fr-bk))
    if z>Z0: v.co.z=Z0+t*H
fm.update()
ymid=(Yfront+Yback)/2
for v in vs: v.co-=Vector((0,ymid,Z0))
SC=20.0/H   # высота над основанием ~20 ед. (0.8 м при масштабе 0.04, как у дельфина)
for v in vs: v.co=Vector((v.co.x*1.2, v.co.y*SC, v.co.z*SC))
fm.update()
for o in bpy.data.objects:
    if o is not fin: o.hide_render=True; o.hide_set(True)
sc=bpy.context.scene; sc.render.resolution_x=1000; sc.render.resolution_y=640
w=sc.world or bpy.data.worlds.new('W'); sc.world=w
if w.use_nodes: w.node_tree.nodes['Background'].inputs[0].default_value=(0.55,0.72,0.85,1)
cam=bpy.data.objects.new('Cam',bpy.data.cameras.new('Cam')); sc.collection.objects.link(cam); sc.camera=cam
cam.data.type='ORTHO'; cam.data.ortho_scale=48
sun=bpy.data.objects.new('Sun',bpy.data.lights.new('Sun','SUN')); sc.collection.objects.link(sun); sun.data.energy=3; sun.rotation_euler=(math.radians(50),0,math.radians(-40))
tgt=bpy.data.objects.new('T',None); sc.collection.objects.link(tgt); tgt.location=(0,0,8)
c=cam.constraints.new('TRACK_TO'); c.target=tgt; c.track_axis='TRACK_NEGATIVE_Z'; c.up_axis='UP_Y'
# вид сбоку: голова (-Y) справа, как на референсе
for name,loc in [('side',(-80,0,9)),('persp',(-45,-35,20))]:
    cam.location=loc; sc.render.filepath=OUT+'_'+name+'.png'; bpy.ops.render.render(write_still=True)
for o in [cam,sun,tgt]: bpy.data.objects.remove(o)
bpy.ops.wm.save_as_mainfile(filepath=OUT+'.blend')
print("RESULT",json.dumps({'B':B,'H':H,'bb':[[round(min(v.co[i] for v in vs),2) for i in range(3)],[round(max(v.co[i] for v in vs),2) for i in range(3)]]}))
