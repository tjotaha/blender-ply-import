from asyncio.windows_events import NULL
from pathlib import Path
import numpy as np
import glob
#### import the simple module from the paraview
from paraview.simple import *
from paraview.util import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# outputName = "wavelet_test"
outputCSVPD = '_pointdata.csv'
outputCSVCD = '_celldata.csv'
# outputPath = 'C:/Users/vjvalve/Documents/09326/ParaViewImportTesting/ExtractorOutput'

# LIST OF FILTERS NEEDED TO EXPORT .PLY FILE WITH VERTEX ATTRIBUTES TO BLENDER
# validFilters = ["merge_blocks", "extract_surface", "triangulate", "cell_data_to_point_data", "cd_to_pd"]

# DEFAULT WILL APPLY ALL NECESSARY FILTERS.
# REFLECT APPLYS ALL NECESSARY FILTERS WITH THE ADDITION OF THE REFLECTION FILTER
validFilters = ["default", "reflect"]

# COMMAND LINE FLAG STORAGE
mergingFlag = False
exportFlag = False
timeStepFlag = False
objFilters = []
stitchParams = []

timeStepRange = []

# OPENS CSV FILE AND RETURNS ARRAY WITH PER VERTEX INFORMATION FOR EACH PROPERTY DEFINED IN TOP LINE OF CSV
def getCSVData(path: str):
    """ Opens .csv file and returns array with per vertex information for each property defined on top line of .csv

    Requires the csv file path to be passed in

    Parameters
    ----------
    path: str, required
        The .csv file path
    """

    print("in getCSVData")

    with open(path) as f:
        # Get rid of unnecessary white space
        contents = [line.strip() for line in f.readlines()]
        # Convert array to numpy array
        dataLines = np.array(contents)
        # Get properties as defined in first line
        properties = np.array(dataLines[0].split(','))
        # Data lines are all values after the first line
        dataLines = dataLines[1:]
        # Convert string lines to numpy arrays per value ([row, column] format)
        dataLines = np.array([np.array(data.split(',')) for data in dataLines])
        # dataLines = dataLines.astype(np.float)
        print(type(properties[0]))
        propMap = {}

        # DEBUGGING
        # print(properties)
        # print(dataLines)
        # print(propMap)

        # FOR EACH PROPERTY STORE ITS ARRAY VALUES (COLUMN) INTO A [STR, INT] DICTIONARY
        for i in range(properties.size):
            propMap.update({str(properties[i]): dataLines[:, i]})

        # DEBUGGING
        # print(properties)
        # print(dataLines)
        # print(propMap)


        # print(lines)
    # RETURN DICTIONARY
    return propMap

# GET NUMBER OF VERTICES IN .PLY FILE USING .PLY FORMAT HEADERS
def findVertNum(plyLines):
    """ Get the number of vertices in .ply file

    .ply file should already be split into array by lines

    Parameters
    ----------
    plyLines: [str]
        .ply file lines converted into numpy string array
    
    """

    # Keep track of what line we are of file
    elementVertexLineIndex = 0

    # Search for element vertex header which contains vertex count data
    while "element vertex" not in plyLines[elementVertexLineIndex]:
        elementVertexLineIndex += 1

    # Using the vertex index stored get the last element in the array after a space split which contains the vertex count
    lineTokens = plyLines[elementVertexLineIndex].split(" ")
    vertNum = int(lineTokens[-1])

    # DEBUGGING
    print(vertNum)

    return vertNum

# 
def findHeaderEnd(plyLines):
    """Find line index where the .ply file header ends
    
    .ply file should already be split into array by lines

    Parameters
    ----------
    plyLines: [str]
        .ply file lines converted into numpy string array

    """
    headerEnd = 0
    while "end_header" not in plyLines[headerEnd]:
        headerEnd += 1
    return headerEnd

def findLastPropIndex(plyLines):
    """In the header find the line index that has the last property for the .ply
    
    .ply file should already be split into array by lines

    Parameters
    ----------
    plyLines: [str]
        .ply file lines converted into numpy string array
    """
    flag = False
    lastPropIndex = 0
    while "end_header" not in plyLines[lastPropIndex]:
        if "property" in plyLines[lastPropIndex] and not flag:
            flag = True

        elif "property" not in plyLines[lastPropIndex] and flag:
            # print("lastPropIndex: ", lastPropIndex)
            return lastPropIndex - 1
        print(plyLines[lastPropIndex])
        lastPropIndex += 1
            # continue
    return -1
        
