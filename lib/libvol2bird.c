/*
 * Copyright 2015 Adriaan Dokter & Netherlands eScience Centre
 * If you want to use this software, please contact me at a.m.dokter@uva.nl
 *
 * This program calculates Vertical Profiles of Birds (VPBs) as described in
 *
 * Bird migration flight altitudes studied by a network of operational weather radars
 * Dokter A.M., Liechti F., Stark H., Delobbe L., Tabary P., Holleman I.
 * J. R. Soc. Interface, 8, 30–43, 2011
 * DOI: 10.1098/rsif.2010.0116
 *
 */


#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <confuse.h>
#include <stdlib.h>
#include <math.h>
#include <vertical_profile.h>

#include "rave_io.h"
#include "rave_debug.h"
#include "polarvolume.h"
#include "polarscan.h"
#include "libvol2bird.h"
#include "libsvdfit.h"
#include "constants.h"
#undef RAD2DEG // to suppress redefine warning, also defined in dealias.h
#undef DEG2RAD // to suppress redefine warning, also defined in dealias.h
//#include "dealias.h"
#include "libdealias.h"

#ifdef RSL
#include "rsl.h"
#include <libgen.h>
#endif



// non-public function prototypes (local to this file/translation unit)

static int analyzeCells(PolarScan_t *scan, vol2birdScanUse_t scanUse, const int nCells, vol2bird_t *alldata);

static float calcDist(const int range1, const int azim1, const int range2, const int azim2, const float rscale, const float ascale);

static void calcTexture(PolarScan_t *scan, vol2birdScanUse_t scanUse, vol2bird_t* alldata);

static void classifyGatesSimple(vol2bird_t* alldata);

static void constructPointsArray(PolarVolume_t* volume, vol2birdScanUse_t *scanUse, vol2bird_t* alldata);

static int detNumberOfGates(const int iLayer, const float rangeScale, const float elevAngle,
                            const int nRang, const int nAzim, const float radarHeight, vol2bird_t* alldata);

static int detSvdfitArraySize(PolarVolume_t* volume, vol2birdScanUse_t *scanUse, vol2bird_t* alldata);

static vol2birdScanUse_t *determineScanUse(PolarVolume_t* volume, vol2bird_t* alldata);

static void exportBirdProfileAsJSON(vol2bird_t* alldata);

static int findWeatherCells(PolarScan_t *scan, const char* quantity, float quantityThreshold,
        int selectAboveThreshold, vol2bird_t* alldata);

static int findNearbyGateIndex(const int nAzimParent, const int nRangParent, const int iParent,
                        const int nAzimChild,  const int nRangChild,  const int iChild, int *iAzimReturn, int *iRangReturn);

static void fringeCells(PolarScan_t* scan, vol2bird_t* alldata);

CELLPROP* getCellProperties(PolarScan_t* scan, vol2birdScanUse_t scanUse, const int nCells, vol2bird_t* alldata);

static int getListOfSelectedGates(PolarScan_t* scan, vol2birdScanUse_t scanUse,
                                      const float altitudeMin, const float altitudeMax,
                                      float* points_local, int iRowPoints, int nColsPoints_local, vol2bird_t* alldata);

static int hasAzimuthGap(const float *points_local, const int nPoints, vol2bird_t* alldata);

static int includeGate(const int iProfileType, const int iQuantityType, const unsigned int gateCode, vol2bird_t* alldata);

static int verticalProfile_AddCustomField(VerticalProfile_t* self, RaveField_t* field, const char* quantity);

static int profileArray2RaveField(vol2bird_t* alldata, int idx_profile, int idx_quantity, const char* quantity, RaveDataType raveType);

static int mapVolumeToProfile(VerticalProfile_t* vp, PolarVolume_t* volume);

PolarScanParam_t* PolarScan_newParam(PolarScan_t *scan, const char *quantity, RaveDataType type);

int PolarVolume_dealias(PolarVolume_t* pvol);

int PolarVolume_getEndDateTime(PolarVolume_t* pvol, char** EndDate, char** EndTime);

int PolarVolume_getStartDateTime(PolarVolume_t* pvol, char** StartDate, char** StartTime);

const char* PolarVolume_getEndTime(PolarVolume_t* pvol);

const char* PolarVolume_getEndDate(PolarVolume_t* pvol);

const char* PolarVolume_getStartDate(PolarVolume_t* pvol);

const char* PolarVolume_getStartTime(PolarVolume_t* pvol);

double PolarVolume_getWavelength(PolarVolume_t* pvol);

#ifdef RSL
PolarVolume_t* PolarVolume_RSL2RaveOLD(Radar* radar, float rangeMax);

PolarVolume_t* PolarVolume_vol2bird_RSL2Rave(Radar* radar, float rangeMax);

PolarVolume_t* PolarVolume_RSL2Rave(Radar* radar, float rangeMax);

PolarScan_t* PolarScan_RSL2Rave(Radar *radar, int iScan, float rangeMax);

PolarScanParam_t* PolarScanParam_RSL2Rave(Radar *radar, float elev, int RSL_INDEX,float rangeMax);
    
int rslCopy2Rave(Sweep *rslSweep,PolarScanParam_t* scanparam);
#endif

static void printCellProp(CELLPROP* cellProp, float elev, int nCells, int nCellsValid, vol2bird_t *alldata);

static void printGateCode(char* flags, const unsigned int gateCode);

static void printImage(PolarScan_t* scan, const char* quantity);

static int printMeta(PolarScan_t* scan, const char* quantity);

static void printProfile(vol2bird_t* alldata);

static int removeDroppedCells(CELLPROP *cellProp, const int nCells);

static int selectCellsToDrop(CELLPROP *cellProp, int nCells, vol2bird_t* alldata);

static int selectCellsToDrop_singlePol(CELLPROP *cellProp, int nCells, vol2bird_t* alldata);

static int selectCellsToDrop_dualPol(CELLPROP *cellProp, int nCells, vol2bird_t* alldata);

static void sortCellsByArea(CELLPROP *cellProp, const int nCells);

static void updateFlagFieldsInPointsArray(const float* yObs, const float* yFitted, const int* includedIndex, 
                                          const int nPointsIncluded, float* points_local, vol2bird_t* alldata);

static int updateMap(PolarScan_t* scan, CELLPROP *cellProp, const int nCells, vol2bird_t* alldata);



// non-public function declarations (local to this file/translation unit)

static int analyzeCells(PolarScan_t *scan, vol2birdScanUse_t scanUse, const int nCells, vol2bird_t *alldata) {

    // ----------------------------------------------------------------------------------- // 
    //  This function analyzes the cellImage array found by the 'findWeatherCells'         //
    //  procedure. Small cells are rejected and the cells are re-numbered according        //
    //  to size. The final number of cells in cellImage is returned as an integer.         // 
    // ----------------------------------------------------------------------------------- //

    CELLPROP *cellProp;
    int nGlobal;
    int nCellsValid;
    long nAzim;
    long nRang;

    nCellsValid = nCells;
    nRang = PolarScan_getNbins(scan);
    nAzim = PolarScan_getNrays(scan);
    nGlobal = nAzim*nRang;
    nCellsValid = 0;
    
    if(!PolarScan_hasParameter(scan, scanUse.cellName)){
        fprintf(stderr,"no CELL quantity in polar scan, aborting analyzeCells()\n");
        return 0;
    }
    
    // first deal with the case that no weather cells were detected by findWeatherCells
    if (nCells == 0) {
        for (int iAzim = 0; iAzim < nAzim; iAzim++) {
            for (int iRang = 0; iRang < nRang; iRang++) {
                PolarScan_setParameterValue(scan, scanUse.cellName, iRang, iAzim, -1);
            }
        }
        return nCellsValid;
    }
    
    cellProp = getCellProperties(scan, scanUse, nCells, alldata);

    selectCellsToDrop(cellProp, nCells, alldata);    
    
    // sorting cell properties according to cell area. Drop small cells from map
    nCellsValid = updateMap(scan, cellProp, nCells, alldata);
        
    // printing of cell properties to stderr
    if (alldata->options.printCellProp == TRUE) {
        printCellProp(cellProp, (float) PolarScan_getElangle(scan), nCells, nCellsValid, alldata);
    } // endif (printCellProp == TRUE)

    free(cellProp);

    return nCellsValid;
} // analyzeCells



static float calcDist(const int iRang1, const int iAzim1, const int iRang2, 
    const int iAzim2, const float rangScale, const float azimScaleDeg) {

    // -------------------------------------------------------- //
    // This function calculates the distance between two gates  //
    // -------------------------------------------------------- //

    float range1;
    float range2;
    float azimuth1;
    float azimuth2;

    range1 = (float) iRang1 * rangScale;
    range2 = (float) iRang2 * rangScale;

    azimuth1 = (float) iAzim1 * azimScaleDeg * (float) DEG2RAD;
    azimuth2 = (float) iAzim2 * azimScaleDeg * (float) DEG2RAD;

    return (float) sqrt((range1 * range1) +
                (range2 * range2) -
                2 * (range1 * range2) * cos(azimuth1-azimuth2));


} // calcDist



static void calcTexture(PolarScan_t *scan, vol2birdScanUse_t scanUse, vol2bird_t* alldata) {


    // --------------------------------------------------------------------------------------- //
    // This function computes a texture parameter based on a block of (nRangNeighborhood x     //
    // nAzimNeighborhood) pixels. The texture parameter equals the local standard deviation    //
    // in the radial velocity field                                                            //
    // --------------------------------------------------------------------------------------- //


    int iRang, iRangLocal;
    int iAzim, iAzimLocal;
    long nRang;
    long nAzim;
    int iNeighborhood;
    int nNeighborhood;
    int count;
    double vradValGlobal;
    double vradValLocal;
    double dbzValGlobal;
    double dbzValLocal;
    double vradMissingValue;
    double vradUndetectValue;
    double dbzMissingValue;
    double dbzUndetectValue;
    double texMissingValue;
    double vmoment1;
    double vmoment2;
    double dbz;
    double tex;
    int iGlobal;
    int iLocal;
    double texOffset;
    double texScale;
    double dbzOffset;
    double dbzScale;
    double vradOffset;
    double vradScale;
    double vRadDiff;

    nRang = PolarScan_getNbins(scan);
    nAzim = PolarScan_getNrays(scan);

    PolarScanParam_t* texImage = PolarScan_getParameter(scan, scanUse.texName);
    PolarScanParam_t* vradImage = PolarScan_getParameter(scan, scanUse.vradName);
    PolarScanParam_t* dbzImage = PolarScan_getParameter(scan, scanUse.dbzName);

    dbzOffset = PolarScanParam_getOffset(dbzImage);
    dbzScale = PolarScanParam_getGain(dbzImage);
    dbzMissingValue = PolarScanParam_getNodata(dbzImage);
    dbzUndetectValue = PolarScanParam_getUndetect(dbzImage);

    vradOffset = PolarScanParam_getOffset(vradImage);
    vradScale = PolarScanParam_getGain(vradImage);
    vradMissingValue = PolarScanParam_getNodata(vradImage);
    vradUndetectValue = PolarScanParam_getUndetect(vradImage);
    
    texOffset = PolarScanParam_getOffset(texImage);
    texScale = PolarScanParam_getGain(texImage);
    texMissingValue = PolarScanParam_getNodata(texImage);

    nNeighborhood = (int)(alldata->constants.nRangNeighborhood * alldata->constants.nAzimNeighborhood);

    for (iAzim = 0; iAzim < nAzim; iAzim++) {
        for (iRang = 0; iRang < nRang; iRang++) {

            iGlobal = iRang + iAzim * nRang;

            // count number of direct neighbors above threshold
            count = 0;
            vmoment1 = 0;
            vmoment2 = 0;

            dbz = 0;

            for (iNeighborhood = 0; iNeighborhood < nNeighborhood; iNeighborhood++) {

                iLocal = findNearbyGateIndex(nAzim,nRang,iGlobal,alldata->constants.nAzimNeighborhood,
                         alldata->constants.nRangNeighborhood,iNeighborhood,&iAzimLocal,&iRangLocal);

                #ifdef FPRINTFON
                fprintf(stderr, "iLocal = %d; ",iLocal);
                #endif

                if (iLocal < 0) {
                    // iLocal less than zero are error codes
                    continue;
                }
                
                PolarScanParam_getValue(vradImage, iRang, iAzim, &vradValGlobal);
                PolarScanParam_getValue(vradImage, iRangLocal, iAzimLocal, &vradValLocal);
                PolarScanParam_getValue(dbzImage, iRang, iAzim, &dbzValGlobal);
                PolarScanParam_getValue(dbzImage, iRangLocal, iAzimLocal, &dbzValLocal);

                
                if (vradValLocal == vradMissingValue || dbzValLocal == dbzMissingValue ||
                    vradValLocal == vradUndetectValue || dbzValLocal == dbzUndetectValue) {
                    continue;
                }

                vRadDiff = vradOffset + vradScale * (vradValGlobal - vradValLocal);
                vmoment1 += vRadDiff;
                vmoment2 += SQUARE(vRadDiff);

                dbz += dbzOffset + dbzScale * dbzValLocal;

                count++;

            }

            vmoment1 /= count;
            vmoment2 /= count;
            dbz /= count;

            // when not enough neighbors, continue
            if (count < alldata->constants.nCountMin) {
                PolarScanParam_setValue(texImage,iRang,iAzim,texMissingValue);
            }
            else {

                tex = sqrt(XABS(vmoment2-SQUARE(vmoment1)));

                float tmpTex = (tex - texOffset) / texScale;
                if (-FLT_MAX <= tmpTex && tmpTex <= FLT_MAX) {
                    PolarScanParam_setValue(texImage,iRang,iAzim,(double) tmpTex);
                }
                else {
                    fprintf(stderr, "Error casting texture value of %f to float type at texImage[%d]. Aborting.\n",tmpTex,iGlobal);
                    return;
                }
                

                #ifdef FPRINTFON
                fprintf(stderr,
                        "\n(C) count = %d; nCountMin = %d; vmoment1 = %f; vmoment2 = %f; tex = %f; texBody[%d] = %d\n",
                        count, alldata->constants.nCountMin, vmoment1, vmoment2, tex,
                        iGlobal, tmpTex);
                #endif

            } //else
        } //for
    } //for
} // calcTexture



static void classifyGatesSimple(vol2bird_t* alldata) {
    
    int iPoint;
    
    for (iPoint = 0; iPoint < alldata->points.nRowsPoints; iPoint++) {
    
        const float azimValue = alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.azimAngleCol];
        const float dbzValue = alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.dbzValueCol];        
        const float vradValue = alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.vradValueCol];
        const int cellValue = (int) alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.cellValueCol];

        unsigned int gateCode = 0;
        
        if (FALSE) {
            // this gate is true in the static clutter map (which we don't have yet TODO)
            gateCode |= 1<<(alldata->flags.flagPositionStaticClutter);
        }

        if (cellValue > 1) {
            // this gate is part of the cluttermap (without fringe)
            gateCode |= 1<<(alldata->flags.flagPositionDynamicClutter);
        }

        if (cellValue == 1) {
            // this gate is part of the fringe of the cluttermap
            gateCode |= 1<<(alldata->flags.flagPositionDynamicClutterFringe);
        }

        if ((isnan(vradValue) == TRUE) || (isnan(dbzValue) == TRUE)) {
            // this gate has no valid radial velocity data
            gateCode |= 1<<(alldata->flags.flagPositionVradMissing);
        }

        if (dbzValue > alldata->misc.dbzMax) {
            // this gate's dbz value is too high to be due to birds, it must be 
            // caused by something else
            gateCode |= 1<<(alldata->flags.flagPositionDbzTooHighForBirds);
        }

        if (fabs(vradValue) < alldata->constants.vradMin) {
            // this gate's radial velocity is too low; Excluded because possibly clutter
            gateCode |= 1<<(alldata->flags.flagPositionVradTooLow);
        }

        if (FALSE) {
            // this flag is set later on by updateFlagFieldsInPointsArray()
            // flagPositionVDifMax
        }

        if (azimValue < alldata->options.azimMin) {
            // the user can specify to exclude gates based on their azimuth;
            // this clause is for gates that have too low azimuth
            gateCode |= 1<<(alldata->flags.flagPositionAzimTooLow);
        }
        
        if (azimValue > alldata->options.azimMax) {
            // the user can specify to exclude gates based on their azimuth;
            // this clause is for gates that have too high azimuth
            gateCode |= 1<<(alldata->flags.flagPositionAzimTooHigh);
        }

        alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.gateCodeCol] = (float) gateCode;
        
    }

    return;
    
    
};




static void constructPointsArray(PolarVolume_t* volume, vol2birdScanUse_t* scanUse, vol2bird_t* alldata) {
    
        // iterate over the scans in 'volume'
        int iScan;
        int nScans;
        
        // determine how many scan elevations the volume object contains
        nScans = PolarVolume_getNumberOfScans(volume);


        for (iScan = 0; iScan < nScans; iScan++) {
            if (scanUse[iScan].useScan == 1)
            {

                // initialize the scan object
                PolarScan_t* scan = NULL;
            
                // extract the scan object from the volume object
                scan = PolarVolume_getScan(volume, iScan);
                
                PolarScanParam_t *cellScanParam = PolarScan_newParam(scan, scanUse->cellName, RaveDataType_INT);
                PolarScanParam_t *texScanParam = NULL;
                PolarScanParam_t *clutScanParam = NULL;                

                // only when static cluttermap data is available, initialize cluttermap field                
                if(alldata->options.useStaticClutterData){
                    clutScanParam = PolarScan_newParam(scan, scanUse->clutName, RaveDataType_DOUBLE);
                }
                
                // only when dealing with normal (non-dual pol) data, initialize a vrad texture field
                if(!alldata->options.dualPol){
                    texScanParam = PolarScan_newParam(scan, scanUse->texName, RaveDataType_DOUBLE);
                }
                
                int nCells = -1;
                if (alldata->options.dualPol){

                    // ------------------------------------------------------------- //
                    //        find (weather) cells in the reflectivity image         //
                    // ------------------------------------------------------------- //
                    
                    nCells = findWeatherCells(scan,scanUse[iScan].rhohvName,
                                    alldata->options.rhohvThresMin,TRUE,alldata);

                }
                else{

                    // ------------------------------------------------------------- //
                    //                      calculate vrad texture                   //
                    // ------------------------------------------------------------- //

                    calcTexture(scan, scanUse[iScan], alldata);
    
                    // ------------------------------------------------------------- //
                    //        find (weather) cells in the reflectivity image         //
                    // ------------------------------------------------------------- //
                    
                    nCells = findWeatherCells(scan,scanUse[iScan].dbzName,alldata->options.dbzThresMin,TRUE,alldata);

                }
                
                if (nCells<0){
                    fprintf(stderr,"Error: findWeatherCells exited with errors\n");
                    return;
                }
                
                if (alldata->options.printCellProp == TRUE) {
                    fprintf(stderr,"(%d/%d): found %d cells.\n",iScan, nScans, nCells);
                }
                
                // ------------------------------------------------------------- //
                //                      analyze cells                            //
                // ------------------------------------------------------------- //
    
                analyzeCells(scan, scanUse[iScan], nCells, alldata);
    
                // ------------------------------------------------------------- //
                //                     calculate fringe                          //
                // ------------------------------------------------------------- //
    
                fringeCells(scan, alldata); 
                
                // ------------------------------------------------------------- //
                //            print selected outputs to stderr                   //
                // ------------------------------------------------------------- //
    
                if (alldata->options.printDbz == TRUE) {
                    fprintf(stderr,"product = dbz\n");
                    printMeta(scan,scanUse[iScan].dbzName);
                    printImage(scan,scanUse[iScan].dbzName);
                }
                if (alldata->options.printVrad == TRUE) {
                    fprintf(stderr,"product = vrad\n");
                    printMeta(scan,scanUse[iScan].vradName);
                    printImage(scan,scanUse[iScan].vradName);
                }
                if (alldata->options.printRhohv == TRUE) {
                    fprintf(stderr,"product = rhohv\n");
                    printMeta(scan,scanUse[iScan].rhohvName);
                    printImage(scan,scanUse[iScan].rhohvName);
                }
                if (alldata->options.printTex == TRUE) {
                    fprintf(stderr,"product = tex\n");
                    printMeta(scan,scanUse[iScan].texName);
                    printImage(scan,scanUse[iScan].texName);
                }
                if (alldata->options.printCell == TRUE) {
                    fprintf(stderr,"product = cell\n");
                    printMeta(scan,scanUse[iScan].cellName);
                    printImage(scan,scanUse[iScan].cellName);
                }
                if (alldata->options.printClut == TRUE) { 
                    fprintf(stderr,"product = clut\n");
                    printMeta(scan,scanUse[iScan].clutName);
                    printImage(scan,scanUse[iScan].clutName);
                }
                            
                // ------------------------------------------------------------- //
                //    fill in the appropriate elements in the points array       //
                // ------------------------------------------------------------- //
    
                int iLayer;
                
                for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
                    
                    float altitudeMin = iLayer * alldata->options.layerThickness;
                    float altitudeMax = (iLayer + 1) * alldata->options.layerThickness;
                    int iRowPoints = alldata->points.indexFrom[iLayer] + alldata->points.nPointsWritten[iLayer];
                        
                    int n = getListOfSelectedGates(scan, scanUse[iScan], altitudeMin, altitudeMax, 
                        &(alldata->points.points[0]), iRowPoints, alldata->points.nColsPoints, alldata);
                    
                    alldata->points.nPointsWritten[iLayer] += n;

                    if (alldata->points.indexFrom[iLayer] + alldata->points.nPointsWritten[iLayer] > alldata->points.indexTo[iLayer]) {
                        fprintf(stderr, "Problem occurred: writing over existing data\n");
                        return;
                    }
    
                } // endfor (iLayer = 0; iLayer < nLayers; iLayer++)
    
                // ------------------------------------------------------------- //
                //                         clean up                              //
                // ------------------------------------------------------------- //
    
                // free previously malloc'ed arrays                
                RAVE_OBJECT_RELEASE(scan);
                RAVE_OBJECT_RELEASE(texScanParam);
                RAVE_OBJECT_RELEASE(cellScanParam);
                RAVE_OBJECT_RELEASE(clutScanParam);
                
            }
        } // endfor (iScan = 0; iScan < nScans; iScan++)
}



static int detNumberOfGates(const int iLayer, 
                     const float rangeScale, const float elevAngle,
                     const int nRang, const int nAzim,
                     const float radarHeight, vol2bird_t* alldata) {

    // Determine the number of gates that are within the limits set
    // by (rangeMin,rangeMax) as well as by (iLayer*layerThickness,
    // (iLayer+1)*layerThickness).

    int nGates;
    int iRang;

    float layerHeight;
    float range;
    float beamHeight;


    nGates = 0;
    layerHeight = (iLayer + 0.5) * alldata->options.layerThickness;
    for (iRang = 0; iRang < nRang; iRang++) {
        range = (iRang + 0.5) * rangeScale;
        if (range < alldata->options.rangeMin || range > alldata->options.rangeMax) {
            // the gate is too close to the radar, or too far away
            continue;
        }
        beamHeight = range * sin(elevAngle * DEG2RAD) + radarHeight;
        if (fabs(layerHeight - beamHeight) > 0.5*alldata->options.layerThickness) {
            // the gate is not close enough to the altitude layer of interest
            continue;
        }

        #ifdef FPRINTFON
        fprintf(stderr, "iRang = %d; range = %f; beamHeight = %f\n",iRang,range,beamHeight);
        #endif

        nGates += nAzim;

    } // for iRang

    return nGates;

} // detNumberOfGates()





