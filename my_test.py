#!/usr/bin/env python
# Blender 2.8x version
#
# blender -P mesh_readply.py -- file.ply
#
import sys, os, time

try:
    import bpy, bmesh
    in_blender = True
except ImportError:
    print("Script being called outside of Blender")
    in_blender = False


sys.path.insert(0, '.')
try:
    import readply
except ImportError:
    scriptdir = os.path.split(os.path.abspath(__file__))[0]    
    sys.path.insert(0, scriptdir)
    import readply


def print_vert_attr(p):
    for i in range(p['num_vertices']):
        print("vertex %d" % i)
        print('vertex coord: %d, %d, %d' % (p['vertices'][i*3], p['vertices'][i*3+1], p['vertices'][i*3+2]))
        print('rgb: %d, %d, %d' % (p['vertex_colors'][i*3] * 255, p['vertex_colors'][i*3+1] * 255, p['vertex_colors'][i*3+2] * 255))
        print('density, temperature, pressure: %.1f, %.1f, %.1f' % (p['density'][i], p['temperature'][i], p['pressure'][i]))
        print("\n")

print('Using readply module: %s' % readply.__file__)

# # Option parsing
#
args = []
try:
    idx = sys.argv.index('--')
    args = sys.argv[idx+1:]
except ValueError as e:
    args = []

#print("current directory: %r\n" % os.getcwd())
fname = 'small_wavelet_1_extra_properties.ply'
path = 'C:\\Users\\vjvalve\\Documents\\blender-ply-import\\'
fpath = path+fname

if len(args) > 0:
     fname = args[0]

print("file %s exists: %r\n" % (fname, os.path.exists(fpath)))

# Start parsing the PLY file

t0 = time.time()

p = readply.readply(fpath)
# print(p.keys())

t1 = time.time()
print(p.keys())
#print('PLY file read by readply() in %.3fs' % (t1-t0))
print('%d vertices, %d faces' % (p['num_vertices'], p['loop_start'].shape[0]))
print('vertices: {}\n'.format(p['vertices']))
# print('vertex_colors.shape: {0}\n'.format(p['vertex_colors'].shape))

# print('density: {}\n'.format(p['density']))

print_vert_attr(p)

if in_blender:
    # Create a mesh + object using the vertex and face data in the numpy arrays

    mesh = bpy.data.meshes.new(name='imported mesh')

    mesh.vertices.add(p['num_vertices'])
    mesh.vertices.foreach_set('co', p['vertices'])

    mesh.loops.add(len(p['faces']))
    mesh.loops.foreach_set('vertex_index', p['faces'])

    mesh.polygons.add(p['num_faces'])
    mesh.polygons.foreach_set('loop_start', p['loop_start'])
    mesh.polygons.foreach_set('loop_total', p['loop_length'])

    if 'vertex_normals' in p:
        mesh.vertices.foreach_set('normal', p['vertex_normals'])

    if 'vertex_colors' in p:
        vcol_layer = mesh.vertex_colors.new()
        vcol_data = vcol_layer.data
        vcol_data.foreach_set('color', p['vertex_colors'])

    if 'texture_coordinates' in p:
        uv_layer = mesh.uv_layers.new(name='default')
        uv_layer.data.foreach_set('uv', p['texture_coordinates'])

    mesh.validate()
    mesh.update()

    # Create object to link to mesh

    obj = bpy.data.objects.new('imported object', mesh)

    # Add object to the scene
    scene = bpy.context.scene
    scene.collection.children[0].objects.link(obj)

    # Select the new object and make it active
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    t2 = time.time()
    print('Blender object+mesh created in %.3fs' % (t2 - t1))

    del p

    print('Total import time %.3fs' % (t2 - t0))