def addVertexPropsToLine(plyLine, propLine):
    """Append property values of vertex to vertex line data
    
    Parameters
    ----------
    plyLine: str
        ply file line

    propLine: [str]
        line of properties to be added on to the vertex line data
    
    """

    newStrArray = plyLine.strip().split(" ")
    newStrArray.extend(propLine)
    returnLine = " ".join(newStrArray)
    returnLine = returnLine + "\n"
    # print(returnLine)
    return returnLine

def addVertexPropertiesToPly(plyFileName, propMap):
    """Using property map collected and original .ply file to create a new .ply file with all the appended properties

    Parameters
    ----------
    plyFileName: str
        original .ply file name and path

    propMap: {str, [str]}
        Dictionary containing all property values per vertex
    
    """

    # Read in the .ply file
    ff = open(plyFileName, "r")
    plyFLines = ff.readlines()
    ff.close()

    # Get number of vertices
    vertNum = findVertNum(plyFLines)

    # Find line index of where vertex data starts
    firstVertexDataLine = findHeaderEnd(plyFLines) + 1

    # Add property values to each line
    i = 0
    for ii in range(firstVertexDataLine, firstVertexDataLine + vertNum):
        # Using list comprehension get all the property values for a vertex into a single line
        linePropVals = np.array([v[i] for k, v in propMap.items()])
        # print(linePropVals)

        # Create a new vertex data line with the new property values
        plyFLines[ii] = addVertexPropsToLine(plyFLines[ii], linePropVals)

        # if i > 5: return
        i += 1

    lastPropLineIndex = findLastPropIndex(plyFLines)

    # propsString = ["property float " + k for k, v in propMap.items()]

    # Create new .ply file header
    propsString = '\n'.join(["property float " + str(prop).replace('\"', '') for prop in propMap.keys()])
    propsString += '\n'
    print(propsString)

    plyFLines[lastPropLineIndex] += propsString
    ff = open( plyFileName.replace(".ply", "") + "_with_added_properties.ply", "w")

    # Write out new .ply file
    for oneLine in plyFLines:
      ff.write(oneLine)
    ff.close()

