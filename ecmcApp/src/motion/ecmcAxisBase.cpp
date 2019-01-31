/*
 * cMcuAxisBase.cpp
 *
 *  Created on: Mar 10, 2016
 *      Author: anderssandstrom
 */

#include "ecmcAxisBase.h"
#include <inttypes.h>
#include <stdint.h>
#include <string>
#include <new>


ecmcAxisBase::ecmcAxisBase(int axisID, double sampleTime) {
  PRINT_ERROR_PATH("axis[%d].error", axisID);
  initVars();
  data_.axisId_                   = axisID;
  data_.sampleTime_               = sampleTime;
  data_.command_.operationModeCmd = ECMC_MODE_OP_AUTO;
  // currently two commands
  commandTransform_               = new ecmcCommandTransform(2, ECMC_MAX_AXES);
  commandTransform_->addCmdPrefix(TRANSFORM_EXPR_COMMAND_EXECUTE_PREFIX,
                                  ECMC_CMD_TYPE_EXECUTE);
  commandTransform_->addCmdPrefix(TRANSFORM_EXPR_COMMAND_ENABLE_PREFIX,
                                  ECMC_CMD_TYPE_ENABLE);
  externalInputTrajectoryIF_ = new ecmcMasterSlaveIF(data_.axisId_,
                                                     ECMC_TRAJECTORY_INTERFACE,
                                                     data_.sampleTime_);
  externalInputEncoderIF_ = new ecmcMasterSlaveIF(data_.axisId_,
                                                  ECMC_ENCODER_INTERFACE,
                                                  data_.sampleTime_);
  enc_  = new ecmcEncoder(&data_, data_.sampleTime_);
  traj_ = new ecmcTrajectoryTrapetz(&data_,
                                    data_.sampleTime_);
  mon_ = new ecmcMonitor(&data_);
  seq_.setAxisDataRef(&data_);
  seq_.setTraj(traj_);
  seq_.setMon(mon_);
  seq_.setEnc(enc_);
  int error = seq_.setExtTrajIF(externalInputTrajectoryIF_);

  if (error) {
    setErrorID(__FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_AXIS_ASSIGN_EXT_INTERFACE_TO_SEQ_FAILED);
  }
}

ecmcAxisBase::~ecmcAxisBase() {
  delete enc_;
  delete traj_;
  delete mon_;
  delete commandTransform_;
  delete externalInputEncoderIF_;
  delete externalInputTrajectoryIF_;
}

void ecmcAxisBase::printAxisState() {
  switch (axisState_) {
  case ECMC_AXIS_STATE_STARTUP:
    LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_STARTUP;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_);
    break;

  case ECMC_AXIS_STATE_DISABLED:
    LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_DISABLED;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_);
    break;

  case ECMC_AXIS_STATE_ENABLED:
    LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_ENABLED;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_);
    break;

  default:
    LOGINFO15("%s/%s:%d: axis[%d].state=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              axisState_);
    break;
  }
}

