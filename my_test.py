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
#print('%d vertices, %d faces' % (p['num_vertices'], p['loop_start'].shape[0]))

