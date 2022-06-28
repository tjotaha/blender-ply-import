#!/usr/bin/env python
# Blender 2.8x version
#
# blender -P mesh_readply.py -- file.ply
#
import sys, os, time
# import bpy, bmesh

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
        print('rgb: %f, %f, %f' % (p['vertex_colors'][i*3] * 255, p['vertex_colors'][i*3+1] * 255, p['vertex_colors'][i*3+2] * 255))
        print('density, temperature, pressure: %d, %d, %d' % (p['density'][i], p['temperature'][i], p['pressure'][i]))
        print("\n")

print('Using readply module: %s' % readply.__file__)

# # Option parsing
#
# args = []
# idx = sys.argv.index('--')
# if idx != -1:
#     args = sys.argv[idx+1:]

fname = 'small_wavelet_1_extra_properties.ply'
# if len(args) > 0:
#     fname = args[0]

print("file %s exists: %r\n" % (fname, os.path.exists(fname)))

# Start parsing the PLY file

t0 = time.time()

p = readply.readply(fname)
# print(p.keys())

t1 = time.time()
print(p.keys())
#print('PLY file read by readply() in %.3fs' % (t1-t0))
print('%d vertices, %d faces' % (p['num_vertices'], p['loop_start'].shape[0]))
print('vertices: {}\n'.format(p['vertices']))
print('vertex_colors.shape: {0}\n'.format(p['vertex_colors'].shape))

print('density: {}\n'.format(p['density']))

print_vert_attr(p)