static int detSvdfitArraySize(PolarVolume_t* volume, vol2birdScanUse_t* scanUse, vol2bird_t* alldata) {
    
    int iScan;
    int nScans = PolarVolume_getNumberOfScans(volume);

    int iLayer;
    int nRowsPoints_local = 0;
    
    int* nGates = malloc(sizeof(int) * alldata->options.nLayers);
    int* nGatesAcc = malloc(sizeof(int) * alldata->options.nLayers);
    
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        nGates[iLayer] = 0;
        nGatesAcc[iLayer] = 0;
    }

    for (iScan = 0; iScan < nScans; iScan++) {
        if (scanUse[iScan].useScan == 1)
        {
            for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
    
                PolarScan_t* scan = PolarVolume_getScan(volume, iScan);
                
                int nRang = (int) PolarScan_getNbins(scan);
                int nAzim = (int) PolarScan_getNrays(scan);
                float elevAngle = (float) (360 * PolarScan_getElangle(scan) / 2 / PI );
                float rangeScale = (float) PolarScan_getRscale(scan);
                float radarHeight = (float) PolarScan_getHeight(scan);

                nGates[iLayer] += detNumberOfGates(iLayer, 
                    rangeScale, elevAngle, nRang, 
                    nAzim, radarHeight, alldata);
                    
                RAVE_OBJECT_RELEASE(scan);

            }
        }
    }

    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        if (iLayer == 0) {
            nGatesAcc[iLayer] = nGates[iLayer];
        }
        else {
            nGatesAcc[iLayer] = nGatesAcc[iLayer-1] + nGates[iLayer];
        }
    }
    nRowsPoints_local = nGatesAcc[alldata->options.nLayers-1];

    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        if (iLayer == 0) {
            alldata->points.indexFrom[iLayer] = 0;
        }
        else {
            alldata->points.indexFrom[iLayer] = nGatesAcc[iLayer-1];
        }
    }
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        alldata->points.indexTo[iLayer] = nGatesAcc[iLayer];
    }

    free((void*) nGates);
    free((void*) nGatesAcc);

    return nRowsPoints_local;
    
}  // detSvdfitArraySize()




// Determine whether to use scan
static vol2birdScanUse_t* determineScanUse(PolarVolume_t* volume, vol2bird_t* alldata)
{
    RAVE_ASSERT((volume != NULL), "pvol == NULL");
    
	RaveAttribute_t *attr;
	PolarScan_t *scan;
	PolarScanParam_t *param;
	int result, nScans, iScan, nScansUsed;
	int noNyquist=0;
	vol2birdScanUse_t *scanUse;
	double nyquist, nyquistMin = DBL_MAX, nyquistMinUsed = DBL_MAX, nyquistMax = 0;
 
	
	// Read number of scans
	nScans = PolarVolume_getNumberOfScans(volume);
    
    // variable that will contain the number of scans to be used as determined by this function
    nScansUsed = 0;
	
	// Allocate memory for useScan variable
	scanUse = (vol2birdScanUse_t *) malloc(nScans * sizeof(vol2birdScanUse_t));
	
    // check that correlation coefficient is present
    // if not, revert to single pol mode
    if (alldata->options.dualPol){
        int dualPolPresent = FALSE;
        
        for (iScan = 0; iScan < nScans; iScan++)
        {
            scan = PolarVolume_getScan(volume, iScan);
            if (PolarScan_hasParameter(scan, "RHOHV")){
                dualPolPresent = TRUE;
            }
        }
        if (!dualPolPresent){
            fprintf(stderr,"Warning: no dual-pol moments not found, entering SINGLE POL mode\n");
            alldata->options.dualPol = FALSE;
        }
    }
    
	for (iScan = 0; iScan < nScans; iScan++)
	{
		// Initialize useScan and result
		scanUse[iScan].useScan = FALSE;
		result = 0;
		
		scan = PolarVolume_getScan(volume, iScan);

        // check that radial velocity parameter is present
		if (PolarScan_hasParameter(scan, "VRAD")){
			sprintf(scanUse[iScan].vradName,"VRAD");	
			scanUse[iScan].useScan = TRUE;
		}
		else{
			if (PolarScan_hasParameter(scan, "VRADH")){
				sprintf(scanUse[iScan].vradName,"VRADH");	
				scanUse[iScan].useScan = TRUE;
			}
			else{
				if (PolarScan_hasParameter(scan, "VRADV")){
					sprintf(scanUse[iScan].vradName,"VRADV");	
					scanUse[iScan].useScan = TRUE;
				}
			}
		}
		if (scanUse[iScan].useScan == FALSE){
			fprintf(stderr,"Warning: radial velocity missing, dropping scan %i ...\n",iScan);
		}

        // check that reflectivity parameter is present
        if (scanUse[iScan].useScan){
            if(PolarScan_hasParameter(scan,alldata->options.dbzType)){
                strcpy(scanUse[iScan].dbzName,alldata->options.dbzType);	
            }
            else{
                fprintf(stderr,"Warning: requested reflectivity factor '%s' missing, searching for alternatives ...\n",alldata->options.dbzType);
                if(PolarScan_hasParameter(scan, "DBZH")){
                    sprintf(scanUse[iScan].dbzName,"DBZH");	
                }
                else{
                    if (PolarScan_hasParameter(scan, "DBZV")){
                        sprintf(scanUse[iScan].dbzName,"DBZV");	
                    }
                    else{	
                        scanUse[iScan].useScan = FALSE;
                    }
		        }
            }
            if (scanUse[iScan].useScan == FALSE){
                fprintf(stderr,"Warning: reflectivity factor missing, dropping scan %i ...\n",iScan);
            }
        }
        
        // check that correlation coefficient is present
        if (alldata->options.dualPol){
            if (PolarScan_hasParameter(scan, "RHOHV")){
                sprintf(scanUse[iScan].rhohvName,"RHOHV");	
                scanUse[iScan].useScan = TRUE;
            }
            else{
                fprintf(stderr,"Warning: correlation coefficient missing, dropping scan %i ...\n",iScan);
                scanUse[iScan].useScan = FALSE;
            }
        }
        
        // check that elevation is not too high or too low
        if (scanUse[iScan].useScan)
        {
            // drop scans with elevations outside the range set by user
            double elev = 360*PolarScan_getElangle(scan) / 2 / PI;
            if (elev < alldata->options.elevMin || elev > alldata->options.elevMax)
            {
                scanUse[iScan].useScan = FALSE;
                    fprintf(stderr,"Warning: elevation (%.1f deg) outside valid elevation range (%.1f-%.1f deg), dropping scan %i ...\n",\
                    elev,alldata->options.elevMin,alldata->options.elevMax,iScan);
            }
        }
        
        // check that range bin size has correct units
        if (scanUse[iScan].useScan)
        {
            // drop scans with range bin sizes below 1 meter
            double rscale = PolarScan_getRscale(scan);
            if (rscale < RSCALEMIN || rscale == 0)
            {
                scanUse[iScan].useScan = FALSE;
                    fprintf(stderr,"Warning: range bin size (%.2f metre) too small, dropping scan %i ...\n",\
                    rscale,iScan);
            }
        }
        
        // check that Nyquist velocity is not too low
        // retrieve Nyquist velocity if not present at scan level
        if (scanUse[iScan].useScan)
        {
            // Read Nyquist interval from scan how group
            attr = PolarScan_getAttribute(scan, "how/NI");
            result = 0;
            if (attr != (RaveAttribute_t *) NULL) result = RaveAttribute_getDouble(attr, &nyquist);

            // Read Nyquist interval from top level how group
            if (result == 0)
            {	
                // set flag that we found no nyquist interval at scan level
                noNyquist = 1;
                // proceed to top level how group
                attr = PolarVolume_getAttribute(volume, "how/NI");
                if (attr != (RaveAttribute_t *) NULL) result = RaveAttribute_getDouble(attr, &nyquist);
            }
            
            // Derive Nyquist interval from the offset attribute of the dataset
            // when no NI attribute was found
            if (result == 0)
            {
                // in case that we'll be using dealiased velocities, but also the original is available
                // we want to get the offset attribute of the original
                if(alldata->options.dealiasVrad && PolarScan_hasParameter(scan, "VRADDH") && PolarScan_hasParameter(scan, "VRADH")){
                    param = PolarScan_getParameter(scan, "VRADH");
                    nyquist = fabs(PolarScanParam_getOffset(param));
                }
                else{
                    param = PolarScan_getParameter(scan, scanUse[iScan].vradName);
                    nyquist = fabs(PolarScanParam_getOffset(param));
                }
                fprintf(stderr,"Warning: Nyquist interval attribute not found for scan %i, using radial velocity offset (%.1f m/s) instead \n",iScan,nyquist);
                RAVE_OBJECT_RELEASE(param);
            }
            
            // Set useScan to 0 if no Nyquist interval is available or if it is too low
            // only check for nyquist interval when we are NOT dealiasing the velocities
            if (!alldata->options.dealiasVrad && nyquist < alldata->options.minNyquist){
                scanUse[iScan].useScan = 0;
                fprintf(stderr,"Warning: Nyquist velocity (%.1f m/s) too low, dropping scan %i ...\n",nyquist,iScan);
            }
            
            // if Nyquist interval (NI) attribute was missing at scan level, add it now
            if (noNyquist){
                RaveAttribute_t* attr_NI = RaveAttributeHelp_createDouble("how/NI", (double) nyquist);
                if (attr_NI == NULL && nyquist > 0){
                    fprintf(stderr, "warning: no valid Nyquist attribute could be added to scan\n");
                }
                else{    
                    PolarScan_addAttribute(scan, attr_NI);
                }
                RAVE_OBJECT_RELEASE(attr_NI);
            }
            
            if (nyquist < nyquistMin){
                nyquistMin=nyquist;
            }
            if (nyquist < nyquistMinUsed && nyquist > alldata->options.minNyquist){
                nyquistMinUsed=nyquist;
            }
            if (nyquist > nyquistMax){
                nyquistMax=nyquist;
            }
            
        }
        
        if (scanUse[iScan].useScan){

            // copy names of scans that will be generated by the program
            sprintf(scanUse[iScan].texName,TEXNAME);	
            sprintf(scanUse[iScan].clutName,CLUTNAME);	
            sprintf(scanUse[iScan].cellName,CELLNAME);	

            nScansUsed+=1;
        }
    }
        
        
    alldata->misc.nyquistMin = nyquistMin;
    alldata->misc.nyquistMinUsed = nyquistMinUsed;
    alldata->misc.nyquistMax = nyquistMax;
    
    // FIXME: better to make scanUse a struct that contains both the array of vol2birdScanUse_t objects
    // and the number nScansUSed, which now is stored ad hoc under alldata->misc
    alldata->misc.nScansUsed = nScansUsed;
    if(nScansUsed == 0){
        alldata->misc.vol2birdSuccessful = FALSE;
        return (vol2birdScanUse_t*) NULL;
    }
    
    // Return the array scanUse
    return scanUse;
}



static void exportBirdProfileAsJSON(vol2bird_t *alldata) {
    
    // produces valid JSON according to http://jsonlint.com/
    
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }
    
    if (alldata->profiles.iProfileTypeLast != 1) {
        fprintf(stderr,"Export method expects profile 1, but found %d. Aborting.",alldata->profiles.iProfileTypeLast);
        return; 
    }
    
    int iLayer;


    FILE *f = fopen("vol2bird-profile1.json", "w");
    if (f == NULL)
    {
        printf("Error opening file 'vol2bird-profile1.json'!\n");
        exit(1);
    }
    
    fprintf(f,"[\n");
    for (iLayer = 0;iLayer < alldata->options.nLayers; iLayer += 1) {
        
        fprintf(f,"   {\n");
        
        {
            char varName[] = "HGHT";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  0];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {
            char varName[] = "HGHT_max";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  1];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
            
        {
            char varName[] = "u";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  2];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {    
            char varName[] = "v";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  3];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {    
            char varName[] = "w";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  4];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {
            char varName[] = "ff";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  5];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
            
        {
            char varName[] = "dd";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  6];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {    
            char varName[] = "sd_vvp";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  7];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
            
        {
            char varName[] = "gap";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  8];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%s,\n",varName,val == TRUE ? "true" : "false");
            }
        }

        {            
            char varName[] = "dbz";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  9];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
            
        {
            char varName[] = "n";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  10];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%d,\n",varName,(int) val);
            }
        }
            
        {
            char varName[] = "eta";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  11];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f,\n",varName,val);
            }
        }
        
        {    
            char varName[] = "dens";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  12];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%.2f\n",varName,val);
            }
        }

        {
            char varName[] = "n_dbz";
            float val = alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  13];
            if (isnan(val) == TRUE) {
                fprintf(f,"    \"%s\":null,\n",varName);
            }
            else {
                fprintf(f,"    \"%s\":%d,\n",varName,(int) val);
            }
        }

        fprintf(f,"   }");
        if (iLayer < alldata->options.nLayers - 1) {
            fprintf(f,",");
        }
        fprintf(f,"\n");
    }
    fprintf(f,"]\n");

}



static int findWeatherCells(PolarScan_t *scan, const char* quantity, float quantityThreshold,
        int selectAboveThreshold, vol2bird_t* alldata) {

    //  ----------------------------------------------------------------------------- //
    //  This function detects the cells in 'quantityImage' using a threshold value of      //
    //  'dbzThresMin' and a non-recursive algorithm which looks for neighboring       //
    //  pixels above that threshold. On return the marked cells are contained by      //
    //  'cellImage'. The number of detected cells/highest index value is returned.    //
    //  ----------------------------------------------------------------------------- //


    int iCellIdentifier;
    int nCells;
    int iRang,iRangLocal;
    int nRang;
    int iAzim, iAzimLocal;
    int nAzim;
    int iNeighborhood;
    int nNeighborhood;
    int count;
    int cellImageInitialValue;

    float quantityThres;

    int iGlobal;
    int iGlobalOther;
    int nGlobal;
    int iLocal;

    double quantityMissing;
    double quantityUndetect;
    int quantitynAzim;
    int quantitynRang;
    double quantityValueOffset;
    double quantityValueScale;
    double quantityValueGlobal, quantityValueLocal;
    double cellValueGlobal, cellValueLocal, cellValueOther;


    float quantityRangeScale;


    // These next two local variables 'nAzimNeighborhood' and 
    // 'nRangNeighborhood' shadow two static global variables of the 
    // same name, but that is in fact what you want. This is because the
    // value of 'nAzimNeighborHood' (function scope) is not equal to 
    // 'nAzimNeighborHood' (file scope) and because 'nRangNeighborHood' 
    // (function scope) is not equal to 'nRangNeighborHood' (file 
    // scope). The variable names are the same though, because they play
    // the same role in 'findNearbyGateIndex()'.
    int nAzimNeighborhood_local;
    int nRangNeighborhood_local;
    
    int nHalfNeighborhood;

    int cellIdentifierGlobal;
    int cellIdentifierGlobalOther;
    int iGlobalInner;


    #ifdef FPRINTFON
    int dbg = 0;
    #endif

    if (scan==NULL) {
        fprintf(stderr,"Input scan is NULL.\n");
        return -1;
    }
    
    PolarScanParam_t *scanParam = PolarScan_getParameter(scan, quantity);
    PolarScanParam_t *cellParam = PolarScan_getParameter(scan, CELLNAME);

    if (scanParam == NULL || cellParam == NULL) {
        fprintf(stderr,"%s and/or CELL quantities not found in polar scan\n", quantity);
        return -1;
    }

    // get direct pointer to the data block
    int* cellParamData = (int*) PolarScanParam_getData(cellParam);
    
    quantityMissing = PolarScanParam_getNodata(scanParam);
    quantityUndetect = PolarScanParam_getUndetect(scanParam);
    quantitynAzim = PolarScan_getNrays(scan);
    quantitynRang = PolarScan_getNbins(scan);
    quantityValueOffset = PolarScanParam_getOffset(scanParam);
    quantityValueScale = PolarScanParam_getGain(scanParam);
    quantityRangeScale = PolarScan_getRscale(scan);

    nAzim = quantitynAzim;
    nRang = quantitynRang;

    nGlobal = nAzim * nRang;
    
    // We use a neighborhood of 3x3, because in this function we are
    // interested in a cell's direct neighbors. See also comment at
    // variable definitions of 'nAzimNeighborhood' and 'nRangNeighborhood'
    // above. 
    nAzimNeighborhood_local = 3;
    nRangNeighborhood_local = 3;
    
    nNeighborhood = nAzimNeighborhood_local * nRangNeighborhood_local;
    nHalfNeighborhood = (nNeighborhood - 1)/2;


    if (scanParam != NULL) {
        quantityThres = (float) ((quantityThreshold - quantityValueOffset) / quantityValueScale);
    }

    cellImageInitialValue = -1;
    for (int iAzim = 0; iAzim < nAzim; iAzim++) {
        for (int iRang = 0; iRang < nRang; iRang++) {
            PolarScanParam_setValue(cellParam, iRang, iAzim, cellImageInitialValue);
        }
    }

    // If threshold value is equal to missing value, produce a warning
    if (quantityThres == quantityMissing) {
        fprintf(stderr,"Warning: in function findWeatherCells, quantityThres equals quantityMissing\n");
    }

    // ----------------------------------------------------------------------- //
    // Labeling of groups of connected pixels using horizontal, vertical, and  //
    // diagonal connections. The algorithm is described in 'Digital Image      //
    // Processing' by Gonzales and Woods published by Addison-Wesley.          //
    // ----------------------------------------------------------------------- //

    // The first cell will have iCellIdentifier = 2, because we reserve 1 for fringe
    // to be added by function fringeCells
    iCellIdentifier = 2;

    for (iAzim = 0; iAzim < nAzim; iAzim++) {
        for (iRang = 0; iRang < nRang; iRang++) {

            iGlobal = iRang + iAzim * nRang;

            if ((float)(iRang + 1) * quantityRangeScale > alldata->misc.rCellMax) {
                continue;
            }
            else {
                #ifdef FPRINTFON
                fprintf(stderr, "iGlobal = %d\niRang + 1 = %d\n"
                "quantityRangeScale = %f\n"
                "rCellMax = %f\n"
                "(iRang + 1) * quantityRangeScale = %f\n"
                "((iRang + 1) * quantityRangeScale > rCellMax) = %d\n"
                "dbg=%d\n",iGlobal,iRang + 1,quantityRangeScale,alldata->misc.rCellMax,
                (iRang + 1) * quantityRangeScale,
                ((iRang + 1) * quantityRangeScale > alldata->misc.rCellMax),dbg);
                
                dbg++;
                #endif
            }

            #ifdef FPRINTFON
            fprintf(stderr,"iGlobal = %d\n",iGlobal);
            #endif
            
            PolarScanParam_getValue(scanParam, iRang, iAzim, &quantityValueGlobal);
            PolarScanParam_getValue(cellParam, iRang, iAzim, &cellValueGlobal);


            if (quantityValueGlobal == quantityMissing || quantityValueGlobal == quantityUndetect) {

                #ifdef FPRINTFON
                fprintf(stderr,"quantityImage[%d] == quantityMissing\n",iGlobal);
                #endif

                continue;
            }
            
            if (selectAboveThreshold && quantityValueGlobal < (float) quantityThres){
                continue;
            }
            if (!selectAboveThreshold && quantityValueGlobal > (float) quantityThres){
                continue;
            }

            // count number of direct neighbors above threshold
            count = 0;
            for (iNeighborhood = 0; iNeighborhood < nNeighborhood; iNeighborhood++) {

                iLocal = findNearbyGateIndex(nAzim,nRang,iGlobal,nAzimNeighborhood_local,nRangNeighborhood_local,iNeighborhood,&iAzimLocal,&iRangLocal);

                if (iLocal < 0) {
                    // iLocal below zero are error codes
                    continue;
                }
                
                PolarScanParam_getValue(scanParam, iRangLocal, iAzimLocal, &quantityValueLocal);

                if (quantityValueLocal > quantityThres) {
                    count++;
                }

            }
            // when not enough qualified neighbors, continue
            if (count - 1 < alldata->constants.nNeighborsMin) {
                continue;
            }

            #ifdef FPRINTFON
            fprintf(stderr,"iGlobal = %d, count = %d\n",iGlobal,count);
            #endif


            // Looking for horizontal, vertical, forward diagonal, and backward diagonal connections.
            for (iNeighborhood = 0; iNeighborhood < nHalfNeighborhood; iNeighborhood++) {

                iLocal = findNearbyGateIndex(nAzim,nRang,iGlobal,nAzimNeighborhood_local,nRangNeighborhood_local,iNeighborhood,&iAzimLocal,&iRangLocal);
//                fprintf(stderr, "iLocal %i - args %i %i %i %i %i %i %i %i\n",iLocal,nAzim,nRang,iGlobal,nAzimNeighborhood_local,nRangNeighborhood_local,iNeighborhood,iAzimLocal,iRangLocal);
                if (iLocal < 0) {
                    // iLocal less than zero are error codes
                    continue;
                }
                
                PolarScanParam_getValue(cellParam, iRangLocal, iAzimLocal, &cellValueLocal);

                // no connection found, go to next pixel within neighborhood
                if ((int) cellValueLocal == cellImageInitialValue) {
                    continue;
                }

                // if pixel still unassigned, assign same iCellIdentifier as connection
                if ((int) cellValueGlobal == cellImageInitialValue) {
                    PolarScanParam_setValue(cellParam, iRang, iAzim, cellValueLocal);
                    cellValueGlobal = cellValueLocal;
                }
                else {
                    // if connection found but pixel is already assigned a different iCellIdentifier:
                    if (cellValueGlobal != cellValueLocal) {
                        // merging cells detected: replace all other occurences by value of connection:
                        for (iGlobalOther = 0; iGlobalOther < nGlobal; iGlobalOther++) {
                            if (cellParamData[iGlobalOther] == cellValueGlobal) {
                                cellParamData[iGlobalOther] = cellValueLocal;
                            }
                            // note: not all iCellIdentifier need to be used eventually
                        }
                    }
                }
            }

            // When no connections are found, give a new number.
            if ((int) cellValueGlobal == cellImageInitialValue) {

                #ifdef FPRINTFON
                fprintf(stderr, "new cell found...assigning number %d\n",iCellIdentifier);
                #endif
                
                PolarScanParam_setValue(cellParam, iRang, iAzim, iCellIdentifier);
                iCellIdentifier++;
            }

        } // (iRang=0; iRang<nRang; iRang++)
    } // for (iAzim=0; iAzim<nAzim; iAzim++)


    // check whether a cell crosses the border of the array (remember that iAzim=0 is
    // adjacent to iAzim=nAzim-1):
    iAzim = 0;

    for (iRang = 0; iRang < nRang; iRang++) {

        iGlobal = iRang + iAzim * nRang;
        // index 1 in a 3x3 child array refers to the cell that is a direct neighbor of
        // iGlobal, but on the other side of the array (because the polar plot is wrapped
        // in the azimuth dimension):
        iGlobalOther = findNearbyGateIndex(nAzim,nRang,iGlobal,3,3,1,&iAzimLocal,&iRangLocal);
        PolarScanParam_getValue(cellParam, iRangLocal, iAzimLocal, &cellValueOther);

        #ifdef FPRINTFON
        fprintf(stderr,"iGlobal = %d, iGlobalOther = %d\n",iGlobal,iGlobalOther);
        #endif

        cellIdentifierGlobal = cellValueGlobal;
        cellIdentifierGlobalOther = cellValueOther;
        if (cellIdentifierGlobal != cellImageInitialValue && cellIdentifierGlobalOther != cellImageInitialValue ) {
            // adjacent gates, both part of a cell -> assign them the same identifier, i.e. assign
            // all elements of cellImage that are equal to cellImage[iGlobalOther] the value of
            // cellImage[iGlobal]

            for (iGlobalInner = 0; iGlobalInner < nGlobal; iGlobalInner++) {
                if (cellParamData[iGlobalInner] == cellIdentifierGlobalOther) {
                    cellParamData[iGlobalInner] = cellIdentifierGlobal;
                }
            }
        }
    }

    // Returning number of detected cells (including fringe/clutter)
    nCells = iCellIdentifier;

    return nCells;
} // findWeatherCells



