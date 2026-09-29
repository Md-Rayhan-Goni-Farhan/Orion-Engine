def bin_to_cpp(filepath, array_name):
    with open(filepath, 'rb') as f:
        data = f.read()
    lines = ['static const uint8_t {}[] =\n{{'.format(array_name)]
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_str = ', '.join('0x{:02x}'.format(b) for b in chunk)
        lines.append('    ' + hex_str + ',')
    lines.append('};\n')
    return '\n'.join(lines)
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\imgui\vs_imgui.bin', 's_vs_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\imgui\fs_imgui.bin', 's_fs_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\mesh\vs_mesh.bin', 's_vs_mesh_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\mesh\fs_mesh.bin', 's_fs_mesh_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\mesh\vs_mesh_skinned.bin', 's_vs_mesh_skinned_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\grid\vs_grid.bin', 's_vs_grid_dx11'))
print(bin_to_cpp(r'E:\GameEngine\assets\shaders\grid\fs_grid.bin', 's_fs_grid_dx11'))