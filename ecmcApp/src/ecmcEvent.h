/*
 * ecmcDataRecorder.h
 *
 *  Created on: May 27, 2016
 *      Author: anderssandstrom
 */

#ifndef ECMCEVENT_H_
#define ECMCEVENT_H_

#include "stdio.h"

#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcEcEntry.h"
#include "ecmcDataStorage.h"
#include "ecmcEcEntryLink.h"
#include "ecmcEventConsumer.h"

//Event
#define ERROR_EVENT_TRIGGER_ECENTRY_NULL 0x20301
#define ERROR_EVENT_INVALID_SAMPLE_TIME 0x20302
#define ERROR_EVENT_ECENTRY_READ_FAIL 0x20303
#define ERROR_EVENT_ARM_ECENTRY_NULL 0x20304
#define ERROR_EVENT_CONSUMER_INDEX_OUT_OF_RANGE 0x20305
#define ERROR_EVENT_HARDWARE_STATUS_NOT_OK 0x20306
#define ERROR_EVENT_ARM_NOT_ENABLED 0x20307

class ecmcEvent : public ecmcEcEntryLink
{
public:
  ecmcEvent(double sampleTime, int index);
  ~ecmcEvent();
  int setEventType(eventType recordType);
  int setTriggerEdge(triggerEdgeType triggerEdge);
  int setDataSampleTime(int sampleTime);
  int setExecute(int execute);
  int execute(int masterOK);
  int setEnableArmSequence(int enable);
  int linkEventConsumer(ecmcEventConsumer *consumer,int index);
  int validate();
  void setInStartupPhase(bool startup);
  void printStatus();
  int setEnablePrintOuts(bool enable);
  int callConsumers(int masterOK);
  int triggerEvent(int masterOK);
  int arm();
private:
  void initVars();
  int armSequence();
  double sampleTime_;
  int dataSampleTime_;
  int dataSampleTimeCounter_;
  int execute_;
  int enableArmSequence_;
  int armState_;
  int armStateCounter_;
  eventType eventType_;
  uint64_t trigger_;
  uint64_t triggerOld_;
  triggerEdgeType triggerEdge_;
  int inStartupPhase_;
  int enableDiagnosticPrintouts_;
  int armed_;
  int index_;
  bool eventTriggered_;
  ecmcEventConsumer *consumers_[ECMC_MAX_EVENT_CONSUMERS];
  bool reArm_;
};

#endif /* ECMCEVENT_H_ */