void ecmcAxisBase::printCurrentState() {
  // called by derived classes
  printAxisState();
  LOGINFO15("%s/%s:%d: axis[%d].reset=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.command_.reset > 0);
  LOGINFO15("%s/%s:%d: axis[%d].enableCascadedCommands=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            cascadedCommandsEnable_ > 0);
  LOGINFO15("%s/%s:%d: axis[%d].enableCommandsTransform=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            enableCommandTransform_ > 0);
  LOGINFO15("%s/%s:%d: axis[%d].inStartupPhase=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.status_.inStartupPhase > 0);
  LOGINFO15("%s/%s:%d: axis[%d].inRealtime=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.status_.inRealtime > 0);
  LOGINFO15("%s/%s:%d: axis[%d].enable=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.command_.enable > 0);
  LOGINFO15("%s/%s:%d: axis[%d].enabled=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.status_.enabled > 0);
  LOGINFO15("%s/%s:%d: axis[%d].moving=%d;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            data_.status_.moving > 0);
}

void ecmcAxisBase::preExecute(bool masterOK) {
  data_.interlocks_.etherCatMasterInterlock = !masterOK;
  data_.refreshInterlocks();

  statusData_.onChangeData.trajSource =
    externalInputTrajectoryIF_->getDataSourceType();
  statusData_.onChangeData.encSource =
    externalInputEncoderIF_->getDataSourceType();
  data_.status_.moving = std::abs(
    data_.status_.currentVelocityActual) > 0;

  if (externalInputEncoderIF_->getDataSourceType() ==
      ECMC_DATA_SOURCE_INTERNAL) {
    enc_->readEntries();
  }

  // Axis state machine
  switch (axisState_) {
  case ECMC_AXIS_STATE_STARTUP:
    setEnable(false);
    data_.status_.busy       = false;
    data_.status_.distToStop = 0;

    if (masterOK) {
      // Auto reset hardware error if starting up
      if ((getErrorID() == ERROR_AXIS_HARDWARE_STATUS_NOT_OK) &&
          data_.status_.inStartupPhase) {
        errorReset();
        setInStartupPhase(false);
        enc_->setToZeroIfRelative();
      }
      LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_DISABLED;\n",
                __FILE__,
                __FUNCTION__,
                __LINE__,
                data_.axisId_);
      axisState_ = ECMC_AXIS_STATE_DISABLED;
    }
    break;

  case ECMC_AXIS_STATE_DISABLED:
    data_.status_.busy       = false;
    data_.status_.distToStop = 0;

    if (data_.status_.enabled) {
      LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_ENABLED;\n",
                __FILE__,
                __FUNCTION__,
                __LINE__,
                data_.axisId_);
      axisState_ = ECMC_AXIS_STATE_ENABLED;
    }

    if (!masterOK) {
      LOGERR(
        "Axis %d: State change (ECMC_AXIS_STATE_DISABLED->ECMC_AXIS_STATE_STARTUP).\n",
        data_.axisId_);
      axisState_ = ECMC_AXIS_STATE_STARTUP;
    }
    break;

  case ECMC_AXIS_STATE_ENABLED:
    data_.status_.distToStop = traj_->distToStop(
      data_.status_.currentVelocitySetpoint);

    if (data_.command_.trajSource == ECMC_DATA_SOURCE_INTERNAL) {
      data_.status_.currentTargetPosition = traj_->getTargetPos();
    } else {  // Synchronized to other axis
      data_.status_.currentTargetPosition =
        data_.status_.currentPositionSetpoint;
    }

    if (!data_.status_.enabled) {
      LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_DISABLED;\n",
                __FILE__,
                __FUNCTION__,
                __LINE__,
                data_.axisId_);
      axisState_ = ECMC_AXIS_STATE_DISABLED;
    }

    if (!masterOK) {
      LOGERR(
        "Axis %d: State change (ECMC_AXIS_STATE_ENABLED->ECMC_AXIS_STATE_STARTUP).\n",
        data_.axisId_);
      LOGINFO15("%s/%s:%d: axis[%d].state=ECMC_AXIS_STATE_STARTUP;\n",
                __FILE__,
                __FUNCTION__,
                __LINE__,
                data_.axisId_);
      axisState_ = ECMC_AXIS_STATE_STARTUP;
    }

    break;
  }
  mon_->readEntries();
  refreshExternalInputSources();
}

void ecmcAxisBase::postExecute(bool masterOK) {
  // Write encoder entries
  enc_->writeEntries();

  if (data_.status_.busyOld != data_.status_.busy) {
    LOGINFO15("%s/%s:%d: axis[%d].busy=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              data_.status_.busy > 0);
  }
  data_.status_.busyOld = data_.status_.busy;

  if (data_.status_.enabledOld != data_.status_.enabled) {
    LOGINFO15("%s/%s:%d: axis[%d].enabled=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              data_.status_.enabled > 0);
  }
  data_.status_.enabledOld = data_.status_.enabled;

  data_.status_.executeOld                 = getExecute();
  data_.status_.currentPositionSetpointOld =
    data_.status_.currentPositionSetpoint;
  data_.status_.cntrlOutputOld = data_.status_.cntrlOutput;
  cycleCounter_++;
  refreshDebugInfoStruct();

  // Update asyn parameters
  if (updateDefAsynParams_ && asynPortDriver_) {
    if ((asynUpdateCycleCounter_ >= asynUpdateCycles_) &&
        asynPortDriver_->getAllowRtThreadCom()) {
      asynUpdateCycleCounter_ = 0;
      asynPortDriver_->setDoubleParam(asynParIdActPos_,
                                      data_.status_.currentPositionActual);
      asynPortDriver_->setDoubleParam(asynParIdSetPos_,
                                      data_.status_.currentPositionSetpoint);
    } else {
      asynUpdateCycleCounter_++;
    }
  }

  if (asynPortDriverDiag_ && (asynParIdDiag_ >= 0)) {
    if ((asynUpdateCycleCounterDiag_ >= asynUpdateCyclesDiag_) &&
        updateAsynParamsDiag_ && asynPortDriverDiag_->getAllowRtThreadCom()) {
      int  bytesUsed = 0;
      char diagBuffer[1024];
      int  error = getAxisDebugInfoData(&diagBuffer[0],
                                        sizeof(diagBuffer),
                                        &bytesUsed);

      if (error) {
        LOGERR(
          "%s/%s:%d: Fail to update asyn par axis<id>.diag. Buffer to small.\n",
          __FILE__,
          __FUNCTION__,
          __LINE__);
      } else {
        asynPortDriverDiag_->doCallbacksInt8Array(reinterpret_cast<epicsInt8 *>(diagBuffer),
                                                  bytesUsed,
                                                  asynParIdDiag_,
                                                  0);
      }
      asynUpdateCycleCounterDiag_ = 0;
    } else {
      asynUpdateCycleCounterDiag_++;
    }
  }

  // Update status entry if linked
  if (statusOutputEntry_) {
    statusOutputEntry_->writeValue(getErrorID() == 0);
  }
}

axisType ecmcAxisBase::getAxisType() {
  return data_.axisType_;
}

int ecmcAxisBase::getAxisID() {
  return data_.axisId_;
}

void ecmcAxisBase::setReset(bool reset) {
  if (data_.command_.reset != reset) {
    LOGINFO15("%s/%s:%d: axis[%d].reset=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              reset);
  }

  data_.command_.reset = reset;

  if (reset) {
    errorReset();

    if (getMon() != NULL) {
      getMon()->errorReset();
    }

    if (getEnc() != NULL) {
      getEnc()->errorReset();
    }

    if (getTraj() != NULL) {
      getTraj()->errorReset();
    }

    if (getSeq() != NULL) {
      getSeq()->errorReset();
    }

    if (getDrv() != NULL) {
      getDrv()->errorReset();
    }

    if (getCntrl() != NULL) {
      getCntrl()->errorReset();
    }
  }
}

bool ecmcAxisBase::getReset() {
  return data_.command_.reset;
}

void ecmcAxisBase::initVars() {
  // errorReset();  //THIS IS NONO..
  data_.axisType_              = ECMC_AXIS_TYPE_BASE;
  data_.command_.reset         = false;
  cascadedCommandsEnable_      = false;
  enableCommandTransform_      = false;
  data_.status_.inStartupPhase = false;

  for (int i = 0; i < ECMC_MAX_AXES; i++) {
    axes_[i] = NULL;
  }
  data_.status_.inRealtime = false;

  data_.status_.externalTrajectoryPosition  = 0;
  data_.status_.externalTrajectoryVelocity  = 0;
  data_.status_.externalTrajectoryInterlock = ECMC_INTERLOCK_EXTERNAL;

  data_.status_.externalEncoderPosition  = 0;
  data_.status_.externalEncoderVelocity  = 0;
  data_.status_.externalEncoderInterlock = ECMC_INTERLOCK_EXTERNAL;

  data_.status_.currentPositionActual   = 0;
  data_.status_.currentPositionSetpoint = 0;
  data_.status_.currentVelocityActual   = 0;
  data_.status_.currentVelocitySetpoint = 0;

  data_.sampleTime_ = 1 / 1000;
  memset(&statusData_,    0, sizeof(statusData_));
  memset(&statusDataOld_, 0, sizeof(statusDataOld_));
  printHeaderCounter_      = 0;
  data_.status_.enabledOld = false;
  data_.status_.enableOld  = false;
  data_.status_.executeOld = false;
  cycleCounter_            = 0;
  axisState_               = ECMC_AXIS_STATE_STARTUP;
  oldPositionAct_          = 0;
  oldPositionSet_          = 0;
  asynPortDriver_          = NULL;
  updateDefAsynParams_     = 0;
  asynParIdActPos_         = 0;
  asynParIdSetPos_         = 0;
  asynUpdateCycleCounter_  = 0;
  asynUpdateCycles_        = 0;

  asynPortDriverDiag_         = NULL;
  updateAsynParamsDiag_       = 0;
  asynParIdDiag_              = -1;
  asynUpdateCycleCounterDiag_ = 0;
  asynUpdateCyclesDiag_       = 0;
  statusOutputEntry_          = 0;
  blockExtCom_                = 0;
}

int ecmcAxisBase::setEnableCascadedCommands(bool enable) {
  if (cascadedCommandsEnable_ != enable) {
    LOGINFO15("%s/%s:%d: axis[%d].enableCascadedCommands=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              enable);
  }
  cascadedCommandsEnable_ = enable;
  return 0;
}

bool ecmcAxisBase::getCascadedCommandsEnabled() {
  return cascadedCommandsEnable_;
}

int ecmcAxisBase::setAxisArrayPointer(ecmcAxisBase *axis, int index) {
  if ((index >= ECMC_MAX_AXES) || (index < 0)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_INDEX_OUT_OF_RANGE);
  }
  axes_[index] = axis;
  return 0;
}

bool ecmcAxisBase::checkAxesForEnabledTransfromCommands(commandType type) {
  for (int i = 0; i < ECMC_MAX_AXES; i++) {
    if (axes_[i] != NULL) {
      if (axes_[i]->getCascadedCommandsEnabled()) {
        return true;
      }
    }
  }
  return false;
}

int ecmcAxisBase::setEnableCommandsTransform(bool enable) {
  if (data_.status_.inRealtime) {
    if (enable) {
      int error = commandTransform_->validate();

      if (error) {
        return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
      }
    }
  }

  if (enableCommandTransform_ != enable) {
    LOGINFO15("%s/%s:%d: axis[%d].enableCommandsTransform=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              enable > 0);
  }
  enableCommandTransform_ = enable;

  return 0;
}

bool ecmcAxisBase::getEnableCommandsTransform() {
  return enableCommandTransform_;
}

int ecmcAxisBase::fillCommandsTransformData() {
  int error = 0;

  // Execute
  for (int i = 0; i < ECMC_MAX_AXES; i++) {
    if (axes_[i] != NULL) {
      error = commandTransform_->setData(axes_[i]->getExecute(),
                                         ECMC_CMD_TYPE_EXECUTE,
                                         i);

      if (error) {
        return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
      }
    } else {
      error = commandTransform_->setData(0, ECMC_CMD_TYPE_EXECUTE, i);

      if (error) {
        return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
      }
    }
  }

  // Enable
  for (int i = 0; i < ECMC_MAX_AXES; i++) {
    if (axes_[i] != NULL) {
      error = commandTransform_->setData(axes_[i]->getEnable(),
                                         ECMC_CMD_TYPE_ENABLE,
                                         i);

      if (error) {
        return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
      }
    } else {
      error = commandTransform_->setData(0, ECMC_CMD_TYPE_ENABLE, i);

      if (error) {
        return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
      }
    }
  }
  return 0;
}

int ecmcAxisBase::setCommandsTransformExpression(std::string expression) {
  LOGINFO15("%s/%s:%d: axis[%d].commandTransformExpression=%s;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            expression.c_str());
  return commandTransform_->setExpression(expression);
}

int ecmcAxisBase::setEnable_Transform() {
  // Atleast one axis have enabled getting execute from transform
  if (checkAxesForEnabledTransfromCommands(ECMC_CMD_TYPE_ENABLE) &&
      enableCommandTransform_) {
    if (!commandTransform_->getCompiled()) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_AXIS_TRANSFORM_ERROR_OR_NOT_COMPILED);
    }
    int error = fillCommandsTransformData();

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }

    // Execute transform
    commandTransform_->refresh();

    // write changes to axes
    for (int i = 0; i < ECMC_MAX_AXES; i++) {
      if (commandTransform_ == NULL) {
        return setErrorID(__FILE__,
                          __FUNCTION__,
                          __LINE__,
                          ERROR_AXIS_INVERSE_TRANSFORM_NULL);
      }

      if (axes_[i] != NULL) {
        if (axes_[i]->getCascadedCommandsEnabled() &&
            commandTransform_->getDataChanged(ECMC_CMD_TYPE_ENABLE,
                                              i) && (i != data_.axisId_)) {
          int error =
            axes_[i]->setEnable(commandTransform_->getData(ECMC_CMD_TYPE_ENABLE,
                                                           i));

          if (error) {
            return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
          }
        }
      }
    }
  }

  return 0;
}

int ecmcAxisBase::setExecute_Transform() {
  // Atleast one axis have enabled getting execute from transform
  if (checkAxesForEnabledTransfromCommands(ECMC_CMD_TYPE_EXECUTE) &&
      enableCommandTransform_) {
    if (!commandTransform_->getCompiled()) {
      LOGINFO7(
        "%s/%s:%d: Error: Command transform not compiled for axis %d.\n",
        __FILE__,
        __FUNCTION__,
        __LINE__,
        data_.axisId_);
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_AXIS_TRANSFORM_ERROR_OR_NOT_COMPILED);
    }

    int error = fillCommandsTransformData();

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }

    // Execute transform
    commandTransform_->refresh();

    // write changes to axes
    for (int i = 0; i < ECMC_MAX_AXES; i++) {
      if (axes_[i] != NULL) {
        if (axes_[i]->getCascadedCommandsEnabled() &&
            commandTransform_->getDataChanged(ECMC_CMD_TYPE_EXECUTE,
                                              i) && (i != data_.axisId_)) {
          int error =
            axes_[i]->setExecute(commandTransform_->getData(
                                   ECMC_CMD_TYPE_EXECUTE,
                                   i));
          if (error) {
            return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
          }
        }
      }
    }
  }
  return 0;
}

ecmcCommandTransform * ecmcAxisBase::getCommandTransform() {
  return commandTransform_;
}

void ecmcAxisBase::setInStartupPhase(bool startup) {
  if (data_.status_.inStartupPhase != startup) {
    LOGINFO15("%s/%s:%d: axis[%d].inStartupPhase=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              startup);
  }
  data_.status_.inStartupPhase = startup;
}

int ecmcAxisBase::setDriveType(ecmcDriveTypes driveType) {
  return setErrorID(__FILE__,
                    __FUNCTION__,
                    __LINE__,
                    ERROR_AXIS_FUNCTION_NOT_SUPPRTED);
}

int ecmcAxisBase::setTrajTransformExpression(std::string expressionString) {
  ecmcCommandTransform *transform =
    externalInputTrajectoryIF_->getExtInputTransform();

  if (!transform) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_FUNCTION_NOT_SUPPRTED);
  }

  LOGINFO15("%s/%s:%d: axis[%d].trajTransformExpression=%s;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            expressionString.c_str());

  int error = transform->setExpression(expressionString);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  error = externalInputTrajectoryIF_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  return 0;
}

int ecmcAxisBase::setEncTransformExpression(std::string expressionString) {
  ecmcCommandTransform *transform =
    externalInputEncoderIF_->getExtInputTransform();

  if (!transform) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_FUNCTION_NOT_SUPPRTED);
  }

  LOGINFO15("%s/%s:%d: axis[%d].encTransformExpression=%s;\n",
            __FILE__,
            __FUNCTION__,
            __LINE__,
            data_.axisId_,
            expressionString.c_str());

  int error = transform->setExpression(expressionString);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  error = externalInputEncoderIF_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  return 0;
}

int ecmcAxisBase::setTrajDataSourceType(dataSource refSource) {
  if (getEnable() && (refSource != ECMC_DATA_SOURCE_INTERNAL)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_COMMAND_NOT_ALLOWED_WHEN_ENABLED);
  }

  // If realtime: Ensure that transform object is compiled and ready to go
  if ((refSource != ECMC_DATA_SOURCE_INTERNAL) && data_.status_.inRealtime) {
    ecmcCommandTransform *transform =
      externalInputTrajectoryIF_->getExtInputTransform();

    if (!transform) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_TRAJ_TRANSFORM_NULL);
    }
    int error = transform->validate();

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }
  }

  // Check if object is ok to go to refSource
  int error = externalInputTrajectoryIF_->validate(refSource);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }
  error = externalInputTrajectoryIF_->setDataSourceType(refSource);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  if (refSource != ECMC_DATA_SOURCE_INTERNAL) {
    data_.interlocks_.noExecuteInterlock = false;
    data_.refreshInterlocks();
    data_.status_.busy = true;
  }

  if (data_.command_.trajSource != refSource) {
    LOGINFO15("%s/%s:%d: axis[%d].trajDataSourceType=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              refSource);
  }
  data_.command_.trajSource = refSource;
  return 0;
}