static int findNearbyGateIndex(const int nAzimParent, const int nRangParent, const int iParent,
                        const int nAzimChild,  const int nRangChild,  const int iChild, int *iAzimReturn, int *iRangReturn) {



    if (nRangChild%2 != 1) {

        #ifdef FPRINTFON
        fprintf(stderr, "nRangChild must be an odd integer number.\n");
        #endif

        return -1;
    }

    if (nAzimChild%2 != 1) {

        #ifdef FPRINTFON
        fprintf(stderr, "nAzimChild must be an odd integer number.\n");
        #endif

        return -2;
    }

    if (iChild > nAzimChild * nRangChild - 1) {

        #ifdef FPRINTFON
        fprintf(stderr, "iChild is outside the child window.\n");
        #endif

        return -3;
    }


    const int iAzimParent = iParent / nRangParent;
    const int iRangParent = iParent % nRangParent;

    const int iAzimChild = iChild / nRangChild;
    const int iRangChild = iChild % nRangChild;

    // the azimuth dimension is wrapped (polar plot); iAzim = 0 is adjacent to iAzim = nAzim-1:
    *iAzimReturn = (iAzimParent - nAzimChild/2 + iAzimChild + nAzimParent) % nAzimParent;
    *iRangReturn = iRangParent - nRangChild/2 + iRangChild;


    if (*iRangReturn > nRangParent - 1) {

        #ifdef FPRINTFON
        fprintf(stderr, "iChild is outside the parent array on the right-hand side.\n");
        #endif

        return -4;
    }
    if (*iRangReturn < 0) {

        #ifdef FPRINTFON
        fprintf(stderr, "iChild is outside the parent array on the left-hand side.\n");
        #endif

        return -5;
    }
    
    return *iAzimReturn * nRangParent + *iRangReturn;
    
} // findNearbyGateIndex


static void fringeCells(PolarScan_t* scan, vol2bird_t* alldata) {

    // -------------------------------------------------------------------------- //
    // This function enlarges cells in cellImage by an additional fringe.         //
    // First a block around each pixel is searched for pixels within a distance   //
    // equal to 'fringeDist'.                                                     //
    // -------------------------------------------------------------------------- //

    if(!PolarScan_hasParameter(scan, CELLNAME)){
        fprintf(stderr,"no CELL quantity in polar scan, aborting fringeCells()\n");
        return;
    }

    int nRang = (int) PolarScan_getNbins(scan);
    int nAzim = (int) PolarScan_getNrays(scan);
    float aScale = 360.0f/ nAzim;
    float rScale = (float) PolarScan_getRscale(scan);
    PolarScanParam_t *cellParam = PolarScan_getParameter(scan,CELLNAME);
    int *cellImage = (int *) PolarScanParam_getData(cellParam);

    int iRang;
    int iAzim;
    int iNeighborhood;
    int nNeighborhood;
    int iRangLocal;
    int iAzimLocal;
    int iLocal;
    int rBlock;
    int aBlock;
    int isEdge;
    int iGlobal;
    float theDist;
    int nAzimChild;
    int nRangChild;

    float actualRange;
    float circumferenceAtActualRange;


    rBlock = ROUND(alldata->constants.fringeDist / rScale);
    for (iAzim = 0; iAzim < nAzim; iAzim++) {
        for (iRang = 0; iRang < nRang; iRang++) {

            iGlobal = iRang + iAzim * nRang;

            if (cellImage[iGlobal] <= 1) {
                continue; // with the next iGlobal; already fringe or not in cellImage
            }

            // determine whether current pixel is a pixel on the edge of a cell
            isEdge = FALSE;
            for (iNeighborhood = 0; iNeighborhood < 9 && isEdge == FALSE; iNeighborhood++) {

                iLocal = findNearbyGateIndex(nAzim,nRang,iGlobal,3,3,iNeighborhood,&iAzimLocal,&iRangLocal);

                if (iLocal < 0) {
                    // iLocal less than zero are error codes
                    continue; // with the next iGlobal
                }

                if (cellImage[iLocal] < 1) { //FIXME: Strictly this should be <=1, but then much slower
                    isEdge = TRUE;           //Now only pixels without any bordering fringe are 'fringed', giving very similar result (but improvement welcome)
                }

            }
            
            if (isEdge == FALSE) {
                continue; // with the next iGlobal
            }

            actualRange = (iRang+0.5) * rScale;
            circumferenceAtActualRange = 2 * PI * actualRange;
            aBlock = (alldata->constants.fringeDist / circumferenceAtActualRange) * nAzim;


            #ifdef FPRINTFON
            fprintf(stderr, "actualRange = %f\n",actualRange);
            fprintf(stderr, "circumferenceAtActualRange = %f\n",circumferenceAtActualRange);
            fprintf(stderr, "fringeDist / circumferenceAtActualRange = %f\n",alldata->constants.fringeDist / circumferenceAtActualRange);
            fprintf(stderr, "aBlock = %d\n", aBlock);
            fprintf(stderr, "rBlock = %d\n", rBlock);
            #endif

            nAzimChild = 2 * aBlock + 1;
            nRangChild = 2 * rBlock + 1;
            nNeighborhood = nAzimChild * nRangChild;

            for (iNeighborhood = 0; iNeighborhood < nNeighborhood; iNeighborhood++) {

                iLocal = findNearbyGateIndex(nAzim,nRang,iGlobal,nAzimChild,nRangChild,iNeighborhood,&iAzimLocal,&iRangLocal);

                if (iLocal < 0) {
                    // iLocal less than zero are error codes
                    continue;
                }

//                iAzimLocal = iLocal / nRang;
//                iRangLocal = iLocal % nRang;

                // if not within range or already in cellImage or already a fringe, do nothing
                theDist = calcDist(iRang, iAzim, iRangLocal, iAzimLocal, rScale, aScale);
                if (theDist > alldata->constants.fringeDist || cellImage[iLocal] >= 1) {
                    continue; // with the next iGlobal
                }
                // include pixel (iRangLocal,iAzimLocal) in fringe
                cellImage[iLocal] = 1;
                
            } // (iNeighborhood = 0; iNeighborhood < nNeighborhood; iNeighborhood++)
        } // (iRang = 0; iRang < nRang; iRang++)
    } // (iAzim = 0; iAzim < nAzim; iAzim++)

    return;

} // fringeCells


CELLPROP* getCellProperties(PolarScan_t* scan, vol2birdScanUse_t scanUse, const int nCells, vol2bird_t* alldata){    
    int iCell;
    int iGlobal;
    int nGlobal;
    int iRang;
    int iAzim;
    long nAzim;
    long nRang;
    float nGatesValid;
    double dbzValue;
    double texValue = 0;
    double vradValue;
    double clutterValue = alldata->constants.clutterValueMin;
    double cellValue;
    
    PolarScanParam_t *dbzParam = PolarScan_getParameter(scan,scanUse.dbzName);
    PolarScanParam_t *vradParam = PolarScan_getParameter(scan,scanUse.vradName);
    PolarScanParam_t *texParam = PolarScan_getParameter(scan,scanUse.texName);
    PolarScanParam_t *clutParam = PolarScan_getParameter(scan,scanUse.clutName);
    PolarScanParam_t *cellParam = PolarScan_getParameter(scan,scanUse.cellName);

    nRang = PolarScan_getNbins(scan);
    nAzim = PolarScan_getNrays(scan);
    nGlobal = nAzim*nRang;
    
    // Allocating and initializing memory for cell properties.
    CELLPROP* cellProp;
    cellProp = (CELLPROP *)malloc(nCells*sizeof(CELLPROP));
    
    if (!cellProp) {
        fprintf(stderr,"Requested memory could not be allocated in getCellProperties!\n");
        return cellProp;
    }
    
    for (iCell = 0; iCell < nCells; iCell++) {
        cellProp[iCell].iRangOfMax = -1;
        cellProp[iCell].iAzimOfMax = -1;
        cellProp[iCell].nGates = 0;
        cellProp[iCell].nGatesClutter = 0;
        cellProp[iCell].dbzAvg = NAN;
        cellProp[iCell].texAvg = NAN;
        cellProp[iCell].dbzMax = NAN;
        cellProp[iCell].index = iCell;
        cellProp[iCell].drop = TRUE;
        cellProp[iCell].cv = NAN;
    }

    // Calculation of cell properties.
    RaveValueType typeDbz, typeVrad;
    for (iAzim = 0; iAzim < nAzim; iAzim++) {
        for (iRang = 0; iRang < nRang; iRang++) {

            iGlobal = iRang + iAzim * nRang;

            typeDbz=PolarScanParam_getConvertedValue(dbzParam, iRang, iAzim, &dbzValue);
            typeVrad=PolarScanParam_getConvertedValue(vradParam, iRang, iAzim, &vradValue);
            if (clutParam != NULL) PolarScanParam_getConvertedValue(clutParam, iRang, iAzim, &clutterValue);
            if (texParam != NULL) PolarScanParam_getConvertedValue(texParam, iRang, iAzim, &texValue);
            PolarScanParam_getConvertedValue(cellParam, iRang, iAzim, &cellValue);
	    
            iCell = (int) cellValue;

            // Note: this also throws out all nodata/undetect values for dbzValue
            if (iCell<0) {
                continue;
            }

            #ifdef FPRINTFON
            fprintf(stderr,"dbzValue = %f; vradValue = %f; clutterValue = %f; texValue = %f\n",dbzValue,vradValue,clutterValue,texValue);
            fprintf(stderr,"iGlobal = %d, iCell = %d\n",iGlobal,iCell);
            #endif

            cellProp[iCell].nGates += 1;
            cellProp[iCell].drop = FALSE;

            // low radial velocities are treated as clutter, not included in calculation cell properties
            if ((fabs(vradValue) < alldata->constants.vradMin) && (typeVrad == RaveValueType_DATA)){

                cellProp[iCell].nGatesClutter += 1;

                #ifdef FPRINTFON
                fprintf(stderr,"iGlobal = %d: vrad too low...treating as clutter\n",iGlobal);
                #endif

                continue;
            }

            if (typeVrad != RaveValueType_DATA || typeDbz != RaveValueType_DATA){

                cellProp[iCell].nGatesClutter += 1;

                continue;
            }


            // pixels in clutter map not included in calculation cell properties
            if (alldata->options.useStaticClutterData == TRUE){
                if (clutterValue > alldata->constants.clutterValueMin){
                    cellProp[iCell].nGatesClutter += 1;
                    continue;
                }
            }

            if (isnan(cellProp[iCell].dbzMax) || (dbzValue > cellProp[iCell].dbzMax)) {

                #ifdef FPRINTFON
                fprintf(stderr,"%d: new dbzMax value of %f found for this cell (%d).\n",iGlobal,dbzValue,iCell);
                #endif

                cellProp[iCell].dbzMax = dbzValue;
                cellProp[iCell].iRangOfMax = iGlobal%nRang;
                cellProp[iCell].iAzimOfMax = iGlobal/nRang;
            }

            if (isnan(cellProp[iCell].dbzAvg)) {
                cellProp[iCell].dbzAvg = dbzValue;
            } 
            else {
                cellProp[iCell].dbzAvg += dbzValue;
            }
            
            if (isnan(cellProp[iCell].texAvg)) {
                cellProp[iCell].texAvg = texValue;
            } 
            else {
                cellProp[iCell].texAvg += texValue;
            }
            
        } // for (iRang = 0; iRang < nRang; iRang++)
    } // for (iAzim = 0; iAzim < nAzim; iAzim++)


    for (iCell = 0; iCell < nCells; iCell++) {
        nGatesValid = cellProp[iCell].nGates - cellProp[iCell].nGatesClutter;
        if (nGatesValid > 0){
            cellProp[iCell].dbzAvg /= nGatesValid;
            cellProp[iCell].texAvg /= nGatesValid;
            cellProp[iCell].cv = cellProp[iCell].texAvg / cellProp[iCell].dbzAvg;
        }
    }
    
    return cellProp;
} // getCellProperties



static int getListOfSelectedGates(PolarScan_t* scan, vol2birdScanUse_t scanUse, const float altitudeMin, const float altitudeMax,
                           float* points_local, int iRowPoints, int nColsPoints_local, vol2bird_t* alldata) {

    // ------------------------------------------------------------------- //
    // Write combinations of an azimuth angle, an elevation angle, an      // 
    // observed vrad value, an observed dbz value, and a cell identifier   //
    // value into an external larger list.                                 //
    // ------------------------------------------------------------------- //

    int iAzim;
    int iRang;
    int iGlobal;
    int nRang;
    int nAzim;
    int nPointsWritten_local;

    float gateHeight;
    float gateRange;
    float gateAzim;
    float rangeScale;
    float azimuthScale;
    float elevAngle;
    float radarHeight;
    double vradValue;
    double dbzValue;
    double cellValue;
    double nyquist = 0;
    
    nPointsWritten_local = 0;
    
    RaveValueType vradValueType, dbzValueType;
    
    nRang = (int) PolarScan_getNbins(scan);
    nAzim = (int) PolarScan_getNrays(scan);
    rangeScale = (float) PolarScan_getRscale(scan);
    azimuthScale = 360.0f/nAzim;
    elevAngle = (float) PolarScan_getElangle(scan);
    radarHeight = (float) PolarScan_getHeight(scan);
    RaveAttribute_t* attr = PolarScan_getAttribute(scan, "how/NI");
    if (attr != (RaveAttribute_t *) NULL){ 
        RaveAttribute_getDouble(attr, &nyquist);
    }
    
    PolarScanParam_t* vradParam = PolarScan_getParameter(scan,scanUse.vradName);
    PolarScanParam_t* dbzParam = PolarScan_getParameter(scan,scanUse.dbzName);
    PolarScanParam_t* cellParam = PolarScan_getParameter(scan,scanUse.cellName);

    for (iRang = 0; iRang < nRang; iRang++) {

        // so gateRange represents a distance along the view direction (not necessarily horizontal)
        gateRange = ((float) iRang + 0.5f) * rangeScale;

        // note that "sin(elevAngle*DEG2RAD)" is equivalent to = "cos((90 - elevAngle)*DEG2RAD)":
        gateHeight = gateRange * (float) sin(elevAngle) + radarHeight;

        if (gateRange < alldata->options.rangeMin || gateRange > alldata->options.rangeMax) {
            // the current gate is either 
            // (1) too close to the radar; or
            // (2) too far away.
            continue;
        }
        if (gateHeight < altitudeMin || gateHeight > altitudeMax) {
            // if the height of the middle of the current gate is too far away from
            // the requested height, continue with the next gate
            continue;
        }

        // the gates at this range and elevation angle are within bounds,
        // include their data in the 'points' array:

        for (iAzim = 0; iAzim < nAzim; iAzim++) {

            iGlobal = iRang + iAzim * nRang;
            gateAzim = ((float) iAzim + 0.5f) * azimuthScale;
            vradValueType = PolarScanParam_getConvertedValue(vradParam, iRang, iAzim, &vradValue);
            dbzValueType = PolarScanParam_getConvertedValue(dbzParam, iRang, iAzim, &dbzValue);
            PolarScanParam_getValue(cellParam, iRang, iAzim, &cellValue);

            // in the points array, store missing reflectivity values as the lowest possible reflectivity
            // this is to treat undetects as absence of scatterers
            if (dbzValueType != RaveValueType_DATA){
                dbzValue = NAN;
            }

            // in the points array, store missing vrad values as NAN
            // this is necessary because different scans may have different missing values
            if (vradValueType != RaveValueType_DATA){
                vradValue = NAN;
            }

            // store the location as an azimuth angle, elevation angle combination
            points_local[iRowPoints * nColsPoints_local + 0] = gateAzim;
            points_local[iRowPoints * nColsPoints_local + 1] = elevAngle * RAD2DEG;

            // also store the dbz value --useful when estimating the bird density
            points_local[iRowPoints * nColsPoints_local + 2] = (float) dbzValue;
            
            // store the corresponding observed vrad value
            points_local[iRowPoints * nColsPoints_local + 3] = (float) vradValue;

            // store the corresponding cellImage value
            points_local[iRowPoints * nColsPoints_local + 4] = (float) cellValue;

            // set the gateCode to zero for now
            points_local[iRowPoints * nColsPoints_local + 5] = (float) 0;

            // store the corresponding observed nyquist velocity
            points_local[iRowPoints * nColsPoints_local + 6] = (float) nyquist;

            // store the corresponding observed vrad value for now (to be dealiased later)
            points_local[iRowPoints * nColsPoints_local + 7] = (float) vradValue;

            // raise the row counter by 1
            iRowPoints += 1;
            
            // raise number of points written by 1
            nPointsWritten_local += 1;

        }  //for iAzim
    } //for iRang

    return nPointsWritten_local;


} // getListOfSelectedGates


// adds a scan parameter to the scan
PolarScanParam_t* PolarScan_newParam(PolarScan_t *scan, const char *quantity, RaveDataType type){
    if (PolarScan_hasParameter(scan, quantity)){
        fprintf(stderr, "Parameter %s already exists in polar scan\n", quantity);
        return NULL;
    }

    PolarScanParam_t *scanParam = RAVE_OBJECT_NEW(&PolarScanParam_TYPE);
    
    if (scanParam == NULL){
        fprintf(stderr, "failed to allocate memory for new polar scan parameter\n");
        return NULL;
    }
    
    int result;
    result = PolarScanParam_createData(scanParam, PolarScan_getNbins(scan), PolarScan_getNrays(scan), type);
    result=PolarScanParam_setQuantity(scanParam,quantity);
    PolarScanParam_setNodata(scanParam,NODATA);
    PolarScanParam_setUndetect(scanParam,UNDETECT);
    PolarScanParam_setOffset(scanParam,0);
    PolarScanParam_setGain(scanParam,1);
    
    // initialize all values to NODATA
    double nodata = NODATA;
    for(int iRang = 0; iRang < PolarScan_getNbins(scan); iRang++){
        for(int iAzim = 0; iAzim < PolarScan_getNrays(scan); iAzim++){            
            PolarScanParam_setValue(scanParam, iRang, iAzim, nodata);
        }
    }

    PolarScan_addParameter(scan, scanParam);
    
 //   RAVE_OBJECT_RELEASE(scanParam);
    
 //   scanParam = PolarScan_getParameter(scan, quantity);
    
    return scanParam;
}


/*
int PolarVolume_dealias(PolarVolume_t* pvol){
    
    fprintf(stderr,"Dealiasing scans: ");
    
    int iScan, nScans;
    int result;
    PolarScan_t* scan;
    PolarScanParam_t* param, *param_clone, *param_dealiased;
    
    // Read number of scans
    nScans = PolarVolume_getNumberOfScans(pvol);
    
    for (iScan = 0; iScan < nScans; iScan++){
                        
        scan = PolarVolume_getScan(pvol, iScan);        

        // continue if already dealiased velocity available
        if (PolarScan_hasParameter(scan, "VRADDH")){
            continue;
        }

        // continue if no radial velocity present
        if (!(PolarScan_hasParameter(scan, "VRAD") || PolarScan_hasParameter(scan, "VRADH"))){
            continue;
        }
        
        // dealiase VRAD or VRADH quantities
        if (PolarScan_hasParameter(scan, "VRAD")){
            param = PolarScan_getParameter(scan, "VRAD");
            param_clone = RAVE_OBJECT_CLONE(param);
            //rename the parameter to VRADH and store it as a copy in the polar volume
            result = PolarScanParam_setQuantity (param_clone, "VRADH");
            //add the copy
            result = PolarScan_addParameter(scan, param_clone);
            //RAVE_OBJECT_RELEASE(param_clone);
        }
        else{
            param = PolarScan_getParameter(scan, "VRADH");
            param_clone = RAVE_OBJECT_CLONE(param);
            //rename the parameter to VRAD, as dealias function only works on VRAD quantity
            result = PolarScanParam_setQuantity (param_clone, "VRAD");
            //add a copy
            result = PolarScan_addParameter(scan, param_clone);
            //RAVE_OBJECT_RELEASE(param_clone);
        }
        
        // dealias the radial velocity parameter (stored as VRAD) of the scan
        result = dealias_scan_by_quantity(scan,"VRAD",90);
     
        // if dealiasing successful
        if (result == 1){
            // print to stderr that this scan was dealiased
            fprintf(stderr,"%i,",iScan+1);

            // remove and extract the dealiased VRAD parameter
            param_dealiased = PolarScan_removeParameter(scan, "VRAD");
            
            // rename the dealiased VRAD parameter to VRADDH
            result = PolarScanParam_setQuantity (param_dealiased, "VRADDH");
        
            // add the dealised VRADHD parameter to the scan
            result = PolarScan_addParameter(scan, param_dealiased);            
        }
        else{
            // remove the unsuccessfully dealiased VRAD parameter
            param_dealiased = PolarScan_removeParameter(scan, "VRAD");
            // and release it
            RAVE_OBJECT_RELEASE(param_dealiased);
        }
    }
    fprintf(stderr," done.\n");
    return 1;
}
*/

long datetime2long(char* date, char* time){

    //concatenate date and time into a datetime string
    char *datetime = malloc(strlen(date)+strlen(time)+1);
    long result;
    strcpy(datetime,date);
    strcat(datetime,time);

    //convert datetime string to a decimal long
    char *eptr;
    long ldatetime;
    ldatetime = strtol(datetime, &eptr, 10);

    // check for conversion errors
    if (ldatetime == 0){
        #ifdef FPRINTFON
            fprintf(stderr,"Conversion error occurred\n");
        #endif
        result = (long) NULL;
    }
    else{
        result = ldatetime;
    }
    
    free(datetime);
    
    return(result);
}

const char* PolarVolume_getStartDate(PolarVolume_t* pvol){
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    char* date = (char *) PolarVolume_getDate(pvol);
    char* time = (char *) PolarVolume_getTime(pvol);
    char* result = (char*) NULL;
    if(PolarVolume_getStartDateTime(pvol, &date, &time)==0){
        result = date;
    }
    return(result);  
}

const char* PolarVolume_getStartTime(PolarVolume_t* pvol){
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    char* date = (char *) PolarVolume_getDate(pvol);
    char* time = (char *) PolarVolume_getTime(pvol);
    char* result = (char*) NULL;
    if(PolarVolume_getStartDateTime(pvol, &date, &time)==0){
        result = time;
    }
    return(result);  
}

const char* PolarVolume_getEndDate(PolarVolume_t* pvol){
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    char* date = (char *) PolarVolume_getDate(pvol);
    char* time = (char *) PolarVolume_getTime(pvol);
    char* result = (char*) NULL;
    if(PolarVolume_getEndDateTime(pvol, &date, &time)==0){
        result = date;
    }
    return(result);  
}

const char* PolarVolume_getEndTime(PolarVolume_t* pvol){
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    char* date = (char *) PolarVolume_getDate(pvol);
    char* time = (char *) PolarVolume_getTime(pvol);
    char* result = (char*) NULL;
    if(PolarVolume_getEndDateTime(pvol, &date, &time)==0){
        result = time;
    }
    return(result);  
}

int PolarVolume_getStartDateTime(PolarVolume_t* pvol, char** StartDate, char** StartTime)
{
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    
    int result = -1;
    
    // Initialize datetimes
    long StartDateTime = 99999999999999;
    
    int nScans;
    char* date;
    char* time;

    // Read number of scans
    nScans = PolarVolume_getNumberOfScans(pvol);
    
    // find the start date and time
    for (int iScan = 0; iScan < nScans; iScan++)
    {
        PolarScan_t* scan = PolarVolume_getScan(pvol, iScan);
        // useScan is set to zero if the quantity is not found in the scan
        if (scan != (PolarScan_t *) NULL){
            date = (char *) PolarScan_getStartDate(scan);
            time = (char *) PolarScan_getStartTime(scan);

            long datetime = datetime2long(date, time);
            
            //continue if no valid datetime can be constructed
            if (datetime == (long) NULL){
                continue;
            }
            
            if (datetime < StartDateTime){
                StartDateTime = datetime;
                *StartDate = date;
                *StartTime = time;
                // success, we found a valid start date time
                result = 0;
            }
        }
    }
    
    return result;
}

