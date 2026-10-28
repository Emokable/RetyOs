"""Small read-only ELF32 inspector for local candidate validation."""
import struct


def inspect(data):
    if not data.startswith(b'\x7fELF'):
        return None
    if data[4:7] != b'\x01\x01\x01':
        raise ValueError('expected ELF32 little endian')
    machine = struct.unpack_from('<H', data, 18)[0]
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    headers = [struct.unpack_from('<8I', data, offset+i*size) for i in range(count)]
    dynamic = next((h for h in headers if h[0] == 2), None)
    result = {'machine': machine, 'needed': [], 'interpreter': None,
              'rwx_load': any(h[0] == 1 and h[6] & 7 == 7 for h in headers)}
    interp = next((h for h in headers if h[0] == 3), None)
    if interp:
        result['interpreter'] = data[interp[1]:interp[1]+interp[4]].rstrip(b'\0').decode()
    if dynamic:
        entries = [struct.unpack_from('<II', data, i)
                   for i in range(dynamic[1], dynamic[1]+dynamic[4], 8)]
        address = next(v for tag, v in entries if tag == 5)
        segment = next(h for h in headers if h[0] == 1 and h[2] <= address < h[2]+h[4])
        base = segment[1]+address-segment[2]
        string = lambda value: data[base+value:data.index(b'\0',base+value)].decode()
        result['needed'] = [string(v) for tag,v in entries if tag == 1]
        result['soname'] = next((string(v) for tag,v in entries if tag == 14), None)
    return result