int ecmcAxisBase::setEncDataSourceType(dataSource refSource) {
  if (getEnable()) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_COMMAND_NOT_ALLOWED_WHEN_ENABLED);
  }

  // If realtime: Ensure that ethercat enty for actual position is linked
  if ((refSource == ECMC_DATA_SOURCE_INTERNAL) && data_.status_.inRealtime) {
    int error = getEnc()->validateEntry(
      ECMC_ENCODER_ENTRY_INDEX_ACTUAL_POSITION);

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }
  }

  // If realtime: Ensure that transform object is compiled and ready to go
  if ((refSource != ECMC_DATA_SOURCE_INTERNAL) && data_.status_.inRealtime) {
    ecmcCommandTransform *transform =
      externalInputEncoderIF_->getExtInputTransform();

    if (!transform) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_TRAJ_TRANSFORM_NULL);
    }
    int error = transform->validate();

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }
  }

  // Check if object is ok to go to refSource
  int error = externalInputEncoderIF_->validate(refSource);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  error = externalInputEncoderIF_->setDataSourceType(refSource);

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  if (data_.command_.encSource != refSource) {
    LOGINFO15("%s/%s:%d: axis[%d].encDataSourceType=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              refSource);
  }

  data_.command_.encSource = refSource;
  return 0;
}

