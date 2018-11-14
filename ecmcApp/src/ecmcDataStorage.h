/*
 * ecmcDataStorage.h
 *
 *  Created on: May 27, 2016
 *      Author: anderssandstrom
 */

#ifndef ECMCDATASTORAGE_H_
#define ECMCDATASTORAGE_H_

#include "stdio.h"
#include <stdlib.h>
#include "ecmcError.h"
#include "ecmcDefinitions.h"

//Data storage
#define ERROR_DATA_STORAGE_FULL 0x20200
#define ERROR_DATA_STORAGE_NULL 0x20201
#define ERROR_DATA_STORAGE_SIZE_TO_SMALL 0x20202
#define ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE 0x20203

enum storageType{
  ECMC_STORAGE_NORMAL_BUFFER=0, //Fill from beginning. Stop when full.
  ECMC_STORAGE_RING_BUFFER=1,   //Fill from beginning. Start over in beginning
  ECMC_STORAGE_FIFO_BUFFER=2,   //Fill from end (newwst value in the end). Old values shifted out
};

class ecmcDataStorage: public ecmcError
{
public:
  ecmcDataStorage (int index);
  ecmcDataStorage (int index,int size,storageType bufferType);
  ~ecmcDataStorage ();
  int setBufferSize(int elements);
  int clearBuffer();
  int appendData(double data);
  int isStorageFull();
  int printBuffer();
  int getSize();
  int getCurrentIndex();
  int getIndex();
  int getData(double **data, int *size);
  int getDataElement(int index,double *data);
  int setData(double *data, int size);
  int setDataElement(int index,double data);
  int appendData(double *data, int size);
  int setCurrentPosition(int position);
  void printCurrentState();
private:
  int appendDataFifo(double *data, int size);
  int appendDataRing(double *data, int size);
  int appendDataNormal(double *data, int size);
  void initVars();
  int currentBufferIndex_;
  double* buffer_;
  int bufferElementCount_;
  storageType bufferType_;
  int index_;
  int bufferFullCounter_;
};

#endif /* ECMCDATASTORAGE_H_ */