int PolarVolume_getEndDateTime(PolarVolume_t* pvol, char** EndDate, char** EndTime)
{
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");

    int result = -1;

    // Initialize datetimes
    long EndDateTime = 00000000000000;
    
    int nScans;
    char* date;
    char* time;

    // Read number of scans
    nScans = PolarVolume_getNumberOfScans(pvol);
    
     // find the end date and time
    for (int iScan = 0; iScan < nScans; iScan++)
    {
        PolarScan_t* scan = PolarVolume_getScan(pvol, iScan);
        // useScan is set to zero if the quantity is not found in the scan
        if (scan != (PolarScan_t *) NULL){
            date = (char *) PolarScan_getEndDate(scan);
            time = (char *) PolarScan_getEndTime(scan);

            long datetime = datetime2long(date, time);
            
            //continue if no valid datetime can be constructed
            if ((date == NULL || time == NULL || datetime == (long) NULL)){
                continue;
            }
            
            if (datetime > EndDateTime){
                EndDateTime = datetime;
                *EndDate = date;
                *EndTime = time;
                // success, we found a valid start date time
                result = 0;
            }
        }
    }
    return result;
}


double PolarVolume_getWavelength(PolarVolume_t* pvol)
{
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    
    double value = 0;

    RaveAttribute_t* attr = PolarVolume_getAttribute(pvol, "how/wavelength");
    if (attr != (RaveAttribute_t *) NULL){
        RaveAttribute_getDouble(attr, &value);
    }
    else{
        // wavelength attribute was not found in the root /how attribute
        // check whether we can find it under /dataset1/how 
        PolarScan_t* scan = PolarVolume_getScan(pvol, 1);
        if (scan != (PolarScan_t *) NULL){
            attr = PolarScan_getAttribute(scan, "how/wavelength");
            if (attr != (RaveAttribute_t *) NULL){
                RaveAttribute_getDouble(attr, &value);
                fprintf(stderr, "Warning: using radar wavelength stored for scan 1 (%f cm) for all scans ...\n", value);
            }
        }
    }
    
    return value;
}

double PolarVolume_setWavelength(PolarVolume_t* pvol, double wavelength)
{
    RAVE_ASSERT((pvol != NULL), "pvol == NULL");
    
    double value = 0;

    RaveAttribute_t* attr = PolarVolume_getAttribute(pvol, "how/wavelength");
    if (attr != (RaveAttribute_t *) NULL){
        RaveAttribute_getDouble(attr, &value);
    }
    else{
        // wavelength attribute was not found in the root /how attribute
        // check whether we can find it under /dataset1/how 
        PolarScan_t* scan = PolarVolume_getScan(pvol, 1);
        if (scan != (PolarScan_t *) NULL){
            attr = PolarScan_getAttribute(scan, "how/wavelength");
            if (attr != (RaveAttribute_t *) NULL){
                RaveAttribute_getDouble(attr, &value);
                fprintf(stderr, "Warning: using radar wavelength stored for scan 1 (%f cm) for all scans ...\n", value);
            }
        }
    }
    
    return value;
}

#ifdef RSL

// copies a RSL sweep to a Rave scan
int rslCopy2Rave(Sweep *rslSweep,PolarScanParam_t* scanparam){
    float value;
    double setvalue;
    float rscale;
    int rayindex=0;
    Ray *rslRay;
    long nrays,nbins;
    
    rslRay = RSL_get_first_ray_of_sweep(rslSweep);
    
    if (rslRay == NULL) return 0;
    
    nbins = PolarScanParam_getNbins(scanparam);
    nrays = PolarScanParam_getNrays(scanparam);

    if (nbins == 0 || nrays == 0) return 0;

    for(int iRay=0; iRay<rslSweep->h.nrays; iRay++){
        // determine at which ray index we are in the rave scanparam
        // adding half a ray bin width, to get into the middle of the ray bin
        rayindex=ROUND(nrays*(rslRay->h.azimuth+180.0/nrays)/360.0);
        // get the range gate size for this ray
        rscale = rslRay->h.gate_size;
        // only values between 0 and 360 degrees permitted
        if (rayindex >= nrays) rayindex-=nrays;
        // loop over range bins
        int iBinStart = ROUND((rslRay->h.range_bin1 + 0.5*rscale)/rscale);
        for(int iBin=iBinStart; iBin<nbins; iBin++){
            value=RSL_get_value_from_ray(rslRay, iBin*rscale/1000);
            if (value == BADVAL || value == RFVAL){
                // BADVAL is used in RSL library to encode for undetects, but also for nodata
                // In most cases we are dealing with undetects, so encode as such
                // RFVAL is a range folded value, see RSL documentation.
                setvalue=PolarScanParam_getUndetect(scanparam);
            }
            else{
                setvalue=(value-PolarScanParam_getOffset(scanparam))/PolarScanParam_getGain(scanparam);
            }
            PolarScanParam_setValue(scanparam, iBin, rayindex, setvalue);
        }
        rslRay=RSL_get_next_cwise_ray(rslSweep, rslRay);
    }
    
    return 1;
}

PolarScanParam_t* PolarScanParam_RSL2Rave(Radar *radar, float elev, int RSL_INDEX,float rangeMax){
    Volume *rslVolume;
    Sweep *rslSweep;
    Ray *rslRay;
    PolarScanParam_t* param = NULL;
    
    float offset,gain;
    int nbins,nrays,rscale;
    
    char* DBZH="DBZH";
    char* VRADH="VRADH";
    char* RHOHV="RHOHV";
    char* WRADH="WRADH";
    char* TH="TH";
    char* ZDR="ZDR";
    char* PHIDP="PHIDP";
    char* KDP="KDP";
    char *name;

    if(radar == NULL) {
        fprintf(stderr, "Warning: RSL radar object is empty...\n");
        return param;
    }
    
    rslVolume = radar->v[RSL_INDEX];

    if(rslVolume == NULL) {
        fprintf(stderr, "Warning: RSL volume %i not found by PolarScanParam_RSL2Rave...\n",RSL_INDEX);
        return param;
    }

    rslSweep = RSL_get_sweep(rslVolume, elev);
    rslRay = RSL_get_first_ray_of_volume(rslVolume);
    
    if(ABS(rslSweep->h.elev-elev)>ELEVTOL){
        fprintf(stderr,"Warning: elevation angle mistmatch in PolarScanParam_RSL2Rave (requested %f, found %f)...\n",elev,rslSweep->h.elev);
        return param;
    }
    
    switch (RSL_INDEX) {
        case DZ_INDEX : 
            name = DBZH;
            offset = 1;
            gain = 1;
            break;
        case VR_INDEX :
            name = VRADH;
            offset = 1;
            gain = 1;
            break;        
        case RH_INDEX :
            name = RHOHV;
            offset = 1;
            gain = 1;
            break;
        case SW_INDEX :
            name = WRADH;
            offset = 1;
            gain = 1;
            break;
        case ZT_INDEX :
            name = TH;
            offset = 1;
            gain = 1;
            break;
        case DR_INDEX :
            name = ZDR;
            offset = 1;
            gain = 1;
            break;
        case PH_INDEX :
            name = PHIDP;
            offset = 1;
            gain = 1;
            break;
        case KD_INDEX :
            name = KDP;
            offset = 1;
            gain = 1;
            break;
        default :
            fprintf(stderr, "Something went wrong; RSL scan parameter not implemented in PolarScanParam_RSL2Rave\n");
            return param;

    }
 
    // get the number of range bins
    rscale = rslRay->h.gate_size;
    nbins = 1+rslRay->h.nbins+rslRay->h.range_bin1/rscale;
    nbins = MIN(nbins, ROUND(rangeMax/rscale));
        
    // estimate the number of azimuth bins
    // early WSR88D scans have somewhat variable azimuth spacing
    // therefore we reproject the data onto a regular azimuth grid
    // azimuth bin size is round off to 1/n with n positive integer
    // i.e. either 1, 0.5, 0.25 degrees etc.
    nrays = 360*ROUND((float) rslSweep->h.nrays/360.0);
    if (nrays != rslSweep->h.nrays){
        fprintf(stderr, "Warning: reprojecting %s sweep at elevation %f (%i rays into %i azimuth-bins) ...\n",name,elev,rslSweep->h.nrays,nrays);
    }

    param = RAVE_OBJECT_NEW(&PolarScanParam_TYPE);
    PolarScanParam_setQuantity(param, name);
    PolarScanParam_createData(param,nbins,nrays,RaveDataType_DOUBLE);
    PolarScanParam_setOffset(param,offset);
    PolarScanParam_setGain(param,gain);
    PolarScanParam_setNodata(param,RSL_NODATA);
    PolarScanParam_setUndetect(param,RSL_UNDETECT);

    // initialize the data field
    for(int iRay=0; iRay<nrays; iRay++){
        for(int iBin=0; iBin<nbins; iBin++){
            PolarScanParam_setValue(param, iBin, iRay, PolarScanParam_getNodata(param));
        }
    }

    // Fill the PolarScanParam_t objects with corresponding RSL data
    //XXXX Check that the encoding works correctly
    rslCopy2Rave(rslSweep,param);
    
    return param;
}


PolarScan_t* PolarScan_RSL2Rave(Radar *radar, int iScan, float rangeMax){
    
    PolarScanParam_t* param;
    PolarScan_t* scan = NULL;
    Volume *rslVol = NULL;
    Ray *rslRay = NULL;
    double elev = 0;
    double nyq_vel = 0;
    double rscale;
    
    if(radar == NULL) {
        fprintf(stderr, "Error: RSL radar object is empty...\n");
        return scan;
    }
    
    // get first RSL volume
    for (int iParam = 0; iParam < radar->h.nvolumes; iParam++){
        if(radar->v[iParam] == NULL) continue;
        rslVol = radar->v[iParam];    
        elev = rslVol->sweep[iScan]->h.elev;    
        break;
    }
    
    if(rslVol == NULL) {
        fprintf(stderr, "Error: RSL radar object is empty...\n");
        return scan;
    }

    if(iScan>rslVol->h.nsweeps-1) {
        fprintf(stderr, "Error: iScan larger than # sweeps...\n");
        return scan;
    }

    // all checks on sweep passed
    // start a new scan object
    scan = RAVE_OBJECT_NEW(&PolarScan_TYPE);
                
    // add attribute Elevation to scan
    PolarScan_setElangle(scan, (double) rslVol->sweep[iScan]->h.elev*PI/180);

    // add attribute Beamwidth to scan
    PolarScan_setBeamwidth(scan, (double) rslVol->sweep[iScan]->h.beam_width);

    // add attribute Nyquist velocity to scan
    rslRay = RSL_get_first_ray_of_volume(rslVol);
    nyq_vel = rslRay->h.nyq_vel;
    RaveAttribute_t* attr_NI = RaveAttributeHelp_createDouble("how/NI", (double) nyq_vel);
    if (attr_NI == NULL || nyq_vel == 0){
        fprintf(stderr, "warning: no valid Nyquist velocity found in RSL polar volume\n");
    }
    else{    
        PolarScan_addAttribute(scan, attr_NI);
    }

    // add range scale Atribute to scan
    rscale = rslRay->h.gate_size;
    PolarScan_setRscale(scan, rscale);

    // loop through the volume pointers
    // iParam gives you the XX_INDEX flag, i.e. scan parameter type
    int result;
    for (int iParam = 0; iParam < radar->h.nvolumes; iParam++){
        
        if(radar->v[iParam] == NULL) continue;
        
        param = PolarScanParam_RSL2Rave(radar, elev, iParam, rangeMax);
        result = PolarScan_addParameter(scan, param);
        
        if(result == 0){
           fprintf(stderr, "PolarScan_RSL2Rave failed to add parameter %i to RAVE polar scan\n",iParam); 
        }
        
    }

    return scan;
}

// maps a RSL polar volume to a RAVE polar volume NEW NEW NEW
PolarVolume_t* PolarVolume_RSL2Rave(Radar* radar, float rangeMax){
        
    // the RAVE polar volume to be returned by this function
    PolarVolume_t* volume = NULL;
    
    if(radar == NULL) {
        fprintf(stderr, "Error: RSL radar object is empty...\n");
        return volume;
    }

    // sort the scans (sweeps) and rays
    if(RSL_sort_radar(radar) == NULL) {
        fprintf(stderr, "Error: failed to sort RSL radar object...\n");
        goto done;
    }
    
    Volume *rslVol = NULL;
    Ray* rslRay = NULL;
    PolarScan_t* scan = NULL;

    // several checks that the volumes contain data -- should be outside this function!
    // should have a specific vol2bird version, and a rsl2odim version
    // this should be in vol2bird version only

    // find the first non-zero RSL volume
    for (int iParam = 0; iParam < radar->h.nvolumes; iParam++){
        if(radar->v[iParam] == NULL) continue;
        rslVol = radar->v[iParam];        
        break;
    }

    // find the largest shared maximum range
    float maxRange=FLT_MAX;
    float iRange;
    for (int iParam = 0; iParam < radar->h.nvolumes; iParam++){
        if(radar->v[iParam] == NULL) continue;
        rslRay = RSL_get_first_ray_of_volume(radar->v[iParam]);
        iRange=rslRay->h.range_bin1 + rslRay->h.nbins * rslRay->h.gate_size;
        if(iRange < maxRange) maxRange=iRange;
    }
    // if largest shared maximum range is larger than requested rangeMax, use the requested value
    if(rangeMax<maxRange) maxRange=rangeMax;

    // retrieve the first ray, for extracting some metadata
    rslRay = RSL_get_first_ray_of_volume(rslVol);
    if (rslRay == NULL){
        fprintf(stderr, "Error: RSL radar object contains no rays...\n");
        goto done;
    }
        
    // all checks on RSL object passed
    // make a new rave polar volume object
    volume = RAVE_OBJECT_NEW(&PolarVolume_TYPE);
    
    if (volume == NULL) {
        RAVE_CRITICAL0("Error: failed to create polarvolume instance");
        goto done;
    }

    // add attribute data to RAVE polar volume
    // first, copy metadata stored in radar header
    char pvtime[7];
    char pvdate[9];
    char *pvsource = malloc(strlen(radar->h.name)+strlen(radar->h.city)+strlen(radar->h.state)+strlen(radar->h.radar_name)+30);
    sprintf(pvtime, "%02i%02i%02i",radar->h.hour,radar->h.minute,ROUND(radar->h.sec));
    sprintf(pvdate, "%04i%02i%02i",radar->h.year,radar->h.month,radar->h.day);
    sprintf(pvsource, "RAD:%s,PLC:%s,state:%s,radar_name:%s",radar->h.name,radar->h.city,radar->h.state,radar->h.radar_name);
    fprintf(stderr,"Reading RSL polar volume with nominal time %s-%s, source: %s\n",pvdate,pvtime,pvsource);    
    PolarVolume_setTime(volume,pvtime);
    PolarVolume_setDate(volume,pvdate);
    PolarVolume_setSource(volume,pvsource);
    PolarVolume_setLongitude(volume,(double) (radar->h.lond + radar->h.lonm/60.0 + radar->h.lons/3600.0)*PI/180);
    PolarVolume_setLatitude(volume,(double) (radar->h.latd + radar->h.latm/60.0 + radar->h.lats/3600.0)*PI/180);
    PolarVolume_setHeight(volume, (double) radar->h.height);

    // second, copy metadata stored in ray header; assume attributes of first ray applies to entire volume
    float wavelength = rslRay->h.wavelength*100;
    RaveAttribute_t* attr_wavelength = RaveAttributeHelp_createDouble("how/wavelength", (double) wavelength);
    if (attr_wavelength == NULL && wavelength > 0){
        fprintf(stderr, "warning: no valid wavelength found in RSL polar volume\n");
    }
    else{    
        PolarVolume_addAttribute(volume, attr_wavelength);
    }
    
    // read the RSL scans (sweeps) and add them to RAVE polar volume
    int result;
    for (int iScan = 0; iScan < rslVol->h.nsweeps; iScan++){
        scan = PolarScan_RSL2Rave(radar, iScan, maxRange);
        // Add the scan to the volume
        result = PolarVolume_addScan(volume,scan);
        if(result == 0){
           fprintf(stderr, "PolarVolume_RSL2Rave failed to add RSL scan %i to RAVE polar volume\n",iScan); 
        }
    }
   
    free(pvsource);
    
    done:
        return volume;
}


// has extra checks in place ... XXX
PolarVolume_t* PolarVolume_vol2bird_RSL2Rave(Radar* radar, float rangeMax){
    // pointers to RSL polar volumes of reflectivity, velocity and correlation coefficient, respectively.
    Volume *rslVolZ,*rslVolV,*rslVolRho;
    // pointer to a RSL ray

    int dualpol;
    
    // retrieve the polar volumes from the Radar object
    rslVolZ = radar->v[DZ_INDEX];
    rslVolV = radar->v[VR_INDEX];
    rslVolRho = radar->v[RH_INDEX];
    
    PolarVolume_t* volume = NULL;
    
    // several checks that the volumes contain data
    if (rslVolZ == NULL){
        fprintf(stderr, "Error: RSL radar object contains no reflectivity volume...\n");
        goto done;
    }
    else if (rslVolV == NULL){
        fprintf(stderr, "Error: RSL radar object contains no radial velocity volume...\n");
        goto done;
    }
    if (rslVolRho){
        dualpol=TRUE;
    }
    
    volume = PolarVolume_RSL2Rave(radar, rangeMax);
    
    done:
        return volume;
}



