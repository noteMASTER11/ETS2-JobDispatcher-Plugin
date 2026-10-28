"""Validate preview bindings against an installed EXE without executing it."""
import argparse, hashlib, pathlib, re, struct

def check(path):
    binary=path.read_bytes()
    identity=hashlib.sha256(binary).hexdigest()
    if identity!='b70c2357cc56eb5834ee47fea3c60b2f2d4591383ff2ac479cfd3cd254301b8c':
        raise ValueError('Unsupported game executable: '+identity)
    pe=struct.unpack_from('<I',binary,60)[0]
    count=struct.unpack_from('<H',binary,pe+6)[0]
    optional=struct.unpack_from('<H',binary,pe+20)[0]
    base=struct.unpack_from('<Q',binary,pe+48)[0]
    sections=[]
    for index in range(count):
        offset=pe+24+optional+index*40
        _,rva,size,file_offset=struct.unpack_from('<IIII',binary,offset+8)
        sections.append((rva,size,file_offset))
    def data(rva,size):
        for start,length,offset in sections:
            if start<=rva and rva+size<=start+length:
                return binary[offset+rva-start:offset+rva-start+size]
        raise ValueError('Unmapped RVA '+hex(rva))
    root=pathlib.Path(__file__).resolve().parents[1]
    constants={name:int(value,16) for name,value in re.findall(r'\b(\w+)=0x([0-9a-f]+)',(root/'src/native_preview_abi.hpp').read_text())}
    entries={0x118:'initialize',0x70:'draw',0x1a0:'assign_vehicle',0x208:'select_camera'}
    for slot,name in entries.items():
        pointer=struct.unpack('<Q',data(constants['widget_vtable']+slot,8))[0]
        if pointer!=base+constants[name]: raise ValueError('Widget vtable mismatch: '+name)
    source=(root/'src/native_preview.cpp').read_text()
    prefixes=re.findall(r'Read<uint64_t>\(base\+abi::(\w+)\)==0x([0-9a-f]+)ULL',source)
    if len(prefixes)!=2: raise ValueError('Expected two production prefix guards')
    for name,hexadecimal in prefixes:
        if int.from_bytes(data(constants[name],8),'little')!=int(hexadecimal,16):
            raise ValueError('Function prefix mismatch: '+name)
    if struct.unpack('<Q',data(constants['window_vtable']+0x60,8))[0]!=base+0x379120:
        raise ValueError('Private window initialization mismatch')
    # The crash of 2026-10-28 exposed confusion between the string receiver
    # and its char* member. Verify both against the real native constructor.
    constructor=data(0xff2c4f,14)
    if constructor[:3]!=bytes.fromhex('488d05') or constructor[7:10]!=bytes.fromhex('488987'):
        raise ValueError('String constructor instructions changed')
    vtable_rva=0xff2c4f+7+struct.unpack_from('<i',constructor,3)[0]
    object_offset=struct.unpack_from('<I',constructor,10)[0]
    if vtable_rva!=constants['string_vtable'] or object_offset!=constants['config_path_string']:
        raise ValueError('Native string receiver layout mismatch')
    if data(0xff2c48,7)!=bytes.fromhex('488987')+struct.pack('<I',object_offset+8):
        raise ValueError('Native string buffer-member layout mismatch')
    print('Executable SHA-256, widget/window vtables, native string layout and production prefix guards match.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-exe',required=True,type=pathlib.Path)
    check(parser.parse_args().game_exe)