int ecmcAxisBase::setRealTimeStarted(bool realtime) {
  if (data_.status_.inRealtime != realtime) {
    LOGINFO15("%s/%s:%d: axis[%d].inRealtime=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              realtime);
  }
  data_.status_.inRealtime = realtime;
  return 0;
}

bool ecmcAxisBase::getError() {
  int error = ecmcAxisBase::getErrorID();

  if (error) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }
  return ecmcError::getError();
}

int ecmcAxisBase::getErrorID() {
  // GeneralsetErrorID
  if (ecmcError::getError()) {
    return ecmcError::getErrorID();
  }

  // Monitor
  ecmcMonitor *mon = getMon();

  if (mon) {
    if (mon->getError()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, mon->getErrorID());
    }
  }

  // Encoder
  ecmcEncoder *enc = getEnc();

  if (enc) {
    if (enc->getError()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, enc->getErrorID());
    }
  }

  // Drive
  ecmcDriveBase *drv = getDrv();

  if (drv) {
    if (drv->getError()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, drv->getErrorID());
    }
  }

  // Trajectory
  ecmcTrajectoryTrapetz *traj = getTraj();

  if (traj) {
    if (traj->getError()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, traj->getErrorID());
    }
  }

  // Controller
  ecmcPIDController *cntrl = getCntrl();

  if (cntrl) {
    if (cntrl->getError()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, cntrl->getErrorID());
    }
  }

  // Sequencer
  ecmcAxisSequencer *seq = getSeq();

  if (seq) {
    if (seq->getErrorID()) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, seq->getErrorID());
    }
  }

  return ecmcError::getErrorID();
}