def GetPlyData(dirPath, fileName):
    """Export out the paraview file out into it's .ply file and the additonal property data as .csv files
    
    Parameters
    ----------
    dirPath: str
        Path where the file's located
    fileName: str
        name of the paraview file

    """

    # trace generated using paraview version 5.10.1
    #import paraview
    #paraview.compatibility.major = 5
    #paraview.compatibility.minor = 10

    # Create a new "XML PolyData Reader"
    # filePathEdit = filePath.replace('\\', "\\\\")
    # filePathEdit += "\\\\"+fileName
    # print(filePathEdit)
    # return

    print(type(dirPath))
    print(type(fileName))

    filePath = dirPath/fileName

    print("filePathWhole: {}".format(filePath))

    # Import all file blocks and store in array
    paraViewFiles = glob.glob(str(filePath)+"*", recursive=True)
    for paraviewFile in paraViewFiles:
        print(paraviewFile)
        print("fileName: {}".format(Path(paraviewFile).name))

    # fileextension = fileArray[1]
    # numFiles = int(fileArray[2])
    print(type(paraViewFiles[0]))

    MyReader = OpenDataFile(paraViewFiles)
    # MyReader = OpenDataFile()
    # files = Glob(path = fileName+"*", rootDir=str(dirPath))
    # print("globbed files: {}".format(files))

    if MyReader:
        print("Success")
        # help(MyReader)
    else:
        print("failure")
    # sys.exit()

    # import_object = IOSSReader(registrationName='sparc-wall.exo.16.*', FileName=['C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.00', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.01', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.02', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.03', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.04', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.05', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.06', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.07', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.08', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.09', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.10', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.11', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.12', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.13', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.14', 'C:\\Users\\vjvalve\\Documents\\09326\\data_for_victor_2022Nov02\\surface_data\\sparc-wall.exo.16.15'])
    # imported_object = MyReader(registrationName= fileName+"*", FileName=exoFiles)
    # import_object = MyReader(registrationName=fileName, FileName=[filePathEdit])
    # imported_object = XMLPolyDataReader(registrationName=fileName, FileName=[filePathEdit])

    # activeObject = imported_object
    activeObject = MyReader

    # Apply all marked filters from objFilters to the paraview object
    try:
        if "merge_blocks" in objFilters:
            mergeBlocks1 = MergeBlocks(registrationName="MergeBlocks1", Input=activeObject)
            activeObject = mergeBlocks1
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)

        if "reflect" in objFilters:
            reflect1= Reflect(registrationName='Reflect1', Input=activeObject)
            reflect1.Plane = 'Z Min'
            activeObject = reflect1
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)

        if "extract_surface" in objFilters:
            extractSurface1 = ExtractSurface(registrationName = 'ExtractSurface1', Input=activeObject)
            activeObject = extractSurface1 
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)
        
        if "triangulate" in objFilters:
            triangulate1 = Triangulate(registrationName="Triangulate1", Input=activeObject)
            activeObject = triangulate1
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)

        if "cell_data_to_point_data" in objFilters or "cd_to_pd" in objFilters:
            cellDataToPointData1 = CellDatatoPointData(registrationName="CellDatatoPointData1", Input=activeObject)
            activeObject = cellDataToPointData1
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)
        
        if "generate_global_ids" in objFilters:
            activeObject = GenerateGlobalIds(registrationName='GenerateGlobalIds1', Input=activeObject)
            SetActiveSource(activeObject)
            UpdatePipeline(time=0.0, proxy=activeObject)

        
    except:
        print("unable to Extract Surface")

    print("filters applied")

    # Export all values to ExtractorOutput folder; code accessed from paraview trace feature
    if exportFlag:

        print("Creating Extractor for export")

        # create extractor
        pLY1 = CreateExtractor('PLY', activeObject, registrationName='PLY1')
        # trace defaults for the extractor.
        pLY1.Trigger = 'TimeStep'


        # Set up the timesteps
        # if timeStepRange is not None:
        #     pLY1.Trigger.UseStartTimeStep = 1
        #     pLY1.Trigger.UseEndTimeStep = 1
        #     pLY1.Trigger.StartTimeStep = timeStepRange[0]
        #     pLY1.Trigger.EndTimeStep = timeStepRange[1]
        # else:
        #     pLY1.Trigger.UseStartTimeStep = 0
        #     pLY1.Trigger.UseEndTimeStep = 0
            


        # init the 'PLY' selected for 'Writer'
        outputName = fileName.split('.')[0]
        pLY1.Writer.FileName = outputName+'.ply'
        pLY1.Writer.Input = activeObject
        pLY1.Writer.FileType = 'Ascii'

        # create extractor
        cSV_pD = CreateExtractor('CSV', activeObject, registrationName='CSV_PointData')
        # trace defaults for the extractor.
        cSV_pD.Trigger = 'TimeStep'


        # Set up the timesteps
        # if timeStepRange is not None:
        #     cSV_pD.Trigger.UseStartTimeStep = 1
        #     cSV_pD.Trigger.UseEndTimeStep = 1
        #     cSV_pD.Trigger.StartTimeStep = timeStepRange[0]
        #     cSV_pD.Trigger.EndTimeStep = timeStepRange[1]
        # else:
        #     cSV_pD.Trigger.UseStartTimeStep = 0
        #     cSV_pD.Trigger.UseEndTimeStep = 0

        # Properties modified on cSV1.Writer
        cSV_pD.Writer.FileName = outputName + outputCSVPD
        cSV_pD.Writer.FieldAssociation = "Point Data"

        # create extractor
        cSV_cD = CreateExtractor('CSV', activeObject, registrationName='CSV_CellData')
        # trace defaults for the extractor.
        cSV_cD.Trigger = 'TimeStep'

        # if timeStepRange is not None:
        #     cSV_cD.Trigger.UseStartTimeStep = 1
        #     cSV_cD.Trigger.UseEndTimeStep = 1
        #     cSV_cD.Trigger.StartTimeStep = timeStepRange[0]
        #     cSV_cD.Trigger.EndTimeStep = timeStepRange[1]
        # else:
        #     cSV_cD.Trigger.UseStartTimeStep = 0
        #     cSV_cD.Trigger.UseEndTimeStep = 0

        # Properties modified on cSV2.Writer
        cSV_cD.Writer.FileName = outputName + outputCSVCD
        cSV_cD.Writer.FieldAssociation = 'Cell Data'

        print("extractor defined")



        # ------------------------------------------------------------------------------
        # Catalyst options
        from paraview import catalyst
        options = catalyst.Options()
        options.ExtractsOutputDirectory = str(outputPath)
        # options.ExtractsOutputDirectory = filePath + "\\ExtractorOutput"
        options.GlobalTrigger = 'TimeStep'
        options.CatalystLiveTrigger = 'TimeStep'

        if timeStepRange is not None:
            options.GlobalTrigger.UseStartTimeStep = 1
            options.GlobalTrigger.UseEndTimeStep = 1

            options.GlobalTrigger.StartTimeStep = timeStepRange[0]
            options.GlobalTrigger.EndTimeStep = timeStepRange[1]
        else:
            options.GlobalTrigger.UseStartTimeStep = 0
            options.GlobalTrigger.UseEndTimeStep = 0

        # print(__name__)
        from paraview.simple import SaveExtractsUsingCatalystOptions
        # Code for non in-situ environments; if executing in post-processing
        # i.e. non-Catalyst mode, let's generate extracts using Catalyst options
        SaveExtractsUsingCatalystOptions(options)

        # print("Files saved in: " + filePath + "\\ExtractorOutput")
        print("Files saved in: " + str(outputPath))