// maps a RSL polar volume to a RAVE polar volume OLD OLD OLD
PolarVolume_t* PolarVolume_RSL2RaveOLD(Radar* radar, float rangeMax){
    
    // the RAVE polar volume to be returned by this function
    PolarVolume_t* volume = NULL;
    
    // flag indicating whether we are dealing with dual-pol data
    int dualpol = FALSE;

    if(radar == NULL) {
        fprintf(stderr, "Error: RSL radar object is empty...\n");
        goto done;
    }

    // sort the scans (sweeps) and rays
    if(RSL_sort_radar(radar) == NULL) {
        fprintf(stderr, "Error: failed to sort RSL radar object...\n");
        goto done;
    }
    
    // pointers to RSL polar volumes of reflectivity, velocity and correlation coefficient, respectively.
    Volume *rslVolZ,*rslVolV,*rslVolRho;
    // pointer to a RSL ray
    Ray* rslRay;
    
    // retrieve the polar volumes from the Radar object
    rslVolZ = radar->v[DZ_INDEX];
    rslVolV = radar->v[VR_INDEX];
    rslVolRho = radar->v[RH_INDEX];
    
    // several checks that the volumes contain data
    if (rslVolZ == NULL){
        fprintf(stderr, "Error: RSL radar object contains no reflectivity volume...\n");
        goto done;
    }
    else if (rslVolV == NULL){
        fprintf(stderr, "Error: RSL radar object contains no radial velocity volume...\n");
        goto done;
    }
    if (rslVolRho){
        dualpol=TRUE;
    }
    
    // retrieve the first ray, for extracting some metadata
    rslRay = RSL_get_first_ray_of_volume(rslVolZ);
    if (rslRay == NULL){
        fprintf(stderr, "Error: RSL radar object contains no rays...\n");
        goto done;
    }
    
    // all checks on RSL object passed
    // make a new rave polar volume object
    volume = RAVE_OBJECT_NEW(&PolarVolume_TYPE);
    
    if (volume == NULL) {
        RAVE_CRITICAL0("Error: failed to create polarvolume instance");
        goto done;
    }

    // add attribute data to RAVE polar volume
    // first, copy metadata stored in radar header
    char pvtime[7];
    char pvdate[9];
    char *pvsource = malloc(strlen(radar->h.name)+strlen(radar->h.city)+strlen(radar->h.state)+strlen(radar->h.radar_name)+30);
    sprintf(pvtime, "%02i%02i%02i",radar->h.hour,radar->h.minute,ROUND(radar->h.sec));
    sprintf(pvdate, "%04i%02i%02i",radar->h.year,radar->h.month,radar->h.day);
    sprintf(pvsource, "RAD:%s,PLC:%s,state:%s,radar_name:%s",radar->h.name,radar->h.city,radar->h.state,radar->h.radar_name);
    fprintf(stderr,"Reading RSL polar volume with nominal time %s-%s, source: %s\n",pvdate,pvtime,pvsource);    
    PolarVolume_setTime(volume,pvtime);
    PolarVolume_setDate(volume,pvdate);
    PolarVolume_setSource(volume,pvsource);
    PolarVolume_setLongitude(volume,(double) (radar->h.lond + radar->h.lonm/60.0 + radar->h.lons/3600.0)*PI/180);
    PolarVolume_setLatitude(volume,(double) (radar->h.latd + radar->h.latm/60.0 + radar->h.lats/3600.0)*PI/180);
    PolarVolume_setHeight(volume, (double) radar->h.height);

    // second, copy metadata stored in ray header; assume attributes of first ray applies to entire volume
    float wavelength = rslRay->h.wavelength*100;
    RaveAttribute_t* attr_wavelength = RaveAttributeHelp_createDouble("how/wavelength", (double) wavelength);
    if (attr_wavelength == NULL && wavelength > 0){
        fprintf(stderr, "warning: no valid wavelength found in RSL polar volume\n");
    }
    else{    
        PolarVolume_addAttribute(volume, attr_wavelength);
    }

    // optional print which scans (sweeps) will be read for this RSL file
    #ifdef FPRINTFON
    for (int iVol=0; iVol<MAX_RADAR_VOLUMES; iVol++) {
        Sweep* rslSweep;
        rslSweep = radar->v[iVol]->sweep[0];
        rslRay   = rslSweep->ray[0];
        fprintf(stderr, "Scans to be read from RSL file:\n");
        if (rslSweep && rslRay) {
            for(int iScan=0; iScan<radar->v[iVol]->h.nsweeps; iScan++){
                rslSweep = radar->v[iVol]->sweep[iScan];
                rslRay   = rslSweep->ray[0];
                sprintf(pvtime, "%02i%02i%02i",rslRay->h.hour,rslRay->h.minute,ROUND(rslRay->h.sec));
                sprintf(pvdate, "%04i%02i%02i",rslRay->h.year,rslRay->h.month,rslRay->h.day);

                fprintf(stderr, "%s-%s,%s,elev=%f,vol=%i,scan=%i,nrays=%i,nbins=%i,rscale=%i\n",pvdate,pvtime,radar->v[iVol]->h.type_str,rslSweep->h.elev,iVol,iScan,rslSweep->h.nrays, rslRay->h.nbins,rslRay->h.gate_size);
            }
        }
    }
    #endif
    
    // read the RSL scans (sweeps) and add them to RAVE polar volume
    for (int iScan = 0; iScan < rslVolZ->h.nsweeps; iScan++){
        Sweep *rslSweepZ, *rslSweepV, *rslSweepRho;
        Ray *rslRayZ, *rslRayV, *rslRayRho;
        PolarScan_t* scan;
        float elev,nyq_vel;
        int nrays,nbins,rscale;
        
        // retrieve the reflectivity sweep
        rslSweepZ = rslVolZ->sweep[iScan];
        elev = rslSweepZ->h.elev;
        // retrieve radial velocity and correlation coeffient sweeps at the same elevation
        rslSweepV = RSL_get_sweep(rslVolV, elev);
        rslSweepRho = RSL_get_sweep(rslVolRho, elev);

        // retrieve first rays of the reflectivity, radial velocity
        // and correlation coeffient volumes, respectively        
        rslRayZ = RSL_get_first_ray_of_sweep(rslSweepZ);
        rslRayV = RSL_get_first_ray_of_sweep(rslSweepV);
        rslRayRho = RSL_get_first_ray_of_sweep(rslSweepRho);
        
        // check that we have data and that the elevations of the sweeps of each quantity match
        if(rslSweepV == NULL){
            fprintf(stderr,"Warning: no velocity (VRAD) data found, dropping sweep %i...\n",iScan);
            continue;
        }
        if(ABS(rslSweepV->h.elev-elev)>ELEVTOL){
            fprintf(stderr,"Warning: elevation angle of radial velocity scan does not match reflectivity scan, dropping sweep %i...\n",iScan);
            continue;
        }
        if(rslRayZ == NULL){
            fprintf(stderr,"Warning: no reflectivity ray data found, dropping sweep %i...\n",iScan);
            continue;
        }
        if(rslRayV == NULL){
            fprintf(stderr,"Warning: no radial velocity ray data found, dropping sweep %i...\n",iScan);
            continue;
        }
        if(dualpol){
            if(rslSweepRho == NULL){
                fprintf(stderr,"Warning: no correlation coefficient (RhoHV) data found, dropping sweep %i...\n",iScan);
                continue;
            }
            if(rslRayRho == NULL){
                fprintf(stderr,"Warning: no correlation coefficient ray data found, dropping sweep %i...\n",iScan);
                continue;
            }
            if(ABS(rslSweepRho->h.elev-elev)>ELEVTOL){
                fprintf(stderr,"Warning: elevation angle of correlation coefficient (RhoHV) scan does not match reflectivity scan, dropping sweep %i...\n",iScan);
                continue;
            }
        }

        // all checks on sweep passed
        // start a new scan object
        scan = RAVE_OBJECT_NEW(&PolarScan_TYPE);
                
        // add attribute Elevation to scan
        PolarScan_setElangle(scan, (double) elev*PI/180);

        // add attribute Beamwidth to scan
        PolarScan_setBeamwidth(scan, (double) rslSweepZ->h.beam_width);

        // add attribute Nyquist velocity to scan
        nyq_vel = rslRayV->h.nyq_vel;
        RaveAttribute_t* attr_NI = RaveAttributeHelp_createDouble("how/NI", (double) nyq_vel);
        if (attr_NI == NULL && nyq_vel > 0){
            fprintf(stderr, "warning: no valid Nyquist velocity found in RSL polar volume\n");
        }
        else{    
            PolarScan_addAttribute(scan, attr_NI);
        }

        // add range scale Atribute to scan
        rscale = rslRayZ->h.gate_size;
        PolarScan_setRscale(scan, rscale);

        // get the number of range bins
        nbins = 1+rslRayZ->h.nbins+rslRayZ->h.range_bin1/rscale;
        nbins = MIN(nbins, ROUND(rangeMax/rscale));
        
        // estimate the number of azimuth bins
        // early WSR88D scans have somewhat variable azimuth spacing
        // therefore we reproject the data onto a regular azimuth grid
        // azimuth bin size is round off to 1/n with n positive integer
        // i.e. either 1, 0.5, 0.25 degrees etc.
        nrays = 360*ROUND((float) rslSweepZ->h.nrays/360.0);
        if (nrays != rslSweepZ->h.nrays){
            fprintf(stderr, "Warning: reprojecting DBZH sweep %i (%i rays into %i azimuth-bins) ...\n", iScan,rslSweepZ->h.nrays,nrays);
        }
        if (nrays != rslSweepV->h.nrays){
            fprintf(stderr, "Warning: reprojecting VRADH sweep %i (%i rays into %i azimuth-bins) ...\n", iScan,rslSweepZ->h.nrays,nrays);
        }
        if (dualpol && nrays != rslSweepRho->h.nrays){
            fprintf(stderr, "Warning: reprojecting RHOHV sweep %i (%i rays into %i azimuth-bins) ...\n", iScan,rslSweepZ->h.nrays,nrays);
        }

        // make new PolarScanParam_t objects with attributes
        PolarScanParam_t *scanparamZ, *scanparamV, *scanparamRho;
        scanparamZ = RAVE_OBJECT_NEW(&PolarScanParam_TYPE);
        scanparamV = RAVE_OBJECT_NEW(&PolarScanParam_TYPE);
        scanparamRho = RAVE_OBJECT_NEW(&PolarScanParam_TYPE);

        // set the attributes needed for encoding
        PolarScanParam_setQuantity(scanparamZ, "DBZH");
        PolarScanParam_setQuantity(scanparamV, "VRADH");
        PolarScanParam_createData(scanparamZ,nbins,nrays,RaveDataType_DOUBLE);
        PolarScanParam_createData(scanparamV,nbins,nrays,RaveDataType_DOUBLE);
        PolarScanParam_setOffset(scanparamZ,RSL_OFFSET_DBZ);
        PolarScanParam_setOffset(scanparamV,RSL_OFFSET_VRAD);
        PolarScanParam_setGain(scanparamZ,RSL_GAIN_DBZ);
        PolarScanParam_setGain(scanparamV,RSL_GAIN_VRAD);
        PolarScanParam_setNodata(scanparamZ,RSL_NODATA);
        PolarScanParam_setNodata(scanparamV,RSL_NODATA);
        PolarScanParam_setUndetect(scanparamZ,RSL_UNDETECT);
        PolarScanParam_setUndetect(scanparamV,RSL_UNDETECT);
        if (dualpol){
            PolarScanParam_createData(scanparamRho,nbins,nrays,RaveDataType_DOUBLE);
            PolarScanParam_setQuantity(scanparamRho, "RHOHV");
            PolarScanParam_setOffset(scanparamRho,RSL_OFFSET_RHOHV);
            PolarScanParam_setGain(scanparamRho,RSL_GAIN_RHOHV);
            PolarScanParam_setNodata(scanparamRho,RSL_NODATA);
            PolarScanParam_setUndetect(scanparamRho,RSL_UNDETECT);
        }
        
        // initialize the data fields
        for(int iRay=0; iRay<nrays; iRay++){
            for(int iBin=0; iBin<nbins; iBin++){
                PolarScanParam_setValue(scanparamZ, iBin, iRay, PolarScanParam_getNodata(scanparamZ));
                PolarScanParam_setValue(scanparamV, iBin, iRay, PolarScanParam_getNodata(scanparamV));
                PolarScanParam_setValue(scanparamRho, iBin, iRay, PolarScanParam_getNodata(scanparamRho));
            }
        }
        
        // Fill the PolarScanParam_t objects with corresponding RSL data
        rslCopy2Rave(rslSweepZ,scanparamZ);
        rslCopy2Rave(rslSweepV,scanparamV);
        if (dualpol){
            rslCopy2Rave(rslSweepRho,scanparamRho);
        }
               
        // Add the scan parameters to the scan
        PolarScan_addParameter(scan, scanparamZ);
        PolarScan_addParameter(scan, scanparamV);
        if(dualpol){
            PolarScan_addParameter(scan, scanparamRho);
        }
                
        // Add the scan to the volume
        PolarVolume_addScan(volume,scan);
        
        // clean up before continuing with next scan/sweep
        RAVE_OBJECT_RELEASE(scan);
        RAVE_OBJECT_RELEASE(scanparamZ);
        RAVE_OBJECT_RELEASE(scanparamV);
        RAVE_OBJECT_RELEASE(scanparamRho);
        RAVE_OBJECT_RELEASE(attr_NI);
    }
    
    free(pvsource);
    
    done:
        return volume;
}
#endif

static int hasAzimuthGap(const float* points_local, const int nPoints, vol2bird_t* alldata) {

    int hasGap;
    int nObs[alldata->constants.nBinsGap];
    int iPoint;
    int iBinGap;
    int iBinGapNext;
    float azimuth;

    hasGap = FALSE;

    // Initialize histogram
    for (iBinGap = 0; iBinGap < alldata->constants.nBinsGap; iBinGap++) {
        nObs[iBinGap] = 0;
    }

    // Collect histogram data
    for (iPoint = 0; iPoint < nPoints; iPoint++) {
        azimuth = points_local[iPoint*alldata->misc.nDims];
        iBinGap = ((int) floor((azimuth / 360.0) * alldata->constants.nBinsGap)) % alldata->constants.nBinsGap;
        nObs[iBinGap]++;
    }

    // Detect adjacent bins in which the number of azimuth observations 
    // is less than the minimum required number
    for (iBinGap = 0; iBinGap < alldata->constants.nBinsGap; iBinGap++) {
        
        iBinGapNext = (iBinGap + 1) % alldata->constants.nBinsGap;
        
        if (nObs[iBinGap] < alldata->constants.nObsGapMin && nObs[iBinGapNext] < alldata->constants.nObsGapMin) {
            hasGap = TRUE;
        }
    }

    return hasGap;
    
} // hasAzimuthGap