int ecmcAxisBase::setEnableLocal(bool enable) {
  if (enable && !data_.command_.enable) {
    traj_->setStartPos(data_.status_.currentPositionActual);
    traj_->setCurrentPosSet(data_.status_.currentPositionActual);
    traj_->setTargetPos(data_.status_.currentPositionActual);
    data_.status_.currentTargetPosition = data_.status_.currentPositionActual;
  }
  traj_->setEnable(enable);

  if (data_.command_.enable != enable) {
    LOGINFO15("%s/%s:%d: axis[%d].enable=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              enable);
  }

  data_.status_.enableOld = data_.command_.enable;
  data_.command_.enable   = enable;
  return 0;
}

void ecmcAxisBase::errorReset() {
  // Monitor
  ecmcMonitor *mon = getMon();

  if (mon) {
    mon->errorReset();
  }

  // Encoder
  ecmcEncoder *enc = getEnc();

  if (enc) {
    enc->errorReset();
  }

  // Drive
  ecmcDriveBase *drv = getDrv();

  if (drv) {
    drv->errorReset();
  }

  // Trajectory
  ecmcTrajectoryTrapetz *traj = getTraj();

  if (traj) {
    traj->errorReset();
  }

  // Controller
  ecmcPIDController *cntrl = getCntrl();

  if (cntrl) {
    cntrl->errorReset();
  }

  // Sequencer
  ecmcAxisSequencer *seq = getSeq();

  if (seq) {
    seq->errorReset();
  }

  ecmcError::errorReset();
}

int ecmcAxisBase::refreshExternalInputSources() {
  // Trajectory

  int error = externalInputTrajectoryIF_->refreshInputs();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }
  data_.status_.externalTrajectoryPosition =
    externalInputTrajectoryIF_->getInputPos();
  data_.status_.externalTrajectoryVelocity =
    externalInputTrajectoryIF_->getInputVel();
  data_.interlocks_.trajTransformInterlock =
    externalInputTrajectoryIF_->getInputIlock();
  data_.refreshInterlocks();

  // Encoder
  error = externalInputEncoderIF_->refreshInputs();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }
  data_.status_.externalEncoderPosition =
    externalInputEncoderIF_->getInputPos();
  data_.status_.externalEncoderVelocity =
    externalInputEncoderIF_->getInputVel();
  data_.interlocks_.encTransformInterlock =
    externalInputEncoderIF_->getInputIlock();
  data_.refreshInterlocks();
  return 0;
}

