/*
 *    Copyright (C) 2020 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef GENERICWORKER_H
#define GENERICWORKER_H

#include "config.h"
#include <stdint.h>
#include <qlog/qlog.h>

#if Qt5_FOUND
	#include <QtWidgets>
#else
	#include <QtGui>
#endif
#include <ui_mainUI.h>
#include <CommonBehavior.h>

#include <CameraRGBDSimple.h>
#include <CameraRGBDSimplePub.h>
#include <CoppeliaUtils.h>
#include <DSRGetID.h>
#include <GenericBase.h>
#include <JointMotor.h>
#include <JointMotorPub.h>
#include <Laser.h>
#include <LaserPub.h>
#include <OmniRobot.h>
#include <OmniRobotPub.h>


#define CHECK_PERIOD 5000
#define BASIC_PERIOD 100


using TuplePrx = std::tuple<RoboCompDSRGetID::DSRGetIDPrxPtr,RoboCompCoppeliaUtils::CoppeliaUtilsPrxPtr,RoboCompOmniRobot::OmniRobotPrxPtr>;


class GenericWorker : public QMainWindow, public Ui_guiDlg
{
Q_OBJECT
public:
	GenericWorker(TuplePrx tprx);
	virtual ~GenericWorker();
	virtual void killYourSelf();
	virtual void setPeriod(int p);

	virtual bool setParams(RoboCompCommonBehavior::ParameterList params) = 0;
	QMutex *mutex;


	RoboCompCoppeliaUtils::CoppeliaUtilsPrxPtr coppeliautils_proxy;
	RoboCompDSRGetID::DSRGetIDPrxPtr dsrgetid_proxy;
	RoboCompOmniRobot::OmniRobotPrxPtr omnirobot_proxy;

	virtual void CameraRGBDSimplePub_pushRGBD (RoboCompCameraRGBDSimple::TImage im, RoboCompCameraRGBDSimple::TDepth dep) = 0;
	virtual void JointMotorPub_motorStates (RoboCompJointMotor::MotorStateMap mstateMap) = 0;
	virtual void LaserPub_pushLaserData (RoboCompLaser::TLaserData laserData) = 0;
	virtual void OmniRobotPub_pushBaseState (RoboCompGenericBase::TBaseState state) = 0;

protected:

	QTimer timer;
	int Period;

private:


public slots:
	virtual void compute() = 0;
	virtual void initialize(int period) = 0;
	
signals:
	void kill();
};

#endif
