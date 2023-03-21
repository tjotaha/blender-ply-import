def getNumVerts(plyFileLines):
  numVertLineIndex = 0
  while "element vertex" not in plyFileLines[numVertLineIndex]:
    numVertLineIndex += 1
  myTokens = plyFileLines[numVertLineIndex].split(" ")
  numVerts = int(myTokens[2])
  print("there are", numVerts, "Verts")
  return numVerts

def findEndOfHeaderIndex(plyFileLines):
  lastLineInHeaderIndex = 0
  while "end_header" not in plyFileLines[lastLineInHeaderIndex]:
    lastLineInHeaderIndex += 1
  print("lastLineInHeaderIndex", lastLineInHeaderIndex)
  return lastLineInHeaderIndex

def addPropertiesToVertexLine(oneVertexLine):
  myTokens = oneVertexLine.strip().split(" ")
  print(str(myTokens))
  xx = float(myTokens[0])
  yy = float(myTokens[1])
  zz = float(myTokens[2])
  addProp1 = xx + 2.0*yy + zz*3.0
  addProp2 = xx*xx - 4.0*yy*yy + 2.0*zz*zz
  addProp3 = xx + yy*yy + zz*zz*zz
  myTokens.append(str(addProp1))
  myTokens.append(str(addProp2))
  myTokens.append(str(addProp3))
  print(str(myTokens))
  myTokens.append("\n")
  returnLine = " ".join(myTokens)
  print(returnLine)
  return returnLine

def findLastVertPropertyLineIndex(plyFileLines):
  numVertLineIndex = 0
  while "element vertex" not in plyFileLines[numVertLineIndex]:
    numVertLineIndex += 1
  lastVertPropertyLineIndex = numVertLineIndex + 1
  if "property" not in plyFileLines[lastVertPropertyLineIndex]:
    print("expected propertly lines after element vertex line")
    exit(-1)
  while "property" in plyFileLines[lastVertPropertyLineIndex + 1]:
    lastVertPropertyLineIndex += 1
  return lastVertPropertyLineIndex

def addVertexPropertiesToPly(plyFileName):
  ff = open(plyFileName, "r")
  flines = ff.readlines()
  ff.close()
  numVerts = getNumVerts(flines)
  firstVertIndex = findEndOfHeaderIndex(flines) + 1
  for ii in range(firstVertIndex, firstVertIndex + numVerts):
      flines[ii] = addPropertiesToVertexLine(flines[ii])
  lastVertPropertyLineIndex = findLastVertPropertyLineIndex(flines)
  flines[lastVertPropertyLineIndex] += "property float density\nproperty float temperature\nproperty float pressure\n"
  ff = open("ply_file_with_added_properties.ply", "w")
  for oneLine in flines:
    ff.write(oneLine)
  ff.close()



#addVertexPropertiesToPly("small_wavelet_1.ply")
addVertexPropertiesToPly("wavelet_scaled_clipped_10.ply")