int ecmcAxisBase::refreshExternalOutputSources() {
  externalInputTrajectoryIF_->getOutputDataInterface()->setPosition(
    data_.status_.currentPositionSetpoint);
  externalInputTrajectoryIF_->getOutputDataInterface()->setVelocity(
    data_.status_.currentVelocitySetpoint);

  externalInputEncoderIF_->getOutputDataInterface()->setPosition(
    data_.status_.currentPositionActual);
  externalInputEncoderIF_->getOutputDataInterface()->setVelocity(
    data_.status_.currentVelocityActual);

  if (getMon()) {
    externalInputEncoderIF_->getOutputDataInterface()->setInterlock(
      data_.interlocks_.trajSummaryInterlockFWD ||
      data_.interlocks_.trajSummaryInterlockBWD);
    externalInputTrajectoryIF_->getOutputDataInterface()->setInterlock(
      data_.interlocks_.trajSummaryInterlockFWD ||
      data_.interlocks_.trajSummaryInterlockBWD);
  }
  return 0;
}

ecmcMasterSlaveIF * ecmcAxisBase::getExternalTrajIF() {
  return externalInputTrajectoryIF_;
}

ecmcMasterSlaveIF * ecmcAxisBase::getExternalEncIF() {
  return externalInputEncoderIF_;
}

int ecmcAxisBase::validateBase() {
  int error = externalInputEncoderIF_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  error = externalInputTrajectoryIF_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  return 0;
}

int ecmcAxisBase::getPosAct(double *pos) {
  *pos = data_.status_.currentPositionActual;
  return 0;
}

int ecmcAxisBase::getVelAct(double *vel) {
  *vel = data_.status_.currentVelocityActual;
  return 0;
}

int ecmcAxisBase::getPosSet(double *pos) {
  if ((externalInputTrajectoryIF_->getDataSourceType() ==
       ECMC_DATA_SOURCE_INTERNAL) && getSeq()) {
    *pos = data_.command_.positionTarget;
  } else {
    *pos = data_.status_.currentPositionSetpoint;
  }

  return 0;
}

ecmcEncoder * ecmcAxisBase::getEnc() {
  return enc_;
}

ecmcTrajectoryTrapetz * ecmcAxisBase::getTraj() {
  return traj_;
}

ecmcMonitor * ecmcAxisBase::getMon() {
  return mon_;
}

ecmcAxisSequencer * ecmcAxisBase::getSeq() {
  return &seq_;
}

int ecmcAxisBase::getAxisHomed(bool *homed) {
  *homed = enc_->getHomed();
  return 0;
}

int ecmcAxisBase::setAxisHomed(bool homed) {
  enc_->setHomed(homed);
  return 0;
}

int ecmcAxisBase::getEncScaleNum(double *scale) {
  *scale = enc_->getScaleNum();
  return 0;
}

int ecmcAxisBase::setEncScaleNum(double scale) {
  enc_->setScaleNum(scale);
  return 0;
}

int ecmcAxisBase::getEncScaleDenom(double *scale) {
  *scale = enc_->getScaleDenom();
  return 0;
}

int ecmcAxisBase::setEncScaleDenom(double scale) {
  enc_->setScaleDenom(scale);
  return 0;
}

int ecmcAxisBase::getEncPosRaw(int64_t *rawPos) {
  *rawPos = enc_->getRawPosMultiTurn();
  return 0;
}

int ecmcAxisBase::setCommand(motionCommandTypes command) {
  seq_.setCommand(command);

  return 0;
}

int ecmcAxisBase::setCmdData(int cmdData) {
  seq_.setCmdData(cmdData);
  return 0;
}

motionCommandTypes ecmcAxisBase::getCommand() {
  return seq_.getCommand();
}

int ecmcAxisBase::getCmdData() {
  return seq_.getCmdData();
}