static int includeGate(const int iProfileType, const int iQuantityType, const unsigned int gateCode, vol2bird_t* alldata) {
    
    int doInclude = TRUE;
    
    if (gateCode & 1<<(alldata->flags.flagPositionStaticClutter)) {
        
        // i.e. flag 0 in gateCode is true
        // this gate is true in the static clutter map (which we don't have yet TODO)
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
    
    if (gateCode & 1<<(alldata->flags.flagPositionDynamicClutter)) {
        
        // i.e. flag 1 in gateCode is true
        // this gate is part of the cluttermap (without fringe)
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                break;
            case 3 : 
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
        
    if (gateCode & 1<<(alldata->flags.flagPositionDynamicClutterFringe)) {
        
        // i.e. flag 2 in gateCode is true
        // this gate is part of the fringe of the cluttermap
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
    
    if (iQuantityType && (gateCode & 1<<(alldata->flags.flagPositionVradMissing))) {
        
        // i.e. flag 3 in gateCode is true
        // this gate has reflectivity data but no corresponding radial velocity data
        // and iQuantityType != 0, i.e. we are not dealing with reflectivity quantities
        
        switch (iProfileType) {
            case 1 : 
		doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }

    if (!iQuantityType && (gateCode & 1<<(alldata->flags.flagPositionVradMissing)) && alldata->options.requireVrad) {
        
        // i.e. flag 3 in gateCode is true
        // this gate has reflectivity data but no corresponding radial velocity data
        // and iQuantityType == 0, i.e. we are dealing with reflectivity quantities
        // and requireVrad is true, i.e. we exclude gates without radial velocity data
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
    
    if (gateCode & 1<<(alldata->flags.flagPositionDbzTooHighForBirds)) {
        
        // i.e. flag 4 in gateCode is true
        // this gate's dbz value is too high to be due to birds, it must be 
        // caused by something else

        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                break;
            case 3 : 
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
    
    if (gateCode & 1<<(alldata->flags.flagPositionVradTooLow)) {
        
        // i.e. flag 5 in gateCode is true
        // this gate's radial velocity is very low, and therefore excluded as potential clutter.
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }
    
    if (iQuantityType && (gateCode & 1<<(alldata->flags.flagPositionVDifMax))) {

        // i.e. iQuantityType !=0, we are dealing with a selection for svdfit.
        // i.e. flag 6 in gateCode is true
        // after the first svdfit, this gate's fitted vRad was more than 
        // VDIFMAX away from the observed vRad for that gate. It is therefore
        // considered an outlier
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }



    if (!iQuantityType && (gateCode & 1<<(alldata->flags.flagPositionAzimTooLow))) {

        // i.e. iQuantityType == 0, we are NOT dealing with a selection for svdfit, but with a selection of reflectivities.
	// Azimuth selection does not apply to svdfit, because svdfit requires data at all azimuths
        // i.e. flag 7 in gateCode is true
        // the user can specify to exclude gates based on their azimuth;
        // this clause is for gates that have too low azimuth
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }


    if (!iQuantityType && (gateCode & 1<<(alldata->flags.flagPositionAzimTooHigh))) {

        // i.e. iQuantityType == 0, we are NOT dealing with a selection for svdfit, but with a selection of reflectivities.
	// Azimuth selection does not apply to svdfit, because svdfit requires data at all azimuths
        // i.e. flag 8 in gateCode is true
        // the user can specify to exclude gates based on their azimuth;
        // this clause is for gates that have too high azimuth
        
        switch (iProfileType) {
            case 1 : 
                doInclude = FALSE;
                break;
            case 2 : 
                doInclude = FALSE;
                break;
            case 3 : 
                doInclude = FALSE;
                break;
            default :
                fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
        }
    }



    return doInclude;

} // includeGate





static int readUserConfigOptions(cfg_t** cfg) {


    cfg_opt_t opts[] = {
        CFG_FLOAT("HLAYER",HLAYER, CFGF_NONE),
        CFG_INT("NLAYER",NLAYER, CFGF_NONE),
        CFG_FLOAT("RANGEMIN",RANGEMIN, CFGF_NONE),
        CFG_FLOAT("RANGEMAX",RANGEMAX, CFGF_NONE),
        CFG_FLOAT("AZIMMIN",AZIMMIN, CFGF_NONE),
        CFG_FLOAT("AZIMMAX",AZIMMAX, CFGF_NONE),
        CFG_FLOAT("ELEVMIN",ELEVMIN, CFGF_NONE),
        CFG_FLOAT("ELEVMAX",ELEVMAX, CFGF_NONE),
        CFG_FLOAT("RADAR_WAVELENGTH_CM",RADAR_WAVELENGTH_CM,CFGF_NONE),
        CFG_BOOL("USE_STATIC_CLUTTER_DATA",USE_STATIC_CLUTTER_DATA,CFGF_NONE),
        CFG_BOOL("VERBOSE_OUTPUT_REQUIRED",VERBOSE_OUTPUT_REQUIRED,CFGF_NONE),
        CFG_BOOL("PRINT_DBZ",PRINT_DBZ,CFGF_NONE),
        CFG_BOOL("PRINT_DEALIAS",PRINT_DEALIAS,CFGF_NONE),
        CFG_BOOL("PRINT_VRAD",PRINT_VRAD,CFGF_NONE),
        CFG_BOOL("PRINT_RHOHV",PRINT_RHOHV,CFGF_NONE),
        CFG_BOOL("PRINT_CELL",PRINT_CELL,CFGF_NONE),
        CFG_BOOL("PRINT_CELL_PROP",PRINT_CELL_PROP,CFGF_NONE),
        CFG_BOOL("PRINT_TEXTURE",PRINT_TEXTURE,CFGF_NONE),
        CFG_BOOL("PRINT_CLUT",PRINT_CLUT,CFGF_NONE),
        CFG_BOOL("PRINT_OPTIONS",PRINT_OPTIONS,CFGF_NONE),
        CFG_BOOL("FIT_VRAD",FIT_VRAD,CFGF_NONE),
        CFG_BOOL("PRINT_PROFILE",PRINT_PROFILE,CFGF_NONE),
        CFG_BOOL("PRINT_POINTS_ARRAY",PRINT_POINTS_ARRAY,CFGF_NONE),
        CFG_FLOAT("MIN_NYQUIST_VELOCITY",MIN_NYQUIST_VELOCITY,CFGF_NONE),
        CFG_FLOAT("MAX_NYQUIST_DEALIAS",MAX_NYQUIST_DEALIAS,CFGF_NONE),
        CFG_FLOAT("STDEV_BIRD",STDEV_BIRD,CFGF_NONE),
        CFG_FLOAT("STDEV_CELL",STDEV_CELL,CFGF_NONE),
        CFG_FLOAT("SIGMA_BIRD",SIGMA_BIRD,CFGF_NONE),
        CFG_FLOAT("ETAMAX",ETAMAX,CFGF_NONE),
        CFG_FLOAT("ETACELL",ETACELL,CFGF_NONE),
        CFG_STR("DBZTYPE",DBZTYPE,CFGF_NONE),
        CFG_BOOL("REQUIRE_VRAD",REQUIRE_VRAD,CFGF_NONE),
        CFG_BOOL("DEALIAS_VRAD",DEALIAS_VRAD,CFGF_NONE),
		CFG_BOOL("DEALIAS_RECYCLE",DEALIAS_RECYCLE,CFGF_NONE),
        CFG_BOOL("EXPORT_BIRD_PROFILE_AS_JSON",FALSE,CFGF_NONE),
        CFG_BOOL("DUALPOL",DUALPOL,CFGF_NONE),
        CFG_FLOAT("DBZMIN",DBZMIN,CFGF_NONE),
        CFG_FLOAT("RHOHVMIN",RHOHVMIN,CFGF_NONE),
        CFG_END()
    };
    
    (*cfg) = cfg_init(opts, CFGF_NONE);
    
    if (cfg_parse((*cfg), "options.conf") == CFG_PARSE_ERROR) {
        return 1;
    }
   
    return 0;

} // readUserConfigOptions
    



// copies shared metadata from rave polar volume to rave vertical profile
static int mapVolumeToProfile(VerticalProfile_t* vp, PolarVolume_t* volume){
    //assert that the volume and vertical profile are defined
    RAVE_ASSERT((vp != NULL), "vp == NULL");
    RAVE_ASSERT((volume != NULL), "volume == NULL");
    
    //copy the metadata
    VerticalProfile_setTime(vp,PolarVolume_getTime(volume));
    VerticalProfile_setDate(vp,PolarVolume_getDate(volume));
    VerticalProfile_setSource(vp,PolarVolume_getSource(volume));
    VerticalProfile_setLongitude(vp,PolarVolume_getLongitude(volume));
    VerticalProfile_setLatitude(vp,PolarVolume_getLatitude(volume));
    VerticalProfile_setHeight(vp,PolarVolume_getHeight(volume));
   
    return 0;
}

int mapDataToRave(PolarVolume_t* volume, vol2bird_t* alldata) {
    int result = 0;
    //assert that the vertical profile is defined
    RAVE_ASSERT((alldata->vp != NULL), "vp == NULL");
    
    // copy shared metadata from volume to profile
    mapVolumeToProfile(alldata->vp, volume);

    // copy vol2bird profile data to RAVE profile
    VerticalProfile_setLevels(alldata->vp,alldata->options.nLayers);
    VerticalProfile_setInterval(alldata->vp,alldata->options.layerThickness);
    VerticalProfile_setMinheight(alldata->vp, 0);
    VerticalProfile_setMaxheight(alldata->vp, alldata->options.nLayers * alldata->options.layerThickness);
    
    //intialize attributes for /how
    RaveAttribute_t* attr_beamwidth = RaveAttributeHelp_createDouble("how/beamwidth", PolarVolume_getBeamwidth(volume)*180/PI);
    RaveAttribute_t* attr_wavelength = RaveAttributeHelp_createDouble("how/wavelength", alldata->options.radarWavelength);
    RaveAttribute_t* attr_rcs_bird = RaveAttributeHelp_createDouble("how/rcs_bird", alldata->options.birdRadarCrossSection);
    RaveAttribute_t* attr_sd_vvp_thresh = RaveAttributeHelp_createDouble("how/sd_vvp_thresh", alldata->options.stdDevMinBird);
    RaveAttribute_t* attr_dealiased = RaveAttributeHelp_createLong("how/dealiased", alldata->options.dealiasVrad);
    RaveAttribute_t* attr_task = RaveAttributeHelp_createString("how/task", PROGRAM);
    RaveAttribute_t* attr_task_version = RaveAttributeHelp_createString("how/task_version", VERSION);
    RaveAttribute_t* attr_task_args = RaveAttributeHelp_createString("how/task_args", alldata->misc.task_args);
    RaveAttribute_t* attr_comment = RaveAttributeHelp_createString("how/comment", "");
    RaveAttribute_t* attr_minrange = RaveAttributeHelp_createDouble("how/minrange", alldata->options.rangeMin/1000);
    RaveAttribute_t* attr_maxrange = RaveAttributeHelp_createDouble("how/maxrange", alldata->options.rangeMax/1000);
    RaveAttribute_t* attr_minazim = RaveAttributeHelp_createDouble("how/minazim", alldata->options.azimMin);
    RaveAttribute_t* attr_maxazim = RaveAttributeHelp_createDouble("how/maxazim", alldata->options.azimMax);
    RaveAttribute_t* attr_cluttermap = RaveAttributeHelp_createString("how/clutterMap", "");

    //add /how attributes to the vertical profile object
    VerticalProfile_addAttribute(alldata->vp, attr_beamwidth);
    VerticalProfile_addAttribute(alldata->vp, attr_wavelength);
    VerticalProfile_addAttribute(alldata->vp, attr_rcs_bird);
    VerticalProfile_addAttribute(alldata->vp, attr_sd_vvp_thresh);
    VerticalProfile_addAttribute(alldata->vp, attr_dealiased);
    VerticalProfile_addAttribute(alldata->vp, attr_task);
    VerticalProfile_addAttribute(alldata->vp, attr_task_version);
    VerticalProfile_addAttribute(alldata->vp, attr_task_args);
    VerticalProfile_addAttribute(alldata->vp, attr_comment);
    VerticalProfile_addAttribute(alldata->vp, attr_minrange);
    VerticalProfile_addAttribute(alldata->vp, attr_maxrange);
    VerticalProfile_addAttribute(alldata->vp, attr_minazim);
    VerticalProfile_addAttribute(alldata->vp, attr_maxazim);
    VerticalProfile_addAttribute(alldata->vp, attr_cluttermap);
      
    //-------------------------------------------//
    //   map the profile data to rave fields     //
    //-------------------------------------------//
    
    //layer specification:
    profileArray2RaveField(alldata, 1, 0, "HGHT", RaveDataType_DOUBLE);

    //bird-specific quantities:
    profileArray2RaveField(alldata, 1, 5, "ff", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 6, "dd", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 2, "u", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 3, "v", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 4, "w", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 8, "gap", RaveDataType_INT);
    profileArray2RaveField(alldata, 1, 9, "dbz", RaveDataType_DOUBLE);    
    profileArray2RaveField(alldata, 1, 11, "eta", RaveDataType_DOUBLE);
    profileArray2RaveField(alldata, 1, 12, "dens", RaveDataType_DOUBLE);        
    profileArray2RaveField(alldata, 1, 10, "n", RaveDataType_LONG);
    profileArray2RaveField(alldata, 1, 13, "n_dbz", RaveDataType_LONG);    

    //quantities calculated from all scatterers:
    profileArray2RaveField(alldata, 3, 7, "sd_vvp", RaveDataType_DOUBLE);    
    profileArray2RaveField(alldata, 3, 9, alldata->options.dbzType, RaveDataType_DOUBLE);    
    profileArray2RaveField(alldata, 3, 10, "n_all", RaveDataType_LONG);    
    profileArray2RaveField(alldata, 3, 13, "n_dbz_all", RaveDataType_LONG);        

    //some unused quantities for later reference:
    //profileArray2RaveField(alldata, 1, 2, "u", RaveDataType_DOUBLE);
    //profileArray2RaveField(alldata, 1, 3, "v", RaveDataType_DOUBLE);
  
     //initialize start and end date attributes to the vertical profile object
    RaveAttribute_t* attr_startdate = RaveAttributeHelp_createString("how/startdate", PolarVolume_getStartDate(volume));
    RaveAttribute_t* attr_starttime = RaveAttributeHelp_createString("how/starttime", PolarVolume_getStartTime(volume));
    RaveAttribute_t* attr_enddate = RaveAttributeHelp_createString("how/enddate", PolarVolume_getEndDate(volume));
    RaveAttribute_t* attr_endtime = RaveAttributeHelp_createString("how/endtime", PolarVolume_getEndTime(volume));

    //add the start and end date attributes to the vertical profile object
    VerticalProfile_addAttribute(alldata->vp, attr_startdate);
    VerticalProfile_addAttribute(alldata->vp, attr_starttime);
    VerticalProfile_addAttribute(alldata->vp, attr_enddate);
    VerticalProfile_addAttribute(alldata->vp, attr_endtime);
  
    RAVE_OBJECT_RELEASE(attr_beamwidth);
    RAVE_OBJECT_RELEASE(attr_wavelength);
    RAVE_OBJECT_RELEASE(attr_rcs_bird);
    RAVE_OBJECT_RELEASE(attr_sd_vvp_thresh);
    RAVE_OBJECT_RELEASE(attr_dealiased);
    RAVE_OBJECT_RELEASE(attr_task);
    RAVE_OBJECT_RELEASE(attr_task_version);
    RAVE_OBJECT_RELEASE(attr_task_args);
    RAVE_OBJECT_RELEASE(attr_comment);
    RAVE_OBJECT_RELEASE(attr_minrange);
    RAVE_OBJECT_RELEASE(attr_maxrange);
    RAVE_OBJECT_RELEASE(attr_minazim);
    RAVE_OBJECT_RELEASE(attr_maxazim);
    RAVE_OBJECT_RELEASE(attr_cluttermap);

    RAVE_OBJECT_RELEASE(attr_startdate);
    RAVE_OBJECT_RELEASE(attr_starttime);
    RAVE_OBJECT_RELEASE(attr_enddate);
    RAVE_OBJECT_RELEASE(attr_endtime);

    result=1;

    return result;
    
}



// this function replaces NODATA and UNDETECT float values to NA and NAN
float nanify(float value){
    float output = value;
    if(value == NODATA) output = NAN;
    if(value == UNDETECT) output = NAN;
    return output;
} // nanify




static int profileArray2RaveField(vol2bird_t* alldata, int idx_profile, int idx_quantity, const char* quantity, RaveDataType raveType){
    int result = 0;
    float* profile;
    
    RaveField_t* field = RAVE_OBJECT_NEW(&RaveField_TYPE);
    if (RaveField_createData(field, 1, alldata->options.nLayers, raveType) == 0){
        fprintf(stderr,"Error pre-allocating field '%s'.\n", quantity); 
        return -1;
    }
    
    switch (idx_profile) {
        case 1 :
            profile=alldata->profiles.profile1;
            break; 
        case 2 :
            profile=alldata->profiles.profile2;
            break; 
        case 3 :
            profile=alldata->profiles.profile3;
            break;
        default:
            fprintf(stderr, "Something is wrong this should not happen.\n");
            break;
    }
    
    int iRowProfile;
    int nColProfile = alldata->profiles.nColsProfile;
    for (iRowProfile = 0; iRowProfile < alldata->profiles.nRowsProfile; iRowProfile++) {
        RaveField_setValue(field, 0, iRowProfile, profile[idx_quantity +iRowProfile * nColProfile]);
    }
    
    result = verticalProfile_AddCustomField(alldata->vp, field, quantity);
    
    RAVE_OBJECT_RELEASE(field);
    
    return result;
}


static int verticalProfile_AddCustomField(VerticalProfile_t* self, RaveField_t* field, const char* quantity)
{
    int result = 0;
    RAVE_ASSERT((self != NULL), "self == NULL");
    RaveAttribute_t* attr = RaveAttributeHelp_createString("what/quantity", quantity);
    RaveAttribute_t* attr_gain = RaveAttributeHelp_createDouble("what/gain", 1.0);
    RaveAttribute_t* attr_offset = RaveAttributeHelp_createDouble("what/offset", 0.0);
    RaveAttribute_t* attr_nodata = RaveAttributeHelp_createDouble("what/nodata", NODATA);
    RaveAttribute_t* attr_undetect = RaveAttributeHelp_createDouble("what/undetect", UNDETECT);

    if (attr == NULL || !RaveField_addAttribute(field, attr)) {
        RAVE_ERROR0("Failed to add what/quantity attribute to field");
        goto done;
    }
    if (attr_gain == NULL || !RaveField_addAttribute(field, attr_gain)) {
        RAVE_ERROR0("Failed to add what/gain attribute to field");
        goto done;
    }
    if (attr_offset == NULL || !RaveField_addAttribute(field, attr_offset)) {
        RAVE_ERROR0("Failed to add what/offset attribute to field");
        goto done;
    }
    if (attr_nodata == NULL || !RaveField_addAttribute(field, attr_nodata)) {
        RAVE_ERROR0("Failed to add what/nodata attribute to field");
        goto done;
    }
    if (attr_undetect == NULL || !RaveField_addAttribute(field, attr_undetect)) {
        RAVE_ERROR0("Failed to add what/undetect attribute to field");
        goto done;
    }
    result = VerticalProfile_addField(self, field);
    
    done:
        RAVE_OBJECT_RELEASE(attr);
        RAVE_OBJECT_RELEASE(attr_gain);
        RAVE_OBJECT_RELEASE(attr_offset);
        RAVE_OBJECT_RELEASE(attr_nodata);
        RAVE_OBJECT_RELEASE(attr_undetect);
        return result;
}


int saveToODIM(RaveCoreObject* object, const char* filename){
    
    //define new Rave IO instance
    RaveIO_t* raveio = RAVE_OBJECT_NEW(&RaveIO_TYPE);
    //VpOdimIO_t* raveio = RAVE_OBJECT_NEW(&VpOdimIO_TYPE);

    //set the object to be saved
    RaveIO_setObject(raveio, object);
    
    //save the object
    int result;
    result = RaveIO_save(raveio, filename);
    
    RAVE_OBJECT_RELEASE(raveio);

    return result;    
}


static void printCellProp(CELLPROP* cellProp, float elev, int nCells, int nCellsValid, vol2bird_t *alldata){
    
    // ---------------------------------------------------------- //
    // this function prints the cell properties struct to stderr  //
    // ---------------------------------------------------------- //
    
    fprintf(stderr,"#Cell analysis for elevation %f:\n",elev*RAD2DEG);
    fprintf(stderr,"#Minimum cell area in pixels   : %i\n",alldata->constants.nGatesCellMin);
    fprintf(stderr,"#Threshold for mean dBZ cell   : %g dBZ\n",alldata->misc.cellDbzMin);
    fprintf(stderr,"#Threshold for mean stdev cell : %g dBZ\n",alldata->options.cellStdDevMax);
    fprintf(stderr,"#Valid cells                   : %i/%i\n#\n",nCellsValid,nCells);
    fprintf(stderr,"cellProp: .index .nGates .nGatesClutter .dbzAvg .texAvg .cv   .dbzMax .iRangOfMax .iAzimOfMax .drop\n");
    for (int iCell = 0; iCell < nCells; iCell++) {
        if (cellProp[iCell].drop == TRUE) {
            continue;
        }
        fprintf(stderr,"cellProp: %6d %7d %14d %7.2f %7.2f %5.2f %7.2f %11d %11d %5c\n",
            cellProp[iCell].index,
            cellProp[iCell].nGates,
            cellProp[iCell].nGatesClutter,
            cellProp[iCell].dbzAvg,
            cellProp[iCell].texAvg,
            cellProp[iCell].cv,
            cellProp[iCell].dbzMax,
            cellProp[iCell].iRangOfMax,
            cellProp[iCell].iAzimOfMax,
            cellProp[iCell].drop == TRUE ? 'T' : 'F');
    }
}


static void printGateCode(char* flags, const unsigned int gateCode) {
    
    // --------------------------------------------------- //
    // this function prints the integer value gateCode as  //
    // the equivalent sequence of bits                     //
    // --------------------------------------------------- //

    int iFlag;
    int nFlagsNeeded;
    int nFlags;
    int nFlagsMax;
    
    
    if (gateCode <= 0) {
        nFlagsNeeded = 0;
    }
    else {
        nFlagsNeeded = (int) ceil(log(gateCode + 1)/log(2));
    }
    
    nFlagsMax = 9;
    if (nFlagsNeeded > nFlagsMax) {
        fprintf(stderr,"There's only space for %d flags\n. Aborting",nFlagsMax);
        return;
    }
    
    nFlags = nFlagsMax;

    for (iFlag = nFlags-1; iFlag >= 0; iFlag--) {
    
        int iFlagIsEnabled = (gateCode & (1 << iFlag)) >> iFlag;
        
        if (iFlagIsEnabled == TRUE) {
            flags[nFlags-iFlag-1] = '1';
        }
        else {
            flags[nFlags-iFlag-1] = '0';
        };
    }
    
    flags[nFlags] = '\0';
    
    return;

} // printGateCode


void printImage(PolarScan_t* scan, const char* quantity) {
    int nAzim = PolarScan_getNrays(scan);
    int nRang = PolarScan_getNbins(scan);
    int iRang;
    int iAzim;
    int iGlobal;
    int needsSignChar;
    int needsFloat;
    int maxValue;
    
    PolarScanParam_t* scanParam = PolarScan_getParameter(scan, quantity);
    
    if(scanParam == (PolarScanParam_t *) NULL){
        fprintf(stderr,"warning::printImage: quantity %s not found in scan\n",quantity);
        return;
    }

    double thisValue;
    int nChars;
    char* formatStr;
    char* naStr;
    
    iGlobal = 0;
    maxValue = 0;
    needsSignChar = FALSE;
    needsFloat = FALSE;
    
    RaveValueType type;
    
    // first, determine how many characters are needed to print array 'imageInt'
    for (iAzim = 0; iAzim < nAzim; iAzim++) { 
        
        for (iRang = 0; iRang < nRang; iRang++) {
            
            type = PolarScanParam_getValue(scanParam,iRang,iAzim,&thisValue);
            if (thisValue < 0) {
                needsSignChar = TRUE;
            }
            
            if (XABS(thisValue - (long) thisValue) >= 0.01) {
                needsFloat = TRUE;
            }

            thisValue = XABS(thisValue);

            if (thisValue > maxValue) {
                maxValue = thisValue;
            }
        }
    }


    nChars = (int) ceil(log(maxValue + 1)/log(10));

    if (needsSignChar) {
        nChars += 1;
    }
    
    if (!needsFloat){
        switch (nChars) {
            case 0 :
                formatStr = "  %1f";
                naStr=" NA";
                break; 
            case 1 :
                formatStr = "  %1f";
                naStr=" NA";
                break; 
            case 2 :
                formatStr = " %2f"; 
                naStr=" NA";
                break;
            case 3 :
                formatStr = " %3f"; 
                naStr=" NA ";
                break;
            case 4 :
                formatStr = " %4f"; 
                naStr=" NA  ";
                break;
            default :
                formatStr = " %8f"; 
                naStr=" NA      ";
                
        }        
    }
    else{
        switch (nChars) {
            case 0 :
                formatStr = " %1.2f";
                naStr=" NA  ";
                break; 
            case 1 :
                formatStr = " %1.2f";
                naStr=" NA  ";
                break; 
            case 2 :
                formatStr = " %2.2f"; 
                naStr=" NA   ";
                break;
            case 3 :
                formatStr = " %3.2f"; 
                naStr=" NA    ";
                break;
            case 4 :
                formatStr = " %4.2f"; 
                naStr=" NA     ";
                break;
            default :
                formatStr = " %8.2f"; 
                naStr=" NA         ";

        }
    }

    
    for (iAzim = 0; iAzim < nAzim; iAzim++) { 
        
        for (iRang = 0; iRang < nRang; iRang++) {
                
            type = PolarScanParam_getValue(scanParam,iRang,iAzim,&thisValue);
            
            if (type == RaveValueType_DATA){
                fprintf(stderr,formatStr,thisValue);
            }
            else{
                fprintf(stderr,naStr,thisValue);

            }
        }
        fprintf(stderr,"\n");
    }
        
} // printImageInt




static int printMeta(PolarScan_t* scan, const char* quantity) {
    
    fprintf(stderr,"scan->heig = %f\n",PolarScan_getHeight(scan));
    fprintf(stderr,"scan->elev = %f\n",PolarScan_getElangle(scan));
    fprintf(stderr,"scan->nRang = %ld\n",PolarScan_getNbins(scan));
    fprintf(stderr,"scan->nAzim = %ld\n",PolarScan_getNrays(scan));
    fprintf(stderr,"scan->rangeScale = %f\n",PolarScan_getRscale(scan));
    fprintf(stderr,"scan->azimScale = %f\n",360.0f/PolarScan_getNrays(scan));
    
    PolarScanParam_t* scanParam = PolarScan_getParameter(scan,quantity);
    
    if(scanParam != (PolarScanParam_t*) NULL){
        fprintf(stderr,"scan->%s->valueOffset = %f\n",quantity,PolarScanParam_getOffset(scanParam));
        fprintf(stderr,"scan->%s->valueScale = %f\n",quantity,PolarScanParam_getGain(scanParam));
        fprintf(stderr,"scan->%s->nodata = %f\n",quantity,PolarScanParam_getNodata(scanParam));
        fprintf(stderr,"scan->%s->undetect = %f\n",quantity,PolarScanParam_getUndetect(scanParam));
    }
    
    return 0;

} // printMeta




static int selectCellsToDrop(CELLPROP *cellProp, int nCells, vol2bird_t* alldata){
    int output = 0;
    if(alldata->options.dualPol){
        output = selectCellsToDrop_dualPol(cellProp, nCells, alldata);
    }
    else{
        output = selectCellsToDrop_singlePol(cellProp, nCells, alldata);
    }
    return output;
}



static int selectCellsToDrop_singlePol(CELLPROP *cellProp, int nCells, vol2bird_t* alldata){
    // determine which blobs to drop from map based on low mean dBZ / high stdev /
    // small area / high percentage clutter
    for (int iCell = 0; iCell < nCells; iCell++) {
        int notEnoughGates = cellProp[iCell].nGates < alldata->constants.nGatesCellMin;
        int dbzTooLow = cellProp[iCell].dbzAvg < alldata->misc.cellDbzMin;
        int texTooHigh = cellProp[iCell].texAvg > alldata->options.cellStdDevMax;
        int tooMuchClutter = ((float) cellProp[iCell].nGatesClutter / cellProp[iCell].nGates) > alldata->constants.cellClutterFractionMax;
        
        if (notEnoughGates) {
            
            // this blob is too small too be a weather cell, more likely 
            // that these are birds. So, drop the blob from the record of 
            // weather cells 

            cellProp[iCell].drop = TRUE;
            
            continue;
        }

        if (dbzTooLow && texTooHigh) {

            // apparently, we are dealing a blob that is fairly large (it
            // passes the 'notEnoughGates' condition above, but it has both 
            // a low dbz and a high vrad texture. In contrast, weather cells
            // have high dbz and low vrad texture. It is therefore unlikely
            // that the blob is a weather cell.
            
            if (tooMuchClutter) {
                // pass
            }
            else {
                
                // So at this point we have established that we are 
                // dealing with a blob that is:
                //     1. fairly large
                //     2. has too low dbz to be precipitation
                //     3. has too high vrad texture to be precipitation
                //     4. has too little clutter to be attributed to clutter
                // Therefore we drop it from the record of known weather cells
                
                cellProp[iCell].drop = TRUE;
                
            }
        }
    }
    return 1;
}


static int selectCellsToDrop_dualPol(CELLPROP *cellProp, int nCells, vol2bird_t* alldata){
    // determine which blobs to drop from map based on small area
    for (int iCell = 0; iCell < nCells; iCell++) {
        int notEnoughGates = cellProp[iCell].nGates < alldata->constants.nGatesCellMin;
        
        if (notEnoughGates) {
            
            // this blob is too small too be a weather cell, more likely 
            // that these are birds. So, drop the blob from the record of 
            // weather cells 

            cellProp[iCell].drop = TRUE;
            
            continue;
        }
    }
    return 1;
}


static void sortCellsByArea(CELLPROP *cellProp, const int nCells) {

    // ---------------------------------------------------------------- //
    // Sorting of the cell properties based on cell area.               //
    // ---------------------------------------------------------------- // 

    int iCell;
    int iCellOther;
    CELLPROP tmp;

    //Sorting of data elements using straight insertion method.
    for (iCell = 1; iCell < nCells; iCell++) {

        tmp = cellProp[iCell];

        iCellOther = iCell - 1;

        while (iCellOther >= 0 && cellProp[iCellOther].nGates < tmp.nGates) {

            cellProp[iCellOther + 1] = cellProp[iCellOther];

            iCellOther--;
        }

        cellProp[iCellOther + 1] = tmp;

    } //for iCell

    return;
} // sortCellsByArea




static int removeDroppedCells(CELLPROP *cellProp, const int nCells) {
    int iCell;
    int iCopy;
    int nCopied;
    CELLPROP* cellPropCopy;
    CELLPROP cellPropEmpty;
    
    cellPropEmpty.iRangOfMax = -1;
    cellPropEmpty.iAzimOfMax = -1;
    cellPropEmpty.nGates = -1;
    cellPropEmpty.nGatesClutter = -1;
    cellPropEmpty.dbzAvg = 0.0f;
    cellPropEmpty.texAvg = 0.0f;
    cellPropEmpty.dbzMax = 0.0f;
    cellPropEmpty.index = -1;
    cellPropEmpty.drop = TRUE;
    cellPropEmpty.cv = 0.0f;



    #ifdef FPRINTFON
    for (iCell = 0; iCell < nCells; iCell++) {
        fprintf(stderr,"(%d/%d): index = %d, nGates = %d\n",iCell,nCells,cellProp[iCell].index,cellProp[iCell].nGates);
    }
    fprintf(stderr,"end of list\n");
    #endif


    
    cellPropCopy = (CELLPROP*) malloc(sizeof(CELLPROP) * nCells);
    if (!cellPropCopy) {
        fprintf(stderr,"Requested memory could not be allocated in removeDroppedCells!\n");
        return -1;
    }    

    iCopy = 0;
    
    for (iCell = 0; iCell < nCells; iCell++) {    
        
        if (cellProp[iCell].drop == TRUE) {
            // pass
        }  
        else {
            cellPropCopy[iCopy] = cellProp[iCell];
            iCopy += 1;
        }
    }
    
    nCopied = iCopy;
    
    for (iCopy = 0; iCopy < nCopied; iCopy++) {
        cellProp[iCopy] = cellPropCopy[iCopy];
    }

    for (iCell = nCopied; iCell < nCells; iCell++) {
        cellProp[iCell] = cellPropEmpty;
    }

    #ifdef FPRINTFON
    for (iCell = 0; iCell < nCells; iCell++) {
        fprintf(stderr,"(%d/%d): copied = %c, index = %d, nGates = %d\n",iCell,nCells,iCell < nCopied ? 'T':'F',cellProp[iCell].index,cellProp[iCell].nGates);
    }
    #endif 
    
    free(cellPropCopy);
       
    return nCopied;
   
}




static void updateFlagFieldsInPointsArray(const float* yObs, const float* yFitted, const int* includedIndex, 
                                   const int nPointsIncluded, float* points_local, vol2bird_t* alldata) {
                                       
    // ----------------------------------------------------------------------------------- //
    // after the first svdfit to the selection of points, we want to identify gates that   //
    // deviate strongly from the fitted vrad value (we assume that they are outliers). So, //
    // this function marks the corresponding points in 'points' by enabling the            //
    // 'flagPositionVDifMax' bit in 'gateCode', such that outliers will be rejected during //
    // the second svdfit iteration                                                         //
    // ----------------------------------------------------------------------------------- //

    int iPointIncluded;
    int iPoint;
    unsigned int gateCode;

    for (iPointIncluded = 0; iPointIncluded < nPointsIncluded; iPointIncluded++) {

        float absVDif = fabs(yObs[iPointIncluded]-yFitted[iPointIncluded]);
        
        if (absVDif > alldata->constants.absVDifMax) {
            
            iPoint = includedIndex[iPointIncluded];
            gateCode = (unsigned int) points_local[iPoint * alldata->points.nColsPoints + alldata->points.gateCodeCol];
            points_local[iPoint * alldata->points.nColsPoints + alldata->points.gateCodeCol] = (float) (gateCode |= 1<<(alldata->flags.flagPositionVDifMax));

        }
    } 

} // updateFlagFieldsInPointsArray



static int updateMap(PolarScan_t* scan, CELLPROP *cellProp, const int nCells, vol2bird_t* alldata) {

    // ------------------------------------------------------------------------- //
    // This function updates cellImage by dropping cells and reindexing the map. //
    // Leaving index 0 unused, will be used for assigning cell fringes           //
    // ------------------------------------------------------------------------- //

    int iGlobal;
    int iCell;
    int iCellNew;
    int nCellsValid;
    int cellImageValue;

    PolarScanParam_t *cellParam = PolarScan_getParameter(scan, CELLNAME);
    int* cellImage = (int *) PolarScanParam_getData(cellParam);

    int nGlobal = PolarScanParam_getNbins(cellParam) * PolarScanParam_getNrays(cellParam);

    #ifdef FPRINTFON
    int minValue = cellImage[0];
    int maxValue = cellImage[0];
    for (iGlobal = 1;iGlobal < nGlobal;iGlobal++) {
        if (cellImage[iGlobal] < minValue) {
            minValue = cellImage[iGlobal];
        }
        if (cellImage[iGlobal] > maxValue) {
            maxValue = cellImage[iGlobal];
        }
    }
    fprintf(stderr,"minimum value in cellImage array = %d.\n", minValue);
    fprintf(stderr,"maximum value in cellImage array = %d.\n", maxValue);
    #endif

    nCellsValid = nCells;

    for (iGlobal = 0; iGlobal < nGlobal; iGlobal++) {

        if (cellImage[iGlobal] == -1) {
            continue;
        }

        cellImageValue = cellImage[iGlobal];

        if (cellImageValue > nCells - 1) {
            fprintf(stderr, "You just asked for the properties of cell %d, which does not exist.\n", cellImageValue);
            continue;
        }

        if (cellProp[cellImageValue].drop == TRUE) {
            cellImage[iGlobal] = -1;
        }
    }

    // label small cells so that 'removeDroppedCells()' will remove them (below)
    for (iCell = 0; iCell < nCells; iCell++) {
        if (cellProp[iCell].nGates < alldata->constants.nGatesCellMin) {
            cellProp[iCell].drop = TRUE;
        }
    }

    // remove all cell that have .drop == TRUE
    nCellsValid = removeDroppedCells(&cellProp[0],nCells);

    // sort the cells by area
    sortCellsByArea(&cellProp[0],nCells);


    #ifdef FPRINTFON
    fprintf(stderr,"nCellsValid = %d\n",nCellsValid);
    fprintf(stderr,"\n");
    #endif

    // replace the values in cellImage with newly calculated index values:
    for (iCell = 0; iCell < nCells; iCell++) {

        if (iCell < nCellsValid) {
            iCellNew = -1 * (iCell + 2 + 100);
        }
        else {
            iCellNew = -1;
        }

        #ifdef FPRINTFON
        fprintf(stderr,"before: cellProp[%d].index = %d.\n",iCell,cellProp[iCell].index);
        fprintf(stderr,"before: cellProp[%d].nGates = %d.\n",iCell,cellProp[iCell].nGates);
        fprintf(stderr,"before: iCell = %d.\n",iCell);
        fprintf(stderr,"before: iCellNew = %d.\n",iCellNew);
        fprintf(stderr,"\n");
        #endif

        for (iGlobal = 0; iGlobal < nGlobal; iGlobal++) {
            if (cellImage[iGlobal] == cellProp[iCell].index) {
                cellImage[iGlobal] = iCellNew;
            }
        }
        // have the indices in cellProp match the re-numbering
        cellProp[iCell].index = iCellNew;

    } // (iCell = 0; iCell < nCells; iCell++)


    // once you've re-numbered everything, flip the sign back and
    // remove the offset of 100...
    for (iGlobal = 0; iGlobal < nGlobal; iGlobal++) {
        if (cellImage[iGlobal] == -1) {
            // do nothing
        }
        else {
            cellImage[iGlobal] = (-1 * cellImage[iGlobal]) - 100;
        }
    }
    // ...and make sure the indices in cellProp match that change
    for (iCell = 0; iCell < nCells; iCell++) {

        if (cellProp[iCell].index == -1) {
            // do nothing
        }
        else {
            cellProp[iCell].index = (-1 * cellProp[iCell].index) - 100;
        }

        #ifdef FPRINTFON
        fprintf(stderr,"after: cellProp[%d].index = %d.\n",iCell,cellProp[iCell].index);
        fprintf(stderr,"after: cellProp[%d].nGates = %d.\n",iCell,cellProp[iCell].nGates);
        fprintf(stderr,"\n");
        #endif
    }

    return nCellsValid;
} // updateMap



void vol2birdCalcProfiles(vol2bird_t* alldata) {

    int nPasses;
    int iPoint;
    int iLayer;
    int iPass;
    int iProfileType;

    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }
    
    // calculate the profiles in reverse order, because you need the result 
    // of iProfileType == 3 in order to check whether chi < stdDevMinBird  
    // when calculating iProfileType == 1
    for (iProfileType = alldata->profiles.nProfileTypes; iProfileType > 0; iProfileType--) {

        // ------------------------------------------------------------- //
        //                        prepare the profile                    //
		//                                                               //
		// iProfileType == 1: birds                                      //
		// iProfileType == 2: non-birds                                  //
		// iProfileType == 3: birds+non-birds                            //
        // ------------------------------------------------------------- //

        // FIXME: we better get rid of ProfileType==2 altogether, instead of
        // skipping it here.
        if(iProfileType == 2) continue;
		
        alldata->profiles.iProfileTypeLast = iProfileType;

        // if the user does not require fitting a model to the observed 
        // vrad values, we don't need a second pass to remove dealiasing outliers
        if (alldata->options.fitVrad == TRUE) {
            nPasses = 2;
        } 
        else {
            nPasses = 1;
        }
			
		// set a flag that indicates if we want to keep earlier dealiased values
		int recycleDealias = FALSE;
		if(iProfileType<3 && alldata->options.dealiasRecycle){
			recycleDealias = TRUE;
		}
			
        // reset the flagPositionVDifMax bit before calculating each profile
        for (iPoint = 0; iPoint < alldata->points.nRowsPoints; iPoint++) {
            unsigned int gateCode = (unsigned int) alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.gateCodeCol];
            alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.gateCodeCol] = (float) (gateCode &= ~(1<<(alldata->flags.flagPositionVDifMax)));
        }
        
		// reset the dealiased vrad value before calculating each profile
		if(!recycleDealias){
			for (iPoint = 0; iPoint < alldata->points.nRowsPoints; iPoint++) {
				alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.vraddValueCol] = alldata->points.points[iPoint * alldata->points.nColsPoints + alldata->points.vradValueCol];
			}	
		}
        
        for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {

            // these variables are needed just outside of the iPass loop below 
            float chi = NAN;
            int hasGap = TRUE;
            float birdDensity = NAN;
 
            for (iPass = 0; iPass < nPasses; iPass++) {

                const int iPointFrom = alldata->points.indexFrom[iLayer];
                const int nPointsLayer = alldata->points.nPointsWritten[iLayer];

                int iPointLayer;
                int iPointIncluded;
                int iPointIncludedZ;
                int nPointsIncluded;
                int nPointsIncludedZ;

                float parameterVector[] = {NAN,NAN,NAN};
                float avar[] = {NAN,NAN,NAN};
                
                float* pointsSelection = malloc(sizeof(float) * nPointsLayer * alldata->misc.nDims);
                float* yNyquist = malloc(sizeof(float) * nPointsLayer);
				float* yDealias = malloc(sizeof(float) * nPointsLayer);
				float* yObs = malloc(sizeof(float) * nPointsLayer);
                float* yFitted = malloc(sizeof(float) * nPointsLayer);
                int* includedIndex = malloc(sizeof(int) * nPointsLayer);
                
                float* yObsSvdFit = yObs;
				float dbzValue = NAN;
                float undbzValue = NAN;
                double undbzSum = 0.0;
                float undbzAvg = NAN;
                float dbzAvg = NAN;
                float reflectivity = NAN;
                float chisq = NAN;
                float hSpeed = NAN;
                float hDir = NAN;


                for (iPointLayer = 0; iPointLayer < nPointsLayer; iPointLayer++) {

                    pointsSelection[iPointLayer * alldata->misc.nDims + 0] = 0.0f;
                    pointsSelection[iPointLayer * alldata->misc.nDims + 1] = 0.0f;

					yNyquist[iPointLayer] = 0.0f;
					yDealias[iPointLayer] = 0.0f;                    
                    yObs[iPointLayer] = 0.0f;
                    yFitted[iPointLayer] = 0.0f;
                    
                    includedIndex[iPointLayer] = -1;

                };

                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  0] = (iLayer + 0.5) * alldata->options.layerThickness;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  1] = alldata->options.layerThickness;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  2] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  3] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  4] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  5] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  6] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  7] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  8] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  9] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 10] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 11] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 12] = NODATA;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 13] = NODATA;

		        //Calculate the average reflectivity Z of the layer
                iPointIncludedZ = 0;
                for (iPointLayer = iPointFrom; iPointLayer < iPointFrom + nPointsLayer; iPointLayer++) {
                    
                    unsigned int gateCode = (unsigned int) alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.gateCodeCol];

                    if (includeGate(iProfileType,0,gateCode, alldata) == TRUE) {

                        // get the dbz value at this [azimuth, elevation] 
                        dbzValue = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.dbzValueCol];
                        // convert from dB scale to linear scale 
                        if (isnan(dbzValue)==TRUE){
                            undbzValue = 0;
                        }
                        else {
                            undbzValue = (float) exp(0.1*log(10)*dbzValue);
                        }
                        // sum the undbz in this layer
                        undbzSum += undbzValue;
                        // raise the counter
                        iPointIncludedZ += 1;
                        
                    }
                } // endfor (iPointLayer = 0; iPointLayer < nPointsLayer; iPointLayer++) {
                nPointsIncludedZ = iPointIncludedZ;
 
				// calculate bird densities from undbzSum
                if (nPointsIncludedZ > alldata->constants.nPointsIncludedMin) {   
                    // when there are enough valid points, convert undbzAvg back to dB-scale
                    undbzAvg = (float) (undbzSum/nPointsIncludedZ);
                    dbzAvg = (10*log(undbzAvg))/log(10);
                }
                else {
                    undbzAvg = UNDETECT;
                    dbzAvg = UNDETECT;
                }

                // convert from Z (not dBZ) in units of mm^6/m^3 to 
                // reflectivity eta in units of cm^2/km^3
                reflectivity = alldata->misc.dbzFactor * undbzAvg;
                
                if (iProfileType == 1) {
                    // calculate bird density in number of birds/km^3 by
                    // dividing the reflectivity by the (assumed) cross section
                    // of one bird
                    birdDensity = reflectivity / alldata->options.birdRadarCrossSection;
                }
                else {
                    birdDensity = UNDETECT;
                }
                
		        //Prepare the arguments of svdfit
                iPointIncluded = 0;
                for (iPointLayer = iPointFrom; iPointLayer < iPointFrom + nPointsLayer; iPointLayer++) {
                    
                    unsigned int gateCode = (unsigned int) alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.gateCodeCol];

                    if (includeGate(iProfileType,1,gateCode, alldata) == TRUE) {

                        // copy azimuth angle from the 'points' array
                        pointsSelection[iPointIncluded * alldata->misc.nDims + 0] = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.azimAngleCol];
                        // copy elevation angle from the 'points' array
                        pointsSelection[iPointIncluded * alldata->misc.nDims + 1] = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.elevAngleCol];
						// copy nyquist interval from the 'points' array
                        yNyquist[iPointIncluded] = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.nyquistCol];
                        // copy the observed vrad value at this [azimuth, elevation] 
                        yObs[iPointIncluded] = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.vradValueCol];
						// copy the dealiased vrad value at this [azimuth, elevation] 
                        yDealias[iPointIncluded] = alldata->points.points[iPointLayer * alldata->points.nColsPoints + alldata->points.vraddValueCol];
                        // pre-allocate the fitted vrad value at this [azimuth,elevation]
                        yFitted[iPointIncluded] = 0.0f;
                        // keep a record of which index was just included
                        includedIndex[iPointIncluded] = iPointLayer;
                        // raise the counter
                        iPointIncluded += 1;

                        
                    }
                } // endfor (iPointLayer = 0; iPointLayer < nPointsLayer; iPointLayer++) {
                nPointsIncluded = iPointIncluded;

                // check if there are directions that have almost no observations
                // (as this makes the svdfit result really uncertain)  
                hasGap = hasAzimuthGap(&pointsSelection[0], nPointsIncluded, alldata);
                
                if (alldata->options.fitVrad == TRUE) {

                    if (hasGap==FALSE) {
                        
                        // ------------------------------------------------------------- //
                        //                  dealias radial velocities                    //
                        // ------------------------------------------------------------- //
                                
                        // dealias velocities if requested by user
                        // only dealias in first pass (later passes for removing dual-PRF dealiasing errors,
                        // which show smaller offsets than (2*nyquist velocity) and therefore are not
                        // removed by dealiasing routine)
                        // The condition nyquistMinUsed<maxNyquistDealias enforces that if all scans
                        // have a higher Nyquist velocity than maxNyquistDealias, dealiasing is suppressed
                        if(alldata->options.dealiasVrad && iPass == 0 && !recycleDealias){
                            #ifdef FPRINTFON
                            fprintf(stderr,"dealiasing %i points for profile %i, layer %i ...\n",nPointsIncluded,iProfileType,iLayer+1);
                            #endif
                            int result = dealias_points(&pointsSelection[0], alldata->misc.nDims, &yNyquist[0],
                                            alldata->misc.nyquistMin, &yObs[0], &yDealias[0], nPointsIncluded);							
                            // store dealiased velocities in points array (for re-use when iPass>0)
                            for(int i=0; i<nPointsIncluded;i++){
                                alldata->points.points[includedIndex[i] * alldata->points.nColsPoints + alldata->points.vraddValueCol] = yDealias[i];	
                            }
                        
                            if(result == 0){
                                fprintf(stderr,"Warning, failed to dealias radial velocities");
                            }
                        }

                        //print the dealiased values to stderr
                        if(alldata->options.printDealias == TRUE){
                            printDealias(&pointsSelection[0], alldata->misc.nDims, &yNyquist[0],
                                            &yObs[0], &yDealias[0], nPointsIncluded, iProfileType, iLayer+1, iPass+1);
                        }

                        
                        // yDealias is initialized to yObs, so we can always run svdfit
                        // on yDealias, even when not running a dealiasing routine
                        yObsSvdFit = yDealias;
                        
                        // ------------------------------------------------------------- //
                        //                       do the svdfit                           //
                        // ------------------------------------------------------------- //
                                                
                        chisq = svdfit(&pointsSelection[0], alldata->misc.nDims, &yObsSvdFit[0], &yFitted[0], 
                                nPointsIncluded, &parameterVector[0], &avar[0], alldata->misc.nParsFitted);

                        if (chisq < alldata->constants.chisqMin) {
                            // the standard deviation of the fit is too low, as in the case of overfit
                            // reset parameter vector array elements to NAN and continue with the next layer
                            parameterVector[0] = NAN;
                            parameterVector[1] = NAN;
                            parameterVector[2] = NAN;
                            // FIXME: if this happens, profile fields are not updated from UNDETECT to NODATA
                            // continue; // with for (iPass = 0; iPass < nPasses; iPass++)
                        } 
                        else {
                            
                            chi = sqrt(chisq);
                            //hSpeed = sqrt(pow(parameterVector[0],2) + pow(parameterVector[1],2));
                            hSpeed = sqrt(pow(parameterVector[0],2) + pow(parameterVector[1],2));
                            hDir = (atan2(parameterVector[0],parameterVector[1])*RAD2DEG);
                            
                            if (hDir < 0) {
                                hDir += 360.0f;
                            }
                            
                            // if the fitted vrad value is more than 'absVDifMax' away from the corresponding
                            // observed vrad value, set the gate's flagPositionVDifMax bit flag to 1, excluding the 
                            // gate in the second svdfit iteration
                            updateFlagFieldsInPointsArray(&yObsSvdFit[0], &yFitted[0], &includedIndex[0], nPointsIncluded,
                                                  &(alldata->points.points[0]), alldata);

                        }
						
                    } // endif (hasGap == FALSE)
                    
                }; // endif (fitVrad == TRUE)
                
                //---------------------------------------------//
                //         Fill the profile arrays             //
                //---------------------------------------------//
                
                // always fill below profile fields, these never have a NODATA or UNDETECT value.
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  0] = iLayer * alldata->options.layerThickness;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  1] = (iLayer + 1) * alldata->options.layerThickness;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  8] = (float) hasGap;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 10] = (float) nPointsIncluded;
                alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 13] = (float) nPointsIncludedZ;
                
                // fill below profile fields when (1) VVP fit was not performed becasue of azimuthal data gap
                // and (2) layer contains range gates within the volume sampled by the radar.
                if (hasGap && nPointsIncludedZ>alldata->constants.nPointsIncludedMin){
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  2] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  3] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  4] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  5] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  6] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  7] = UNDETECT;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  9] = dbzAvg;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 11] = reflectivity;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 12] = birdDensity;
                }
                // case of valid fit, fill profile fields with VVP fit parameters
                if (!hasGap){
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  2] = parameterVector[0];
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  3] = parameterVector[1];
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  4] = parameterVector[2];
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  5] = hSpeed;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  6] = hDir;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  7] = chi;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile +  9] = dbzAvg;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 11] = reflectivity;
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 12] = birdDensity;
                }
  
                free((void*) yObs);
                free((void*) yFitted);
				free((void*) yNyquist);
				free((void*) yDealias);
                free((void*) pointsSelection);
                free((void*) includedIndex);
        
            } // endfor (iPass = 0; iPass < nPasses; iPass++)
            
            // You need some of the results of iProfileType == 3 in order 
            // to calculate iProfileType == 1, therefore iProfileType == 3 is executed first
            if (iProfileType == 3) {
                if (chi < alldata->options.stdDevMinBird) {
                    alldata->misc.scatterersAreNotBirds[iLayer] = TRUE;
                }
                else {
                    alldata->misc.scatterersAreNotBirds[iLayer] = FALSE;
                }
            }   
            if (iProfileType == 1) {
                // set the bird density to zero if radial velocity stdev below threshold:
                if (alldata->misc.scatterersAreNotBirds[iLayer] == TRUE){
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 12] = 0.0;
                }
                // set bird density values to zero if hasGap:
                if (hasGap && birdDensity>0){
                    alldata->profiles.profile[iLayer*alldata->profiles.nColsProfile + 12] = 0.0;
                }
            }

        } // endfor (iLayer = 0; iLayer < nLayers; iLayer++)

        if (alldata->options.printProfileVar == TRUE) {
            printProfile(alldata);
        }
        if (iProfileType == 1 && alldata->options.exportBirdProfileAsJSONVar == TRUE) {
            exportBirdProfileAsJSON(alldata);
        }

        // ---------------------------------------------------------------- //
        //       this next section is a bit ugly but it does the job        //
        // ---------------------------------------------------------------- //
        
        int iCopied = 0;
        int iColProfile;
        int iRowProfile;
        for (iRowProfile = 0; iRowProfile < alldata->profiles.nRowsProfile; iRowProfile++) {
            for (iColProfile = 0; iColProfile < alldata->profiles.nColsProfile; iColProfile++) {
                switch (iProfileType) {
                    case 1:
                        alldata->profiles.profile1[iCopied] = alldata->profiles.profile[iCopied];
                        break;
                    case 2:
                        alldata->profiles.profile2[iCopied] = alldata->profiles.profile[iCopied];
                        break;
                    case 3:
                        alldata->profiles.profile3[iCopied] = alldata->profiles.profile[iCopied];
                        break;
                    default:
                        fprintf(stderr, "Something is wrong this should not happen.\n");
                }
                iCopied += 1;
            }
        }
    
    } // endfor (iProfileType = nProfileTypes; iProfileType > 0; iProfileType--)


} // vol2birdCalcProfiles