# ------------------------------------------------------------------------------
# if __name__ == '__vtkconsole__':
#     GetPlyData()

if __name__ == '__main__':
    from os.path import exists
    import csv
    # import re
    import sys 
    import argparse

    currentPath = Path(__file__).parent
    dirPath = ""

    # Define our viable flags that can be used
    short_options = ['e', 'm', 'p:']
    long_options = ["export", "merge", "parameters="]
    flag_tuples = list(zip(short_options, long_options))
    print(*flag_tuples)

    # Create filter options set
    obj_filters_sets = {
        "default" : ["merge_blocks", "extract_surface", "triangulate", "cd_to_pd"],
        "default_reflect" : ["merge_blocks", "reflect", "extract_surface", "triangulate", "cd_to_pd"]
    }
    default_sparc_reflect = "merge_blocks, reflect, extract_surface, triangulate, cd_to_pd"
    default_sparc = "merge_blocks, extract_surface, triangulate, cd_to_pd"

    parser = argparse.ArgumentParser(description= 'Process command line flags')
    parser.add_argument("-f", "--file", nargs=1, required=True, help=".vtp file to be imported into ParaView")
    parser.add_argument("-e", "--export", action= "store_true", help="create file exports of the passed value")
    parser.add_argument("-m", "--merge", action= "store_true", help="merge our export files into a single .ply")
    parser.add_argument("-p", "--parameters", nargs= '+', default= ["all"], help="specify parameters that we want stitched into the .plys")
    
    
    # Reflect modifier as a possible filter
    parser.add_argument("-o", "--object_filters", nargs= '?', choices= obj_filters_sets.keys(), default= "default", help="what filter set do we want to apply to the exodus file")
    parser.add_argument("-t", "--time_steps", type=int, nargs= 2, help="what time steps should be exported")

    args = (parser.parse_args())
    print(args)
    
    inputFile = Path(args.file[0])
    inputFile = inputFile.resolve()


    # Verify that specified file to export from Paraview exists
    if not Path.exists(inputFile):
        raise FileNotFoundError

    dirPath = Path(inputFile).parent
    fileName = Path(inputFile).name

    print("dirPath: " +str(dirPath))

    exportFlag = args.export
    mergingFlag = args.merge
    timeStepRange = args.time_steps
    # timeStepFlag = args.time_steps
    stitchParams = args.parameters
    # objFilters = args.object_filters
    objFilters = obj_filters_sets[args.object_filters] 

    # print("exportFlag: {}\nmergeFlag: {}\nstitchParams: {}\nobjFilters: {}".format(exportFlag, mergingFlag, stitchParams, objFilters))
    # print("timeStepsRange: {}".format(timeStepRange))
    # print("currentPath: {}".format(currentPath))

    # sys.exit()
    if inputFile:
        outputPath = dirPath / "ExtractorOutput"

        # If export flag is marked then export the model from Paraview as a .ply along with its csv vertex and cell data
        if exportFlag:
            GetPlyData(dirPath=dirPath, fileName= fileName)
        
        # Iff merge flag is marked then take the exported data and merge additional vertex properties into a single .ply file
        if mergingFlag:

            outputName = fileName.split('.')[0]

            print("outputPath: " + str(outputPath))
            print("outputPath/outputName: " + str(outputPath/outputName))

            pointData = getCSVData(str(outputPath/outputName)+ outputCSVPD)
            addVertexPropertiesToPly(str(outputPath/outputName) + ".ply", pointData)