int ecmcAxisBase::slowExecute() {
  if (oldPositionAct_ != data_.status_.currentPositionActual) {
    LOGINFO15("%s/%s:%d: axis[%d].encoder.actPos=%lf;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              data_.status_.currentPositionActual);
  }
  oldPositionAct_ = data_.status_.currentPositionActual;

  if (oldPositionSet_ != data_.status_.currentPositionSetpoint) {
    LOGINFO15("%s/%s:%d: axis[%d].trajectory.currentPositionSetpoint=%lf;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              data_.status_.currentPositionSetpoint);
  }
  oldPositionSet_ = data_.status_.currentPositionSetpoint;

  if (data_.status_.movingOld != data_.status_.moving) {
    LOGINFO15("%s/%s:%d: axis[%d].moving=%d;\n",
              __FILE__,
              __FUNCTION__,
              __LINE__,
              data_.axisId_,
              data_.status_.moving > 0);
  }
  data_.status_.movingOld = data_.status_.moving;

  return 0;
}

void ecmcAxisBase::printAxisStatus() {
  if (memcmp(&statusDataOld_.onChangeData, &statusData_.onChangeData,
             sizeof(statusData_.onChangeData)) == 0) {
    return;  // Printout on change
  }

  statusDataOld_ = statusData_;

  // Only print header once per 25 status lines
  if (printHeaderCounter_ <= 0) {
    LOGINFO(
      "ecmc::  Ax     PosSet     PosAct     PosErr    PosTarg   DistLeft    CntrOut   VelFFSet     VelAct   VelFFRaw VelRaw  Error Co CD St IL LI TS ES En Ex Bu Ta Hd L- L+ Ho\n");
    printHeaderCounter_ = 25;
  }
  printHeaderCounter_--;

  LOGINFO(
    "ecmc:: %3d %10.3lf %10.3lf %10.3lf %10.3lf %10.3lf %10.3lf %10.3lf %10.3lf %10.3lf %6i %6x %2d %2d %2d %2d %2d %2d %2d %1d%1d %2d %2d %2d %2d %2d %2d %2d\n",
    statusData_.axisID,
    statusData_.onChangeData.positionSetpoint,
    statusData_.onChangeData.positionActual,
    statusData_.onChangeData.cntrlError,
    statusData_.onChangeData.positionTarget,
    statusData_.onChangeData.positionError,
    statusData_.onChangeData.cntrlOutput,
    statusData_.onChangeData.velocitySetpoint,
    statusData_.onChangeData.velocityActual,
    statusData_.onChangeData.velocityFFRaw,
    statusData_.onChangeData.velocitySetpointRaw,
    statusData_.onChangeData.error,
    statusData_.onChangeData.command,
    statusData_.onChangeData.cmdData,
    statusData_.onChangeData.seqState,
    statusData_.onChangeData.trajInterlock,
    statusData_.onChangeData.lastActiveInterlock,
    statusData_.onChangeData.trajSource,
    statusData_.onChangeData.encSource,
    statusData_.onChangeData.enable,
    statusData_.onChangeData.enabled,
    statusData_.onChangeData.execute,
    statusData_.onChangeData.busy,
    statusData_.onChangeData.atTarget,
    statusData_.onChangeData.homed,
    statusData_.onChangeData.limitBwd,
    statusData_.onChangeData.limitFwd,
    statusData_.onChangeData.homeSwitch);
}

int ecmcAxisBase::setExecute(bool execute) {
  // Internal trajectory source
  if (externalInputTrajectoryIF_->getDataSourceType() ==
      ECMC_DATA_SOURCE_INTERNAL) {
    // Allow direct homing without enable
    if (execute && !getEnable() &&
        !((data_.command_.cmdData == 15) && (data_.command_.command == 10))) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_AXIS_NOT_ENABLED);
    }

    if (execute && !data_.status_.executeOld && data_.status_.busy) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_AXIS_BUSY);
    }

    int error = seq_.setExecute(execute);

    if (error) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }
  }

  return setExecute_Transform();
}

bool ecmcAxisBase::getExecute() {
  if (externalInputTrajectoryIF_->getDataSourceType() ==
      ECMC_DATA_SOURCE_INTERNAL) {
    return seq_.getExecute();
  } else {
    return true;
  }
}

bool ecmcAxisBase::getBusy() {
  return data_.status_.busy;
}

int ecmcAxisBase::getDebugInfoData(ecmcAxisStatusType *data) {
  if (data == NULL) {
    return ERROR_AXIS_DATA_POINTER_NULL;
  }

  memcpy(data, &statusData_, sizeof(*data));
  return 0;
}

ecmcAxisStatusType * ecmcAxisBase::getDebugInfoDataPointer() {
  return &statusData_;
}

int ecmcAxisBase::getCycleCounter() {
  /// Use for watchdog purpose (will overflow)
  return cycleCounter_;
}

bool ecmcAxisBase::getEnable() {
  return data_.command_.enable;
}

bool ecmcAxisBase::getEnabled() {
  return data_.status_.enabled && data_.command_.enable;
}

