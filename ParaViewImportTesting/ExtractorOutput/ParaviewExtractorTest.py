# script-version: 2.0
# Catalyst state generated using paraview version 5.10.1

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'XML PolyData Reader'
wavelet_for_importvtp = XMLPolyDataReader(registrationName='wavelet_for_import.vtp', FileName=['C:\\Users\\vjvalve\\Documents\\09326\\ParaViewImportTesting\\wavelet_for_import.vtp'])
wavelet_for_importvtp.PointArrayStatus = ['RTData']
wavelet_for_importvtp.TimeArray = 'None'

# create a new 'Extract Surface'
extactSurface = ExtractSurface(registrationName='ExtactSurface', Input=wavelet_for_importvtp)

# create a new 'Triangulate'
triangulate1 = Triangulate(registrationName='Triangulate1', Input=extactSurface)

# create a new 'Generate Global Ids'
generateGlobalIds1 = GenerateGlobalIds(registrationName='GenerateGlobalIds1', Input=triangulate1)

# ----------------------------------------------------------------
# setup extractors
# ----------------------------------------------------------------

# create extractor
pLY1 = CreateExtractor('PLY', generateGlobalIds1, registrationName='PLY1')
# trace defaults for the extractor.
pLY1.Trigger = 'TimeStep'

# init the 'PLY' selected for 'Writer'
pLY1.Writer.FileName = 'ply_extractor.ply'
pLY1.Writer.Input = generateGlobalIds1
pLY1.Writer.FileType = 'Ascii'

# create extractor
cSV1 = CreateExtractor('CSV', generateGlobalIds1, registrationName='CSV1')
# trace defaults for the extractor.
cSV1.Trigger = 'TimeStep'

# init the 'CSV' selected for 'Writer'
cSV1.Writer.FileName = 'csv_extractor.csv'

# ----------------------------------------------------------------
# restore active source
SetActiveSource(cSV1)
# ----------------------------------------------------------------

# ------------------------------------------------------------------------------
# Catalyst options
from paraview import catalyst
options = catalyst.Options()
options.GlobalTrigger = 'TimeStep'
options.CatalystLiveTrigger = 'TimeStep'

# ------------------------------------------------------------------------------
if __name__ == '__main__':
    from paraview.simple import SaveExtractsUsingCatalystOptions
    # Code for non in-situ environments; if executing in post-processing
    # i.e. non-Catalyst mode, let's generate extracts using Catalyst options
    SaveExtractsUsingCatalystOptions(options)
