// clang-format off

// Parse a3dapi.dll declarations as 32-bit types with .\ida\build_til.ps1.

#ifndef IDA_A3DAPI_H
#define IDA_A3DAPI_H

#define WIN32
#define _WINDOWS
#define _USRDLL
#define STRICT
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#define IDACLANG

#include "A3dPrivate.h"

#include "ChunkPage.h"
#include "Plex.h"
#include "LinkList.h"

#include "A3dMatrix.h"
#include "Corners.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "MaterialObject.h"
#include "A3dList.h"
#include "A3dFrame.h"
#include "A3dGeom.h"
#include "WallEdge.h"
#include "Polygon.h"
#include "PolygonBuilder.h"
#include "A3dRoom.h"
#include "A3dRoomBuilder.h"
#include "A3dWall.h"
#include "A3dWallBuilder.h"
#include "A3dScene.h"

#include "A3d3.h"
#include "A3dSource.h"
#include "a3dclsfc.h"
#include "apimapper.h"

#include "hrtfmgr.h"
#include "softmix.h"
#include "a2dbuffer.h"
#include "d2dbuffer.h"
#include "outqueue.h"
#include "resman.h"
#include "rmstatbuffer.h"
#include "rmstreambuffer.h"
#include "dal_a2d.h"
#include "dal_d2d.h"
#include "dalinfo.h"

#include "a3d202.h"

#endif // IDA_A3DAPI_H