int ecmcAxisBase::initAsyn(ecmcAsynPortDriver *asynPortDriver,
                           bool                regAsynParams,
                           int                 skipCycles) {
  asynPortDriver_      = asynPortDriver;
  updateDefAsynParams_ = regAsynParams;
  asynUpdateCycles_    = skipCycles;

  if (!regAsynParams) {
    return 0;
  }

  if (asynPortDriver_ == NULL) {
    LOGERR("%s/%s:%d: ERROR: AsynPortDriver object NULL (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_AXIS_ASYN_PORT_OBJ_NULL);
    return ERROR_AXIS_ASYN_PORT_OBJ_NULL;
  }

  char asynParName[1024];

  // actpos
  int ret = snprintf(asynParName, sizeof(asynParName), "ax%d.actpos", data_.axisId_);

  if ((ret >= static_cast<int>(sizeof(asynParName))) || (ret <= 0)) {
    return ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL;
  }

  asynStatus status = asynPortDriver_->createParam(asynParName,
                                                   asynParamFloat64,
                                                   &asynParIdActPos_);

  if (status != asynSuccess) {
    LOGERR("%s/%s:%d: ERROR: Add default asyn parameter %s failed.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           asynParName);
    return asynError;
  }
  asynPortDriver_->setDoubleParam(asynParIdActPos_, 0);

  // setpos
  ret = snprintf(asynParName, sizeof(asynParName), "ax%d.setpos", data_.axisId_);

  if ((ret >= static_cast<int>(sizeof(asynParName))) || (ret <= 0)) {
    return ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL;
  }

  status = asynPortDriver_->createParam(asynParName,
                                        asynParamFloat64,
                                        &asynParIdSetPos_);

  if (status != asynSuccess) {
    LOGERR("%s/%s:%d: ERROR: Add default asyn parameter %s failed.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           asynParName);
    return asynError;
  }
  asynPortDriver_->setDoubleParam(asynParIdSetPos_, 0);

  asynPortDriver_->callParamCallbacks();
  return 0;
}

int ecmcAxisBase::initDiagAsyn(ecmcAsynPortDriver *asynPortDriver,
                               bool                regAsynParams,
                               int                 skipCycles) {
  asynPortDriverDiag_   = asynPortDriver;
  updateAsynParamsDiag_ = regAsynParams;
  asynUpdateCyclesDiag_ = skipCycles;

  if (!regAsynParams) {
    return 0;
  }

  if (asynPortDriverDiag_ == NULL) {
    LOGERR("%s/%s:%d: ERROR: AsynPortDriver object NULL (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_AXIS_ASYN_PORT_OBJ_NULL);
    return ERROR_AXIS_ASYN_PORT_OBJ_NULL;
  }

  char asynParName[1024];

  // Diagnostic string
  int ret = snprintf(asynParName, sizeof(asynParName), "ax%d.diagnostic", data_.axisId_);

  if ((ret >= 1024) || (ret <= 0)) {
    return ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL;
  }

  asynStatus status = asynPortDriverDiag_->createParam(asynParName,
                                                       asynParamInt8Array,
                                                       &asynParIdDiag_);

  if (status != asynSuccess) {
    LOGERR("%s/%s:%d: ERROR: Add diagnostic asyn parameter %s failed.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           asynParName);
    return asynError;
  }

  return 0;
}

int ecmcAxisBase::getAxisDebugInfoData(char *buffer,
                                       int   bufferByteSize,
                                       int  *bytesUsed) {
  ecmcAxisStatusType data;
  int error = getDebugInfoData(&data);

  if (error) {
    return error;
  }

  // (Ax,PosSet,PosAct,PosErr,PosTarg,DistLeft,CntrOut,VelFFSet,VelAct,VelFFRaw,VelRaw,CycleCounter,Error,Co,CD,St,IL,TS,ES,En,Ena,Ex,Bu,Ta,L-,L+,Ho");
  int ret = snprintf(buffer,
                     bufferByteSize,
                     "%d,%lf,%lf,%lf,%lf,%lf,%" PRId64 ",%lf,%lf,%lf,%lf,%d,%d,%x,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                     data.axisID,
                     data.onChangeData.positionSetpoint,
                     data.onChangeData.positionActual,
                     data.onChangeData.cntrlError,
                     data.onChangeData.positionTarget,
                     data.onChangeData.positionError,
                     data.onChangeData.positionRaw,
                     data.onChangeData.cntrlOutput,
                     data.onChangeData.velocitySetpoint,
                     data.onChangeData.velocityActual,
                     data.onChangeData.velocityFFRaw,
                     data.onChangeData.velocitySetpointRaw,
                     data.cycleCounter,
                     data.onChangeData.error,
                     data.onChangeData.command,
                     data.onChangeData.cmdData,
                     data.onChangeData.seqState,
                     data.onChangeData.trajInterlock,
                     data.onChangeData.lastActiveInterlock,
                     data.onChangeData.trajSource,
                     data.onChangeData.encSource,
                     data.onChangeData.enable,
                     data.onChangeData.enabled,
                     data.onChangeData.execute,
                     data.onChangeData.busy,
                     data.onChangeData.atTarget,
                     data.onChangeData.homed,
                     data.onChangeData.limitBwd,
                     data.onChangeData.limitFwd,
                     data.onChangeData.homeSwitch);

  if ((ret >= bufferByteSize) || (ret <= 0)) {
    *bytesUsed = 0;
    return ERROR_AXIS_PRINT_TO_BUFFER_FAIL;
  }
  *bytesUsed = ret;
  return 0;
}

int ecmcAxisBase::setEcStatusOutputEntry(ecmcEcEntry *entry) {
  statusOutputEntry_ = entry;
  return 0;
}

motionDirection ecmcAxisBase::getAxisSetDirection() {
  if (!data_.status_.enabled) {
    return ECMC_DIR_STANDSTILL;
  }

  // Transform or internal trajectory
  if (data_.command_.trajSource == ECMC_DATA_SOURCE_INTERNAL) {
    return getTraj()->getCurrSetDir();
  } else {
    if (data_.status_.currentPositionSetpoint <
        data_.status_.currentPositionSetpointOld) {
      return ECMC_DIR_BACKWARD;
    } else if (data_.status_.currentPositionSetpoint >
               data_.status_.currentPositionSetpointOld) {
      return ECMC_DIR_FORWARD;
    } else {
      return ECMC_DIR_STANDSTILL;
    }
  }
  return ECMC_DIR_STANDSTILL;
}

int ecmcAxisBase::getBlockExtCom() {
  return blockExtCom_;
}

int ecmcAxisBase::setBlockExtCom(int block) {
  blockExtCom_ = block;
  return 0;
}