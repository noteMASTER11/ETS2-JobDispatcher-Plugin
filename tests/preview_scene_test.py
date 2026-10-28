"""Loaded trailer attachment and special/area loading, without running ETS2."""
import importlib.util, pathlib, sys, math, array, struct
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('scene',root/'tools/preview-scene.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
body=('/vehicle/trailer/body/platform.pmd','default');chassis=('/vehicle/trailer/chassis/base.pmd','default')
cargo=('/vehicle/cargo/excavator.pmd','default','red')
metadata={'bodies':{'trailer.lowbed':{body[0]:{'methods':['custom','area'],'custom':'/def/loads'}}},
          'cargo':{'cargo.excavator':[{'component':list(cargo),'method':'custom','dimensions':[2,3,4]}]},
          'custom':{'/def/loads':{'cargo.excavator':{'offset':(0,0,1),'rotation':(0,180,0)}}}}
load=s.choose_load(metadata,'trailer.lowbed','cargo.excavator');assert load
points={body:{'cargo':((0,0,-2),(1,0,0,0),(1,1,1))}}
poses={body:((0,1.2,.3),(1,0,0,0),(1,1,1))}
instances=s.cargo_instances(load,[body,chassis],points,poses,1)
assert len(instances)==1 and instances[0][0]==cargo
point=s.transform(instances[0][1],(0,0,1))
assert all(abs(a-b)<1e-6 for a,b in zip(point,(0,1.2,-1.7))),point
normal=s.transform(instances[0][1],(0,0,1),True)
assert abs(normal[2]+1)<1e-6 and abs(sum(v*v for v in normal)-1)<1e-6
# Area packing uses native load-area corners and requested quantity.
area=(body[0],{'component':list(cargo),'method':'area','dimensions':(1,1,2)},None)
points={body:{'larea_s_0':((-1,0,-3),(1,0,0,0),(1,1,1)),
              'larea_e_0':((1,0,3),(1,0,0,0),(1,1,1))}}
instances=s.cargo_instances(area,[body,chassis],points,poses,4)
assert len(instances)==4
for component,pose in instances:
 x,y,z=pose[0];assert -.5<=x<=.5 and abs(y-1.2)<1e-6 and -2.7<=z<=3.3
assert s.choose_load(metadata,'trailer.lowbed','cargo.closed_tank') is None
assert s.cargo_instances(None,[body],{},poses,100)==[]
# Verify variant-controlled locator visibility and chassis->body attachment.
fixture=pathlib.Path(sys.argv[1]);fixture.mkdir(parents=True,exist_ok=True)
pim=fixture/'chassis.pim';pim.write_text('''Part {
Name: "bodypart"
Locators: 0
}
Locator {
Name: "body"
Index: 0
Position: (0 1.2 .3)
Rotation: (1 0 0 0)
Scale: (1 1 1)
}''')
pim.with_suffix('.pit').write_text('Variant {\nName: "default"\nPart {\nName: "bodypart"\nAttribute { Value: (1) } } }')
body_pim=fixture/'body.pim';body_pim.write_text('Locator {\nName: "cargo"\nIndex: 0\nPosition: (0 0 -2)\n}')
body_pim.with_suffix('.pit').write_text('')
points,poses=s.trailer_poses([body,chassis],{body[0]:str(body_pim),chassis[0]:str(pim)})
assert poses[body][0]==(0,1.2,.3) and poses[chassis]==s.IDENTITY
# Exercise the real mesh assembler: trailer and cargo vertices share one mesh,
# their native attachments affect positions, normals rotate but do not translate.
spec=importlib.util.spec_from_file_location('builder',root/'tools/prepare-textured-previews.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
part=fixture/'triangle.part'
vertices=array.array('f',[0,0,0, 0,0,1, 1,1,1, 0,0,
                         1,0,0, 0,0,1, 1,1,1, 1,0,
                         0,1,0, 0,0,1, 1,1,1, 0,1])
part.write_bytes(struct.pack('<4sIIIHIII',b'JDT1',3,1,1,0,0,3,0)+vertices.tobytes())
b.native_pack=lambda data:data
cargo_pose=((0,1.2,-2),(0,0,1,0),(1,1,1))
result=b.assemble(('trailer.test|cargo.test|1',[body],{body:str(part),cargo:str(part)}, {},str(fixture),('wheel','default'),
                   [(body,poses[body]),(cargo,cargo_pose)],poses))
textures,groups,data=b.read_part(fixture/result[1])
assert len(data)==6*11 and len(groups)==2
assert all(abs(data[i]-v)<1e-6 for i,v in enumerate((0,1.2,.3)))
assert all(abs(data[3*11+i]-v)<1e-6 for i,v in enumerate((0,1.2,-2)))
assert abs(data[3*11+5]+1)<1e-6
print('Loaded trailer body attachment, custom offset/rotation, area placement and closed-body cases passed')