int vol2birdGetNColsProfile(vol2bird_t *alldata) {

    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return -1;
    }
    return alldata->profiles.nColsProfile;
} // vol2birdGetNColsProfile


int vol2birdGetNRowsProfile(vol2bird_t *alldata) {   

    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return -1;
    }
    return alldata->profiles.nRowsProfile;
} // vol2birdGetNColsProfile


float* vol2birdGetProfile(int iProfileType, vol2bird_t *alldata) {
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return (float *) NULL;
    }

    switch (iProfileType) {
        case 1 : 
            return &(alldata->profiles.profile1[0]);
        case 2 : 
            return &(alldata->profiles.profile2[0]);
        case 3 : 
            return &(alldata->profiles.profile3[0]);
        default :
            fprintf(stderr, "Something went wrong; behavior not implemented for given iProfileType.\n");
    }

    return (float *) NULL;
}


void vol2birdPrintIndexArrays(vol2bird_t* alldata) {
    
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }
    
    int iLayer;

    fprintf(stderr, "iLayer  iFrom   iTo     iTo-iFrom nWritten\n");
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        fprintf(stderr, "%7d %7d %7d %10d %8d\n",
            iLayer, 
            alldata->points.indexFrom[iLayer], 
            alldata->points.indexTo[iLayer], 
            alldata->points.indexTo[iLayer] - alldata->points.indexFrom[iLayer], 
            alldata->points.nPointsWritten[iLayer]);
    }
} // vol2birdPrintIndexArrays



void vol2birdPrintOptions(vol2bird_t* alldata) {
    
    // ------------------------------------------------------- //
    // this function prints vol2bird's configuration to stderr //
    // ------------------------------------------------------- //
    
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }

    fprintf(stderr,"\n\nvol2bird configuration:\n\n");

    fprintf(stderr,"%-25s = %f\n","absVDifMax",alldata->constants.absVDifMax);
    fprintf(stderr,"%-25s = %f\n","azimMax",alldata->options.azimMax);
    fprintf(stderr,"%-25s = %f\n","azimMin",alldata->options.azimMin);
    fprintf(stderr,"%-25s = %f\n","birdRadarCrossSection",alldata->options.birdRadarCrossSection);
    fprintf(stderr,"%-25s = %f\n","cellClutterFractionMax",alldata->constants.cellClutterFractionMax);
    fprintf(stderr,"%-25s = %f\n","cellEtaMin",alldata->options.cellEtaMin);
    fprintf(stderr,"%-25s = %f\n","cellStdDevMax",alldata->options.cellStdDevMax);
    fprintf(stderr,"%-25s = %f\n","chisqMin",alldata->constants.chisqMin);
    fprintf(stderr,"%-25s = %f\n","clutterValueMin",alldata->constants.clutterValueMin);
    fprintf(stderr,"%-25s = %f\n","etaMax",alldata->options.etaMax);
    fprintf(stderr,"%-25s = %f\n","dbzThresMin",alldata->options.dbzThresMin);
    fprintf(stderr,"%-25s = %s\n","dbzType",alldata->options.dbzType);
    fprintf(stderr,"%-25s = %f\n","elevMax",alldata->options.elevMax);
    fprintf(stderr,"%-25s = %f\n","elevMin",alldata->options.elevMin);
    fprintf(stderr,"%-25s = %d\n","fitVrad",alldata->options.fitVrad);
    fprintf(stderr,"%-25s = %f\n","fringeDist",alldata->constants.fringeDist);
    fprintf(stderr,"%-25s = %f\n","layerThickness",alldata->options.layerThickness);
    fprintf(stderr,"%-25s = %f\n","minNyquist",alldata->options.minNyquist);
    fprintf(stderr,"%-25s = %d\n","nGatesCellMin",alldata->constants.nGatesCellMin);
    fprintf(stderr,"%-25s = %d\n","nAzimNeighborhood",alldata->constants.nAzimNeighborhood);
    fprintf(stderr,"%-25s = %d\n","nBinsGap",alldata->constants.nBinsGap);
    fprintf(stderr,"%-25s = %d\n","nCountMin",alldata->constants.nCountMin);
    fprintf(stderr,"%-25s = %d\n","nLayers",alldata->options.nLayers);
    fprintf(stderr,"%-25s = %d\n","nObsGapMin",alldata->constants.nObsGapMin);
    fprintf(stderr,"%-25s = %d\n","nPointsIncludedMin",alldata->constants.nPointsIncludedMin);
    fprintf(stderr,"%-25s = %d\n","nRangNeighborhood",alldata->constants.nRangNeighborhood);
    fprintf(stderr,"%-25s = %f\n","radarWavelength",alldata->options.radarWavelength);
    fprintf(stderr,"%-25s = %f\n","rangeMax",alldata->options.rangeMax);
    fprintf(stderr,"%-25s = %f\n","rangeMin",alldata->options.rangeMin);
    fprintf(stderr,"%-25s = %f\n","rCellMax",alldata->misc.rCellMax);
    fprintf(stderr,"%-25s = %f\n","refracIndex",alldata->constants.refracIndex);
    fprintf(stderr,"%-25s = %d\n","requireVrad",alldata->options.requireVrad);
    fprintf(stderr,"%-25s = %f\n","stdDevMinBird",alldata->options.stdDevMinBird);
    fprintf(stderr,"%-25s = %c\n","useStaticClutterData",alldata->options.useStaticClutterData == TRUE ? 'T' : 'F');
    fprintf(stderr,"%-25s = %f\n","vradMin",alldata->constants.vradMin);
    
    fprintf(stderr,"\n\n");

}  // vol2birdPrintOptions





void vol2birdPrintPointsArray(vol2bird_t* alldata) {
    
    // ------------------------------------------------- //
    // this function prints the 'points' array to stderr //
    // ------------------------------------------------- //
    
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }

    int iPoint;
    
    fprintf(stderr, "iPoint  azim    elev    dbz         vrad        cell    gateCode  flags     nyquist vradd\n");
    
    for (iPoint = 0; iPoint < alldata->points.nRowsPoints * alldata->points.nColsPoints; iPoint+=alldata->points.nColsPoints) {
        
            char gateCodeStr[10];  // 9 bits plus 1 position for the null character '\0'
            
            printGateCode(&gateCodeStr[0], (int) alldata->points.points[iPoint + alldata->points.gateCodeCol]);
        
            fprintf(stderr, "  %6d",    iPoint/alldata->points.nColsPoints);
            fprintf(stderr, "  %6.2f",  alldata->points.points[iPoint + alldata->points.azimAngleCol]);
            fprintf(stderr, "  %6.2f",  alldata->points.points[iPoint + alldata->points.elevAngleCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.dbzValueCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.vradValueCol]);
            fprintf(stderr, "  %6.0f",  alldata->points.points[iPoint + alldata->points.cellValueCol]);
            fprintf(stderr, "  %8.0f",  alldata->points.points[iPoint + alldata->points.gateCodeCol]);
            fprintf(stderr, "  %12s",   gateCodeStr);
			fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.nyquistCol]);
			fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.vraddValueCol]);
            fprintf(stderr, "\n");
    }    
} // vol2birdPrintPointsArray




void vol2birdPrintPointsArraySimple(vol2bird_t* alldata) {
    
    // ------------------------------------------------- //
    // this function prints the 'points' array to stderr //
    // ------------------------------------------------- //
    
    int iPoint;
    
    fprintf(stderr, "iPoint  azim    elev    dbz         vrad        cell     flags     nyquist vradd\n");
    
    for (iPoint = 0; iPoint < alldata->points.nRowsPoints * alldata->points.nColsPoints; iPoint+=alldata->points.nColsPoints) {
                
            fprintf(stderr, "  %6d",    iPoint/alldata->points.nColsPoints);
            fprintf(stderr, "  %6.2f",  alldata->points.points[iPoint + alldata->points.azimAngleCol]);
            fprintf(stderr, "  %6.2f",  alldata->points.points[iPoint + alldata->points.elevAngleCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.dbzValueCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.vradValueCol]);
            fprintf(stderr, "  %6.0f",  alldata->points.points[iPoint + alldata->points.cellValueCol]);
            fprintf(stderr, "  %8.0f",  alldata->points.points[iPoint + alldata->points.gateCodeCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.nyquistCol]);
            fprintf(stderr, "  %10.2f", alldata->points.points[iPoint + alldata->points.vraddValueCol]);
            fprintf(stderr, "\n");
    }    
} // vol2birdPrintPointsArray






void printProfile(vol2bird_t* alldata) {
    
    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }

    fprintf(stderr,"\n\nProfile type: %d\n",alldata->profiles.iProfileTypeLast);

    fprintf(stderr,"altmin-altmax: [u         ,v         ,w         ]; "
                   "hSpeed  , hDir    , chi     , hasGap  , dbzAvg  ,"
                   " nPoints, eta         , rhobird nPointsZ \n");

    int iLayer;
    
    for (iLayer = alldata->options.nLayers - 1; iLayer >= 0; iLayer--) {
        
        fprintf(stderr,"%6.0f-%-6.0f: [%10.2f,%10.2f,%10.2f]; %8.2f, "
        "%8.1f, %8.1f, %8c, %8.2f, %7.0f, %12.2f, %8.2f %5.f\n",
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  0],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  1],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  2],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  3],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  4],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  5],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  6],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  7],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  8] == TRUE ? 'T' : 'F',
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile +  9],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile + 10],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile + 11],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile + 12],
        alldata->profiles.profile[iLayer * alldata->profiles.nColsProfile + 13]);
    }

    
} // printProfile()

// reads a polar volume from file and returns it as a RAVE polar volume object
// remember to release the polar volume object when done with it
PolarVolume_t* vol2birdGetVolume(char* filename, float rangeMax, int small){
    
    PolarVolume_t* volume = NULL;
    
    RaveIO_t* raveio = RaveIO_open(filename);

    // check that a valid RaveIO_t pointer was returned
    if (raveio != (RaveIO_t*) NULL){
        // check that the Rave object is a polar volume
        if (RaveIO_getObjectType(raveio) == Rave_ObjectType_PVOL) {

            // the if statement above tests whether we are dealing with a 
            // PVOL object, so we can safely cast the generic object to
            // the PolarVolume_t type:
            
            volume = (PolarVolume_t*) RaveIO_getObject(raveio);
        }
        else{
            fprintf(stderr,"Error: ODIM HDF5 file is not a polar volume\n");
        }
    }
    // not a rave complient file, attempt to read the file with the RSL library instead
    #ifdef RSL
    else{
        Radar *radar;

        // only read reflectivity, velocity, Rho_HV        
        // select all scans
        // XXXX FIXME: make here full selection OR vol2bird limited selection!
        // XXXX hier INDEX array opstellen??
        if(small) RSL_select_fields("dz","vr","rh", NULL);
        else RSL_select_fields("dz","vr","sw","zt","dr","rh","ph","kd", NULL);
        
        RSL_read_these_sweeps("all",NULL);
        
        // read the file to a RSL radar object
        
        // according to documentation of RSL it is not required to parse a callid
        // but in practice it is for WSR88D.
        char* base = basename(filename);
        char callid[5];
        strncpy(callid, base,4);
        callid[4] = 0; //null terminate destination
        radar = RSL_anyformat_to_radar(filename,callid);
 
        if (radar == NULL) {
            fprintf(stderr, "critical error, cannot open file %s\n", filename);
            return NULL;
        }
        
        // convert RSL object to RAVE polar volume
        
        volume = PolarVolume_vol2bird_RSL2Rave(radar, rangeMax);
        
        RSL_free_radar(radar);
    }
    #endif
    
    RAVE_OBJECT_RELEASE(raveio);
    
    return volume;
}



RaveIO_t* vol2birdIO_open(const char* filename)
{
  RaveIO_t* result = NULL;

  if (filename == NULL) {
    goto done;
  }

  result = RAVE_OBJECT_NEW(&RaveIO_TYPE);
  if (result == NULL) {
    RAVE_CRITICAL0("Failed to create raveio instance");
    goto done;
  }

  if (!RaveIO_setFilename(result, filename)) {
    RAVE_CRITICAL0("Failed to set filename");
    RAVE_OBJECT_RELEASE(result);
    goto done;
  }

  if (!RaveIO_load(result)) {
    RAVE_WARNING0("Failed to load file");
    RAVE_OBJECT_RELEASE(result);
    goto done;
  }

done:
  return result;
}


// loads configuration data in the alldata struct
int vol2birdLoadConfig(vol2bird_t* alldata) {

    alldata->misc.loadConfigSuccessful = FALSE;

    if (readUserConfigOptions(&(alldata->cfg)) != 0) {
        fprintf(stderr, "An error occurred while reading the user configuration file 'options.conf'.\n");
        return -1; 
    }
   
    cfg_t** cfg = &(alldata->cfg);

    // ------------------------------------------------------------- //
    //              vol2bird options from options.conf               //
    // ------------------------------------------------------------- //

    alldata->options.azimMax = cfg_getfloat(*cfg, "AZIMMAX");
    alldata->options.azimMin = cfg_getfloat(*cfg, "AZIMMIN");
    alldata->options.layerThickness = cfg_getfloat(*cfg, "HLAYER");
    alldata->options.nLayers = cfg_getint(*cfg, "NLAYER");
    alldata->options.rangeMax = cfg_getfloat(*cfg, "RANGEMAX");
    alldata->options.rangeMin = cfg_getfloat(*cfg, "RANGEMIN");
    alldata->options.elevMax = cfg_getfloat(*cfg, "ELEVMAX");
    alldata->options.elevMin = cfg_getfloat(*cfg, "ELEVMIN");
    alldata->options.radarWavelength = cfg_getfloat(*cfg, "RADAR_WAVELENGTH_CM");
    alldata->options.useStaticClutterData = cfg_getbool(*cfg,"USE_STATIC_CLUTTER_DATA");
    alldata->options.printDbz = cfg_getbool(*cfg,"PRINT_DBZ");
    alldata->options.printDealias = cfg_getbool(*cfg,"PRINT_DEALIAS");
    alldata->options.printVrad = cfg_getbool(*cfg,"PRINT_VRAD");
    alldata->options.printRhohv = cfg_getbool(*cfg,"PRINT_RHOHV");
    alldata->options.printTex = cfg_getbool(*cfg,"PRINT_TEXTURE");
    alldata->options.printCell = cfg_getbool(*cfg,"PRINT_CELL");
    alldata->options.printCellProp = cfg_getbool(*cfg,"PRINT_CELL_PROP");
    alldata->options.printClut = cfg_getbool(*cfg,"PRINT_CLUT");
    alldata->options.printOptions = cfg_getbool(*cfg,"PRINT_OPTIONS");
    alldata->options.printProfileVar = cfg_getbool(*cfg,"PRINT_PROFILE");
    alldata->options.printPointsArray = cfg_getbool(*cfg,"PRINT_POINTS_ARRAY");
    alldata->options.fitVrad = cfg_getbool(*cfg,"FIT_VRAD");
    alldata->options.exportBirdProfileAsJSONVar = cfg_getbool(*cfg,"EXPORT_BIRD_PROFILE_AS_JSON"); 
    alldata->options.minNyquist = cfg_getfloat(*cfg,"MIN_NYQUIST_VELOCITY");
    alldata->options.maxNyquistDealias = cfg_getfloat(*cfg,"MAX_NYQUIST_DEALIAS");
    alldata->options.birdRadarCrossSection = cfg_getfloat(*cfg,"SIGMA_BIRD");
    alldata->options.cellStdDevMax = cfg_getfloat(*cfg,"STDEV_CELL");
    alldata->options.stdDevMinBird = cfg_getfloat(*cfg,"STDEV_BIRD");
    alldata->options.etaMax = cfg_getfloat(*cfg,"ETAMAX");
    alldata->options.cellEtaMin = cfg_getfloat(*cfg,"ETACELL");
    strcpy(alldata->options.dbzType,cfg_getstr(*cfg,"DBZTYPE"));
    alldata->options.requireVrad = cfg_getbool(*cfg,"REQUIRE_VRAD");
    alldata->options.dealiasVrad = cfg_getbool(*cfg,"DEALIAS_VRAD");
    alldata->options.dealiasRecycle = cfg_getbool(*cfg,"DEALIAS_RECYCLE");
    alldata->options.dualPol = cfg_getbool(*cfg,"DUALPOL");
    alldata->options.dbzThresMin = cfg_getfloat(*cfg,"DBZMIN");
    alldata->options.rhohvThresMin = cfg_getfloat(*cfg,"RHOHVMIN");

    // ------------------------------------------------------------- //
    //              vol2bird options from constants.h                //
    // ------------------------------------------------------------- //

    alldata->constants.nGatesCellMin = AREACELL;
    alldata->constants.cellClutterFractionMax = CLUTPERCCELL;
    alldata->constants.chisqMin = CHISQMIN;
    alldata->constants.clutterValueMin = DBZCLUTTER;
    alldata->constants.fringeDist = FRINGEDIST;
    alldata->constants.nBinsGap = NBINSGAP;
    alldata->constants.nPointsIncludedMin = NDBZMIN;
    alldata->constants.nNeighborsMin = NEIGHBORS;
    alldata->constants.nObsGapMin = NOBSGAPMIN;
    alldata->constants.nAzimNeighborhood = NTEXBINAZIM;
    alldata->constants.nRangNeighborhood = NTEXBINRANG;
    alldata->constants.nCountMin = NTEXMIN; 
    alldata->constants.refracIndex = REFRACTIVE_INDEX_OF_WATER;
    alldata->constants.absVDifMax = VDIFMAX;
    alldata->constants.vradMin = VRADMIN;

    // ------------------------------------------------------------- //
    //       some other variables, derived from user options         //
    // ------------------------------------------------------------- //

    alldata->misc.rCellMax = alldata->options.rangeMax + RCELLMAX_OFFSET;
    alldata->misc.nDims = 2;
    alldata->misc.nParsFitted = 3;

    // the following settings depend on wavelength, will be set in Vol2birdSetup
    alldata->misc.dbzFactor = NAN;
    alldata->misc.dbzMax = NAN;
    alldata->misc.cellDbzMin = NAN;

    alldata->misc.loadConfigSuccessful = TRUE;

    return 0;

}


//int vol2birdSetUp(PolarVolume_t* volume, cfg_t** cfg, vol2bird_t* alldata) {
int vol2birdSetUp(PolarVolume_t* volume, vol2bird_t* alldata) {
    
    alldata->misc.initializationSuccessful = FALSE;
    
    alldata->misc.vol2birdSuccessful = TRUE;

    if (alldata->misc.loadConfigSuccessful == FALSE){
        fprintf(stderr,"Vol2bird configuration not loaded. Run vol2birdLoadConfig prior to vol2birdSetup\n");
        return -1;
    }
 
    // reading radar wavelength from polar volume attribute
    // if present, overwrite options.radarWavelength with the value found.
    double wavelength = PolarVolume_getWavelength(volume);
    if (wavelength > 0){
        alldata->options.radarWavelength = wavelength;
    }
    else{
        fprintf(stderr,"Warning: radar wavelength not stored in polar volume. Using user-defined value of %f cm ...\n", alldata->options.radarWavelength);
    }
    
    // Now that we have the wavelength, calculate its derived quantities:
    // dbzFactor is the proportionality constant between reflectivity factor Z in mm^6/m^3
    // and reflectivity eta in cm^2/km^3, when radarWavelength in cm:
    alldata->misc.dbzFactor = (pow(alldata->constants.refracIndex,2) * 1000 * pow(PI,5))/pow(alldata->options.radarWavelength,4);
    alldata->misc.dbzMax = 10*log(alldata->options.etaMax / alldata->misc.dbzFactor)/log(10);
    alldata->misc.cellDbzMin = 10*log(alldata->options.cellEtaMin / alldata->misc.dbzFactor)/log(10);
 
    // ------------------------------------------------------------- //
    //                 determine which scans to use                  //
    // ------------------------------------------------------------- //
    
    vol2birdScanUse_t* scanUse;
    scanUse = determineScanUse(volume, alldata);
    if (!alldata->options.dealiasVrad && alldata->misc.nyquistMinUsed < alldata->options.maxNyquistDealias){   
       fprintf(stderr,"Warning: Nyquist velocity below maxNyquistDealias threshold was found (%f<%f), consider dealiasing.\n",alldata->misc.nyquistMinUsed,alldata->options.maxNyquistDealias);       
    }    
    if (alldata->options.dealiasVrad && alldata->misc.nyquistMinUsed > alldata->options.maxNyquistDealias){
       alldata->options.dealiasVrad=FALSE;
       //Maybe you want to give a warning here, but that generates a lot of warnings as this situation is common ...
    }
    if (alldata->options.dealiasVrad){
        fprintf(stderr,"Warning: radial velocities will be dealiased...\n");
    }

    // ------------------------------------------------------------- //
    //     store all options and constants in task_args string       //
    // ------------------------------------------------------------- //

    // the radar wavelength setting is read from taken from the volume object
    // if a wavelength attribute is present. Therefore the task_args string is
    // set here and not in vol2birdLoadConfig(), which has no access to the volume    
    
    sprintf(alldata->misc.task_args,
        "azimMax=%f,azimMin=%f,layerThickness=%f,nLayers=%i,rangeMax=%f,"
        "rangeMin=%f,elevMax=%f,elevMin=%f,radarWavelength=%f,"
        "useStaticClutterData=%i,fitVrad=%i,exportBirdProfileAsJSONVar=%i,"
        "minNyquist=%f,maxNyquistDealias=%f,birdRadarCrossSection=%f,stdDevMinBird=%f,"
        "cellEtaMin=%f,etaMax=%f,dbzType=%s,requireVrad=%i,"
        "dealiasVrad=%i,dealiasRecycle=%i,dualPol=%i,rhohvThresMin=%f,"
    
        "nGatesCellMin=%i,cellClutterFractionMax=%f,"
        "chisqMin=%f,clutterValueMin=%f,dbzThresMin=%f,"
        "fringeDist=%f,nBinsGap=%i,nPointsIncludedMin=%i,nNeighborsMin=%i,"
        "nObsGapMin=%i,nAzimNeighborhood=%i,nRangNeighborhood=%i,nCountMin=%i,"
        "refracIndex=%f,cellStdDevMax=%f,absVDifMax=%f,vradMin=%f",

        alldata->options.azimMax,
        alldata->options.azimMin,
        alldata->options.layerThickness,
        alldata->options.nLayers,
        alldata->options.rangeMax,
        alldata->options.rangeMin,
        alldata->options.elevMax,
        alldata->options.elevMin,
        alldata->options.radarWavelength,
        alldata->options.useStaticClutterData,
        alldata->options.fitVrad,
        alldata->options.exportBirdProfileAsJSONVar,
        alldata->options.minNyquist,
        alldata->options.maxNyquistDealias,
        alldata->options.birdRadarCrossSection,
        alldata->options.stdDevMinBird,
        alldata->options.cellEtaMin,
        alldata->options.etaMax,
        alldata->options.dbzType,
        alldata->options.requireVrad,
        alldata->options.dealiasVrad,
        alldata->options.dealiasRecycle,
        alldata->options.dualPol,
        alldata->options.rhohvThresMin,

        alldata->constants.nGatesCellMin,
        alldata->constants.cellClutterFractionMax,
        alldata->constants.chisqMin,
        alldata->constants.clutterValueMin,
        alldata->options.dbzThresMin,
        alldata->constants.fringeDist,
        alldata->constants.nBinsGap,
        alldata->constants.nPointsIncludedMin,
        alldata->constants.nNeighborsMin,
        alldata->constants.nObsGapMin,
        alldata->constants.nAzimNeighborhood,
        alldata->constants.nRangNeighborhood,
        alldata->constants.nCountMin,
        alldata->constants.refracIndex,
        alldata->options.cellStdDevMax,
        alldata->constants.absVDifMax,
        alldata->constants.vradMin
    );

   
    if (scanUse == (vol2birdScanUse_t*) NULL){
        fprintf(stderr, "Error: no valid scans found in polar volume, aborting ...\n");
        return -1;
    }

    // Print warning for S-band in single pol mode
    if(alldata->options.radarWavelength > 7.5 && !alldata->options.dualPol){
        fprintf(stderr,"Warning: using experimental SINGLE polarization mode on S-band data, results may be unreliable!\n");
    }


    // ------------------------------------------------------------- //
    //             lists of indices into the 'points' array:         //
    //          where each altitude layer's data starts and ends     //
    // ------------------------------------------------------------- //

    int iLayer;
    
    // pre-allocate the list with start-from indexes for each 
    // altitude bin in the profile
    alldata->points.indexFrom = (int*) malloc(sizeof(int) * alldata->options.nLayers);
    if (alldata->points.indexFrom == NULL) {
        fprintf(stderr,"Error pre-allocating array 'indexFrom'\n");
        return -1;
    }
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        alldata->points.indexFrom[iLayer] = 0;
    }

    // pre-allocate the list with end-before indexes for each 
    // altitude bin in the profile
    alldata->points.indexTo = (int*) malloc(sizeof(int) * alldata->options.nLayers);
    if (alldata->points.indexTo == NULL) {
        fprintf(stderr,"Error pre-allocating array 'indexTo'\n");
        return -1;
    }
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        alldata->points.indexTo[iLayer] = 0;
    }

    // pre-allocate the list containing TRUE or FALSE depending on the 
    // results of calculating iProfileType == 3, which are needed when
    // calculating iProfileType == 1
    alldata->misc.scatterersAreNotBirds = (int*) malloc(sizeof(int) * alldata->options.nLayers);
    if (alldata->misc.scatterersAreNotBirds == NULL) {
        fprintf(stderr,"Error pre-allocating array 'scatterersAreNotBirds'\n");
        return -1;
    }
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        alldata->misc.scatterersAreNotBirds[iLayer] = -1;
    }


    // for each altitude layer, you need to remember how many points 
    // were already written. This information is stored in the 
    // 'nPointsWritten' array
    alldata->points.nPointsWritten = (int*) malloc(sizeof(int) * alldata->options.nLayers);
    if (alldata->points.nPointsWritten == NULL) {
        fprintf(stderr,"Error pre-allocating array 'nPointsWritten'\n");
        return -1;
    }
    for (iLayer = 0; iLayer < alldata->options.nLayers; iLayer++) {
        alldata->points.nPointsWritten[iLayer] = 0;
    }


    // ------------------------------------------------------------- //
    //               information about the 'points' array            //
    // ------------------------------------------------------------- //

    alldata->points.nColsPoints = 8;
    alldata->points.nRowsPoints = detSvdfitArraySize(volume, scanUse, alldata);

    alldata->points.azimAngleCol = 0;
    alldata->points.elevAngleCol = 1;
    alldata->points.dbzValueCol = 2;
    alldata->points.vradValueCol = 3;
    alldata->points.cellValueCol = 4;
    alldata->points.gateCodeCol = 5;
	alldata->points.nyquistCol = 6;
	alldata->points.vraddValueCol = 7;

    // pre-allocate the 'points' array (note it has 'nColsPoints'
    // pseudo-columns)
    alldata->points.points = (float*) malloc(sizeof(float) * alldata->points.nRowsPoints * alldata->points.nColsPoints);
    if (alldata->points.points == NULL) {
        fprintf(stderr,"Error pre-allocating array 'points'.\n"); 
        return -1;
    }

    int iRowPoints;
    int iColPoints;
        
    for (iRowPoints = 0; iRowPoints < alldata->points.nRowsPoints; iRowPoints++) {
        for (iColPoints = 0; iColPoints < alldata->points.nColsPoints; iColPoints++) {
            alldata->points.points[iRowPoints*alldata->points.nColsPoints + iColPoints] = NAN;
        }
    }

    // information about the flagfields of 'gateCode'
    
    alldata->flags.flagPositionStaticClutter = 0;
    alldata->flags.flagPositionDynamicClutter = 1;
    alldata->flags.flagPositionDynamicClutterFringe = 2;
    alldata->flags.flagPositionVradMissing = 3;
    alldata->flags.flagPositionDbzTooHighForBirds = 4;
    alldata->flags.flagPositionVradTooLow = 5;
    alldata->flags.flagPositionVDifMax = 6;
    alldata->flags.flagPositionAzimTooLow = 7;
    alldata->flags.flagPositionAzimTooHigh = 8;

    // construct the 'points' array
    constructPointsArray(volume, scanUse, alldata);

    // classify the gates based on the data in 'points'
    classifyGatesSimple(alldata);


    // ------------------------------------------------------------- //
    //              information about the 'profile' array            //
    // ------------------------------------------------------------- //

    alldata->profiles.nProfileTypes = 3;
    alldata->profiles.nRowsProfile = alldata->options.nLayers;
    alldata->profiles.nColsProfile = 14; 
    
    // pre-allocate the array holding any profiled data (note it has 
    // 'nColsProfile' pseudocolumns):
    alldata->profiles.profile = (float*) malloc(sizeof(float) * alldata->profiles.nRowsProfile * alldata->profiles.nColsProfile);
    if (alldata->profiles.profile == NULL) {
        fprintf(stderr,"Error pre-allocating array 'profile'.\n"); 
        return -1;
    }

    // these next three variables are a quick fix
    alldata->profiles.profile1 = (float*) malloc(sizeof(float) * alldata->profiles.nRowsProfile * alldata->profiles.nColsProfile);
    if (alldata->profiles.profile1 == NULL) {
        fprintf(stderr,"Error pre-allocating array 'profile1'.\n"); 
        return -1;
    }
    alldata->profiles.profile2 = (float*) malloc(sizeof(float) * alldata->profiles.nRowsProfile * alldata->profiles.nColsProfile);
    if (alldata->profiles.profile2 == NULL) {
        fprintf(stderr,"Error pre-allocating array 'profile2'.\n"); 
        return -1;
    }
    alldata->profiles.profile3 = (float*) malloc(sizeof(float) * alldata->profiles.nRowsProfile * alldata->profiles.nColsProfile);
    if (alldata->profiles.profile3 == NULL) {
        fprintf(stderr,"Error pre-allocating array 'profile3'.\n"); 
        return -1;
    }

    int iRowProfile;
    int iColProfile;
        
    for (iRowProfile = 0; iRowProfile < alldata->profiles.nRowsProfile; iRowProfile++) {
        for (iColProfile = 0; iColProfile < alldata->profiles.nColsProfile; iColProfile++) {
            alldata->profiles.profile[iRowProfile*alldata->profiles.nColsProfile + iColProfile] = NODATA;
            alldata->profiles.profile1[iRowProfile*alldata->profiles.nColsProfile + iColProfile] = NODATA;
            alldata->profiles.profile2[iRowProfile*alldata->profiles.nColsProfile + iColProfile] = NODATA;
            alldata->profiles.profile3[iRowProfile*alldata->profiles.nColsProfile + iColProfile] = NODATA;
        }
    }

    alldata->profiles.iProfileTypeLast = -1;


 
    // ------------------------------------------------------------- //
    //              initialising rave profile fields                 //
    // ------------------------------------------------------------- //

    alldata->vp = RAVE_OBJECT_NEW(&VerticalProfile_TYPE);
        
    alldata->misc.initializationSuccessful = TRUE;

    if (alldata->options.printOptions == TRUE) {
        vol2birdPrintOptions(alldata);
    }
    
    if (alldata->options.printPointsArray == TRUE) {

        vol2birdPrintIndexArrays(alldata);
        vol2birdPrintPointsArray(alldata);

    }
    
    free(scanUse);

    return 0;

} // vol2birdSetUp




void vol2birdTearDown(vol2bird_t* alldata) {
    
    // ---------------------------------------------------------- //
    // free the memory that was previously allocated for vol2bird //
    // ---------------------------------------------------------- //

    if (alldata->misc.initializationSuccessful==FALSE) {
        fprintf(stderr,"You need to initialize vol2bird before you can use it. Aborting.\n");
        return;
    }

    // free the points array, the indexes into it, the counters, as well
    // as the profile data array
    free((void*) alldata->points.points);
    free((void*) alldata->profiles.profile);
    free((void*) alldata->profiles.profile1);
    free((void*) alldata->profiles.profile2);
    free((void*) alldata->profiles.profile3);
    free((void*) alldata->points.indexFrom);
    free((void*) alldata->points.indexTo);
    free((void*) alldata->points.nPointsWritten);
    free((void*) alldata->misc.scatterersAreNotBirds);
   
    // free all rave fields
    RAVE_OBJECT_RELEASE(alldata->vp);
 
    // free the memory that holds the user configurable options
    cfg_free(alldata->cfg);
    
    // reset this variable to its initial value
    alldata->misc.initializationSuccessful = FALSE;
    alldata->misc.loadConfigSuccessful = FALSE;

} // vol2birdTearDown




