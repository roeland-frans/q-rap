 /*
 *    Q-Rap Project
 *
 *    Version     : 0.1
 *    Date        : 2012/04/24
 *    License     : GNU GPLv3
 *    File        : cPosEstimation.cpp
 *    Copyright   : (c) University of Pretoria
 *    Author      : Magdaleen Ballot (magdaleen.ballot@up.ac.za)
 *    Description : This class does the analysis of the measurements in the 
 *			the database
 *
 **************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; See the GNU General Public License for      *
 *   more details                                                          *
 *                                                                         *
 ************************************************************************* */


#include "cPosEstimation.h"
#include <random>


//*********************************************************************
cPosEstimation::cPosEstimation() // default constructor
{
	mCurSiteI = 0;
	mCurPosI = 0;
	mNumPoints = 0;
	mNewTP = 0;
	double kS, kI;
	mUnits = dBm;
	string setting;
	setting = gDb.GetSetting("kFactorServer");
	if(setting!="")
		kS=atof(setting.c_str());
	else kS = 1.25;
	setting = gDb.GetSetting("kFactorInt");
	if(setting!="")
		kI=atof(setting.c_str());
	else kI = 1.25;
	mkFactor = min((kI+2*kS)/3.0,1.33);

	setting = gDb.GetSetting("PlotResolution");
	if(setting!="")
	{
		mPlotResolution = atof(setting.c_str());
	}
	else mPlotResolution = 30;

	mPlotResolution = 30;

	setting = gDb.GetSetting("UseClutter");
	if (setting=="true")
		mUseClutter = true;
	else mUseClutter = false;

	mUseClutter = true;

	mClutterSource = atoi(gDb.GetSetting("ClutterSource").c_str());
	cout << "mClutterSource = " << mClutterSource << endl;
	if (mUseClutter)
		cout << "cPosEstimation constructor1:  Using Clutter " <<  endl;
	else cout << "cPosEstimation constructor1:  NOT Using Clutter " <<  endl;
	if (mUseClutter)
		mUseClutter = mClutter.SetRasterFileRules(mClutterSource);
	if (mUseClutter)
		cout << "cPosEstimation constructor2:  Using Clutter " <<  endl;
	else cout << "cPosEstimation constructor2:  NOT Using Clutter " <<  endl;
	if (mUseClutter)
		mClutterClassGroup = mClutter.GetClutterClassGroup();
	if (mUseClutter)
		cout << "cPosEstimation constructor3:  Using Clutter " <<  endl;
	else cout << "cPosEstimation constructor3:  NOT Using Clutter " <<  endl;
	mUseClutter = (mUseClutter)&&(mClutterClassGroup>0)&&(mClutterClassGroup<9000);
	if (mUseClutter) mClutterCount = new unsigned[mPathLoss.mClutter.mNumber];
	else mClutterCount = new unsigned[2];
	if (mUseClutter) mClutter.SetSampleMethod(1);
	if (mUseClutter)
		cout << "cPosEstimation constructor4:  Using Clutter " <<  endl;
	else cout << "cPosEstimation constructor4:  NOT Using Clutter " <<  endl;

	mDEMsource = atoi(gDb.GetSetting("DEMsource").c_str());
	cout << "mDEMsource = " << mDEMsource << endl;
	mDEM.SetRasterFileRules(mDEMsource);
	mDEM.SetSampleMethod(2);

	mFixedAnts = new cAntennaPattern[2];
	mCellPathLoss = new double[2];
	mNumInsts = 2;
	mCurANNa = new FANN::neural_net();
	mCurANNd = new FANN::neural_net();
}

//*********************************************************************
cPosEstimation::~cPosEstimation() // destructor
{
	unsigned i;
	for (i=0;i<mNumPoints;i++)
	{
		mPosSets[i].sTestPoints.clear();
		mPosSets[i].sMeasurements.clear();
	}
	mPosSets.clear();
	delete [] mFixedAnts;
	delete [] mCellPathLoss;
	delete mCurANNa;
	delete mCurANNd;
}

//*********************************************************************
bool cPosEstimation::LoadMeasurements(vPoints Points,
					unsigned MeasType, 
					unsigned MeasSource,
					unsigned PosSource,
					unsigned Technology)
{
	pqxx::result r;
	double Lat, Lon, mNorth, mSouth, mEast, mWest;
	char *text= new char[33];
	cGeoP NorthWestCorner,SouthEastCorner; 
	Points[0].Get(mNorth,mEast);
	mSouth = mNorth;
	mWest = mEast;
	string query, areaQuery;
	pqxx::result MeasSelect;
	unsigned i;

	double longitude, latitude;
	string PointString;
	unsigned spacePos;
	unsigned Counter=0;
	unsigned NumInPosSet = 0; 
	unsigned siteid, tp,type, oldtype;
	
	areaQuery += " @ ST_GeomFromText('POLYGON((";
	for (i = 0 ; i < Points.size();i++)
   {
		Points[i].Get(Lat, Lon);
     	mNorth = max(mNorth,Lat);
     	mSouth = min(mSouth,Lat);
     	mEast = max(mEast,Lon);
     	mWest = min(mWest,Lon);
		gcvt(Lon,12,text);
     	areaQuery += text;
     	areaQuery += " ";
		gcvt(Lat,12,text);
     	areaQuery += text;
     	areaQuery += ",";
   }
   NorthWestCorner.Set(mNorth,mWest,DEG);
   SouthEastCorner.Set(mSouth,mEast,DEG);
	cout << "North West corner: " << endl;
	NorthWestCorner.Display();
	cout << "South East corner: " << endl;
	SouthEastCorner.Display();
	Points[0].Get(Lat,Lon);
	gcvt(Lon,12,text);
   areaQuery += text;
   areaQuery += " ";
	gcvt(Lat,12,text);
   areaQuery += text;
   areaQuery += "))',4326) ";

	query = "select distinct site.id as siteid, ST_AsText(location) as siteLocation, ";
	query += "type, maxdist, NumInputs, NumOutputs, filename, index, cellid ";
	query += "from site cross join NeuralNet cross join anninputlist ";
	query += "where NeuralNet.siteid=site.id ";
	query += "and anninputlist.annid= NeuralNet.id ";
	query += "and anninputlist.siteid=site.id ";
	query += "and site.location ";
	query += areaQuery;
	query += " order by siteid, type, index; ";

	if (!gDb.PerformRawSql(query))
	{
		string err = "cTrainPosNet::LoadMeasurements: ";
		err +="Problem with database query to get sites from selected area! Problem with query: ";
		err += query;
		cout << err << endl;
		QRAP_ERROR(err.c_str());
		delete [] text;
		return false;
	}

	tCell NewCell;
	tSiteInfo NewSite;
	NewCell.sCI=0;
	NewSite.sSiteID=0;
	Counter = 0;
	oldtype = 1;
	type = 0;

	gDb.GetLastResult(r);
	if (r.size() >0)
	{
		cout << "cPosEstimation::LoadMeasurements:  r.size() = " <<  r.size() << endl;
		for (i=0; i<r.size(); i++)
		{
			type = atoi(r[i]["type"].c_str());
			siteid = atoi(r[i]["siteid"].c_str());
			if (siteid != NewSite.sSiteID)
			{
				if (NewSite.sSiteID>0)
				{
					mSites.push_back(NewSite);
					NewSite.sCellSet.clear();
				}
				NewSite.sSiteID = siteid;
				PointString = r[i]["siteLocation"].c_str();
				spacePos = PointString.find_first_of(' ');
				longitude = atof((PointString.substr(6,spacePos).c_str())); 
				latitude = atof((PointString.substr(spacePos,PointString.length()-1)).c_str());
				NewSite.sPosition.Set(latitude,longitude,DEG);
				NewSite.sNumInputs= atoi(r[i]["NumInputs"].c_str());
				NewSite.sMaxDist = atof(r[i]["maxdist"].c_str());
				Counter=0;
				if (1==type)	
				{
						NewSite.sANNfileA = r[i]["filename"].c_str();
						NewSite.sNumOutputsA= atoi(r[i]["NumOutputs"].c_str());  
				}
				else if (2==type) 
				{
					NewSite.sANNfileD = r[i]["filename"].c_str();
					NewSite.sNumOutputsD= atoi(r[i]["NumOutputs"].c_str()); 
				} 
			}
			else if (type!=oldtype)
			{
				if (1==type)	
				{
						NewSite.sANNfileA = r[i]["filename"].c_str();
						NewSite.sNumOutputsA= atoi(r[i]["NumOutputs"].c_str());  
				}
				else if (2==type) 
				{
					NewSite.sANNfileD = r[i]["filename"].c_str();
					NewSite.sNumOutputsD= atoi(r[i]["NumOutputs"].c_str()); 
				} 
				oldtype=type;
			}
			if (1==type)
			{
					NewCell.sI =Counter;
					NewCell.sCI= atoi(r[i]["cellid"].c_str());
					Counter++;
					NewSite.sCellSet.push_back(NewCell);
			}
		}
		if (NewSite.sSiteID>0)
		{
			mSites.push_back(NewSite);
			NewSite.sCellSet.clear();
		}
	}
	else
	{
		string err = "cTrainPosNet::LoadMeasurements: ";
		err +="Query to get site info from selected area is empty! Problem with query: ";
		err += query;
		cout << err << endl;
		QRAP_ERROR(err.c_str());
		delete [] text;
		return false;
	}
	mNumSites = mSites.size();

	cout << "cPosEstimation::LoadMeasurements: mNumSites = " <<  mNumSites << endl;

	if (Points.size() > 1)
	{
		Points[0].Get(mNorth,mEast);
		mSouth = mNorth;
		mWest = mEast;
		string query;
		pqxx::result MeasSelect;

		query ="SELECT distinct measurement.tp as tp, ST_AsText(testpoint.location) as origLocation,";
		query += "siteid, ST_AsText(site.location) as siteLocation, radioinstallation.id as InstKeyFixed, ";
		query += "txantennaheight, EIRP, txbearing, txmechtilt, txantpatternkey, azibeamwidth, ";
		query += "ci, ST_AsText(centriod) as centriod, measurement.frequency as frequency, ";
		query += "max(measvalue) as measvalue, min(pathloss) as pathloss, ";
		query += "tp1.TA as TA, technology.DistRes as DistRes ";
		query += "from measurement left outer join tplist as tp1 ";
		query += "on ( measurement.tp =tp1.tp and measurement.ci = tp1.servci) "; 
		query += "cross join testpoint cross join cell cross join radioinstallation  ";
		query += "cross join antennapattern cross join site ";
		query += "LEFT OUTER JOIN technology ON (radioinstallation.techkey=technology.id) ";
		query += "where measurement.tp=testpoint.id and measurement.ci=cell.id and siteid=site.id ";
		query += "and cell.risector=radioinstallation.id ";
		query += "and radioinstallation.txantpatternkey = antennapattern.id ";
		query += "and testpoint.positionsource<2 ";
		query += "and site.Location @ ST_GeomFromText('POLYGON((";
		for ( i = 0 ; i < Points.size();i++)
	   {
			Points[i].Get(Lat, Lon);
	      mNorth = max(mNorth,Lat);
	      mSouth = min(mSouth,Lat);
	      mEast = max(mEast,Lon);
	      mWest = min(mWest,Lon);
			gcvt(Lon,12,text);
	      query += text;
	      query += " ";
			gcvt(Lat,12,text);
	      query += text;
	      query += ",";
	   }
	   NorthWestCorner.Set(mNorth,mWest,DEG);
	   SouthEastCorner.Set(mSouth,mEast,DEG);
		cout << "North West corner: " << endl;
		NorthWestCorner.Display();
		cout << "South East corner: " << endl;
		SouthEastCorner.Display();
		Points[0].Get(Lat,Lon);
		gcvt(Lon,12,text);
	   query += text;
	   query += " ";
		gcvt(Lat,12,text);
	   query += text;
	   query += "))',4326) ";

		if (MeasType>0)
		{
			query += " and meastype=";
			gcvt(MeasType,9,text);
			query += text;
		}
		if (MeasSource>0)
		{
			query += " and measdatasource=";
			gcvt(MeasSource,9,text);
			query += text;
		}
		if (PosSource>0)
		{
			query += " and positionsource=";
			gcvt(PosSource,9,text);
			query += text;
		}
		if (Technology>0)
		{
			query += " and Technology.id=";
			gcvt(Technology,9,text);
			query += text;
		}
		query+=" group by siteid, InstKeyFixed, azibeamwidth, origLocation, "; 
		query+=" measurement.frequency, measurement.tp, tp1.TA, ci, siteLocation, centriod, DistRes ";
		query+=" order by measurement.tp, TA, measvalue desc;";

		cout << query << endl;


		if (!gDb.PerformRawSql(query))
		{
			string err ="Problem with database query to get measurements from selected area! Problem with query: ";
			err += query;
			cout << err << endl;
			QRAP_ERROR(err.c_str());
			delete [] text;
			return false;
		}

		gDb.GetLastResult(r);
		if (r.size() >0)
		{
			tTestPoint NewTestPoint;
			tPosSet NewPosSet;
			tMeas NewMeasurement; 
			double longitude, latitude;
			double Distance;
			string PointString;
			unsigned spacePos;

			NewTestPoint.sOriginalTP=0;
			NewTestPoint.sNewTP=0;

			unsigned i;
			for (i = 0; i < r.size();i++)
			{
				tp = atoi(r[i]["tp"].c_str());
				if (tp != NewTestPoint.sOriginalTP)
				{
					NewPosSet.sNumMeas = NewPosSet.sMeasurements.size();
					mPosSets.push_back(NewPosSet);
					NewPosSet.sTestPoints.clear();
					NewPosSet.sMeasurements.clear();
					NumInPosSet = 0;
					NewTestPoint.sOriginalTP = tp;
					NewTestPoint.sNewTP = tp;
					PointString = r[i]["origLocation"].c_str();
					spacePos = PointString.find_first_of(' ');
					longitude = atof((PointString.substr(6,spacePos).c_str())); 
					latitude = atof((PointString.substr(spacePos,PointString.length()-1)).c_str());
					NewTestPoint.sOriginalLocation.Set(latitude,longitude,DEG);
					NewTestPoint.sEstimatedLocation.Set(latitude,longitude,DEG);
					NewTestPoint.sMethodUsed = GPS;
					NewTestPoint.sErrorEstimate = 0.0;
					NewPosSet.sTestPoints.push_back(NewTestPoint);
				}
				NewMeasurement.sOriginalTP = tp;
				NewMeasurement.sID = i;
				NewMeasurement.sSiteID = atoi(r[i]["siteid"].c_str());
				PointString = r[i]["siteLocation"].c_str();
				spacePos = PointString.find_first_of(' ');
				longitude = atof((PointString.substr(6,spacePos).c_str())); 
				latitude = atof((PointString.substr(spacePos,PointString.length()-1)).c_str());
//				if ((longitude<181)&&(longitude>-181)&&(latitude<91)&&(latitude>-91))
				if ((longitude<34)&&(longitude>16)&&(latitude<-22)&&(latitude>-35)) // In South Africa
				{
					NumInPosSet++; 
					NewMeasurement.sSiteLocation.Set(latitude,longitude,DEG);
					NewMeasurement.sCellID = atoi(r[i]["ci"].c_str());
					PointString = r[i]["centriod"].c_str();
					spacePos = PointString.find_first_of(' ');
					longitude = atof((PointString.substr(6,spacePos).c_str())); 
					latitude = atof((PointString.substr(spacePos,PointString.length()-1)).c_str());
					NewMeasurement.sCentroid.Set(latitude,longitude,DEG);
					NewMeasurement.sInstKeyFixed = atoi(r[i]["InstKeyFixed"].c_str());
					NewMeasurement.sAntPatternKey = atoi(r[i]["txantpatternkey"].c_str());
					NewMeasurement.sFrequency = atof(r[i]["frequency"].c_str());
					NewMeasurement.sMeasValue = atof(r[i]["measvalue"].c_str());
					NewMeasurement.sPathLoss = atof(r[i]["pathloss"].c_str());
					NewMeasurement.sResDist = atof(r[i]["DistRes"].c_str());
					NewMeasurement.sEIRP = atof(r[i]["EIRP"].c_str());
					NewMeasurement.sPredValue = 0;
					NewMeasurement.sAzimuth = atof(r[i]["txbearing"].c_str());
					NewMeasurement.sTA = atof(r[i]["TA"].c_str());
					NewMeasurement.sBeamWidth = atof(r[i]["azibeamwidth"].c_str());
					NewMeasurement.sTilt = atof(r[i]["txmechtilt"].c_str());
					NewMeasurement.sHeight = atof(r[i]["txantennaheight"].c_str());
					NewMeasurement.sDistance = 999999;
					NewMeasurement.sServingCell = false;
					if (strlen(r[i]["TA"].c_str())>0)
					{
						NewMeasurement.sServingCell = true;
						Distance = (atoi(r[i]["TA"].c_str())+0.5)*NewMeasurement.sResDist;
						if (Distance<NewMeasurement.sDistance)
							NewMeasurement.sDistance = Distance;
					}
	
					NewMeasurement.sPathLoss = - NewMeasurement.sMeasValue + NewMeasurement.sEIRP;
					NewMeasurement.sRFDistEstimate = - NewMeasurement.sMeasValue + NewMeasurement.sEIRP
							-20 *log10(NewMeasurement.sFrequency);
					NewPosSet.sMeasurements.push_back(NewMeasurement);
				}
			}// end for number of entries

			NewPosSet.sNumMeas = NewPosSet.sMeasurements.size();
			mPosSets.push_back(NewPosSet);
			mNumPoints = mPosSets.size();
			string q = "SELECT (MAX(id)+1) AS id FROM TestPoint";
			pqxx::result r;
			gDb.PerformRawSql(q);
			gDb.GetLastResult(r);
			mNewTP = atoi(r[0]["id"].c_str());
		} // end if query is NOT empty
		else 
		{
			string err ="Empty query: ";
			err += query;
			cout << err << endl;
			QRAP_ERROR(err.c_str());
			delete [] text;
			return false;
		}
		delete [] text;
/*
		for (unsigned i=0; i<mPosSets.size(); i++)
			if (mPosSets[i].sTestPoints.size()>0)
				cout << mPosSets[i].sTestPoints[0].sOriginalTP << "n" 
					<< mPosSets[i].sMeasurements.size() << "	";
		cout << endl; 
*/
		cout << "cPosEstimation::LoadMeasurement: leaving ... happy ... " << endl << endl;
		return true;
	}
	delete [] text;
	cout << "cPosEstimation::LoadMeasurement: leaving ... not enough points in selecting area." << endl << endl;
	return false;
}

//********************************************************************************
void cPosEstimation::EstimatePositions()
{
	unsigned i,j;
	double distance;
	double phiERR;
	double distERR;
	mNumPoints = mPosSets.size();
	cout << "cPosEstimation::EstimatePositions(): mNumPoints = " << mNumPoints << endl;
	if (mUseClutter)
		cout << "cPosEstimation::EstimatePositions(): Using Clutter " <<  endl;
	else cout << "cPosEstimation::EstimatePositions(): NOT Using Clutter " <<  endl;

	if (mNumSites>0)
	{
		mCurSiteI = 0;
		mCurANNa->create_from_file(mSites[0].sANNfileA);
		mCurANNd->create_from_file(mSites[0].sANNfileD);
	}

	for (i=0; i< mNumPoints; i++)
	{
		mCurPosI = i;
		if ((mPosSets[mCurPosI].sMeasurements.size()>0)
			&&(mPosSets[mCurPosI].sTestPoints.size()>0))
		{
			cout << "# " << mCurPosI << endl;
			mPosSets[mCurPosI].sTestPoints[0].sAzimuth = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
																													Bearing(mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation);
			mPosSets[mCurPosI].sTestPoints[0].sDistance = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
																													Distance(mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation);

			delete [] mFixedAnts;
			mNumInsts = mPosSets[mCurPosI].sMeasurements.size();
			mFixedAnts = new cAntennaPattern[mNumInsts];
			for (j=0; j < mNumInsts; j++)
			{
				mFixedAnts[j].SetAntennaPattern(mPosSets[mCurPosI].sMeasurements[j].sAntPatternKey, 
					mPosSets[mCurPosI].sMeasurements[j].sAzimuth, 
					mPosSets[mCurPosI].sMeasurements[j].sTilt);
			}
			mPathLoss.setParameters(mkFactor,mPosSets[mCurPosI].sMeasurements[0].sFrequency,
					mPosSets[mCurPosI].sMeasurements[0].sHeight, MOBILEHEIGHT,
					mUseClutter, mClutterClassGroup);
	
			if (mPosSets[mCurPosI].sMeasurements[0].sDistance<120001)		
			{
				CoSinRule();
				if (!CI_TA())
					SecondSite();
			}
			else SecondSite();
			CI();
			DCM_ParticleSwarm();
			ANNrun();

			for (j=0; j<mPosSets[mCurPosI].sTestPoints.size(); j++)
			{
				cout << endl;
				distance = mPosSets[mCurPosI].sTestPoints[j].sOriginalLocation.
				Distance(mPosSets[mCurPosI].sTestPoints[j].sEstimatedLocation);
				cout << distance << "	";
				switch (mPosSets[mCurPosI].sTestPoints[j].sMethodUsed)
				{
					case GPS: 			cout << "GPS" << endl;		break;  
					case CellID: 			cout << "CellID" << endl;	break; 
					case CellID_TA: 		cout << "CellID_TA" << endl;	break; 
					case SSiteDir:			cout << "SSiteDir" << endl;	break; 
					case CoSiteSecDir:		cout << "CoSiteSecDir" << endl;	break; 
					case CosRuleAngleDistRatio:	cout << "CosRuleAngleDistRatio" << endl; break; 
					case CosRuleDistDist:		cout << "CosRuleDistDist" << endl; break; 
					case CosRuleDistAngle:		cout << "CosRuleDistAngle" << endl; break; 
					case CosRuleAngleAngle:		cout << "CosRuleAngleAngle" << endl; break; 
					case DCM_PSO:			cout << "DCM_PSO" << endl;	break; 
					case DCM_PSObestN:			cout << "DCM_PSObestN" << endl;	break; 
					case ANN:			cout << "ANN" << endl;	break; 
					default: cout << "None" << endl;
				}
				
				mPosSets[mCurPosI].sTestPoints[j].sOriginalLocation.Display();
				mPosSets[mCurPosI].sTestPoints[j].sEstimatedLocation.Display();
				cout << "rho = " << mPosSets[mCurPosI].sTestPoints[j].sDistance 
						<< "	phi = " << mPosSets[mCurPosI].sTestPoints[j].sAzimuth << endl;

	
				distERR = mPosSets[mCurPosI].sTestPoints[j].sDistance 
									- mPosSets[mCurPosI].sTestPoints[0].sDistance;
				phiERR = mPosSets[mCurPosI].sTestPoints[j].sAzimuth 
									- mPosSets[mCurPosI].sTestPoints[0].sAzimuth;
				if (phiERR > 180)  phiERR-=360;
				if (phiERR < -180) phiERR+=360;
				cout << "rhoERR = " << distERR
						<< "		phiERR = " << phiERR  << endl;
				

			} 
			cout << endl;
		}
	}
}

//*******************************************************************
bool cPosEstimation::CI()
{
	cout << "In cPosEstimation::CI()" << endl;
	unsigned i;
	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;

	newTestPoint.sMethodUsed = CellID;

	double Distance=0.0;
	for(i=1;i<mPosSets[mCurPosI].sMeasurements.size(); i++)
	{
		Distance+= mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
			Distance(mPosSets[mCurPosI].sMeasurements[i].sSiteLocation);
	}
	newTestPoint.sErrorEstimate = Distance /mPosSets[mCurPosI].sMeasurements.size()/4;

	newTestPoint.sEstimatedLocation =  mPosSets[mCurPosI].sMeasurements[0].sCentroid;
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sAzimuth = 0.0;
	newTestPoint.sDistance = 0.0;

	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;
	cout << "In cPosEstimation::CI():  tata" << endl;
	return true;

}
//*******************************************************************
// Can only be used if a sectorised site is used
bool cPosEstimation::CI_TA()
{
	cout << "In cPosEstimation::CI_TA()" << endl;
	unsigned j;
	if (mPosSets[mCurPosI].sMeasurements[0].sBeamWidth > 270)
		return false;

	cout << "mAzimuth = " << mPosSets[mCurPosI].sMeasurements[0].sAzimuth << endl;
	double Distance = mPosSets[mCurPosI].sMeasurements[0].sDistance; 

//	int ClosestIndex =-1;
//	double ClosestValue = MAXDOUBLE;

/*	if (Distance>120000)
	{
		for (i=1; i < mPosSets[mCurPosI].sMeasurements.size(); i++)
		{
			if (mPosSets[mCurPosI].sMeasurements[0].sSiteID !=
				mPosSets[mCurPosI].sMeasurements[i].sSiteID)
			{
				pathloss = - mPosSets[mCurPosI].sMeasurements[i].sMeasValue
						+ mPosSets[mCurPosI].sMeasurements[i].sEIRP
						- 20*log10(mPosSets[mCurPosI].sMeasurements[i].sFrequency);
				if (pathloss < ClosestValue)
				{
					ClosestValue = pathloss;
					ClosestIndex = i;
				}
			}
		}
		if (ClosestIndex > 0)
		{
			Distance = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
				Distance(mPosSets[mCurPosI].sMeasurements[ClosestIndex].sSiteLocation)/2;
		}
		else Distance = 17500;
	}
*/

//	cout << "cPosEstimation::CI_TA() : Kry maxDist" << endl;
/*	double minDist = mPlotResolution;
	double maxDist = 17500;
	if (Distance>120000)
	{	
		if (mPosSets[mCurPosI].sMeasurements.size()>1)
		{
			j=0;
			while ((j<mPosSets[mCurPosI].sMeasurements.size()-1)
						&&(	mPosSets[mCurPosI].sMeasurements[0].sSiteID==
								mPosSets[mCurPosI].sMeasurements[j].sSiteID))
				j++;
		
			if ((mPosSets[mCurPosI].sMeasurements.size()<j)
				||(mPosSets[mCurPosI].sMeasurements[0].sSiteID!=
								mPosSets[mCurPosI].sMeasurements[j].sSiteID))
			{		
				maxDist = 
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
								(mPosSets[mCurPosI].sMeasurements[j].sSiteLocation);
			}
		}
	}
	else
	{
		maxDist = (mPosSets[mCurPosI].sMeasurements[0].sDistance
				/mPosSets[mCurPosI].sMeasurements[0].sResDist+0.5)*
				mPosSets[mCurPosI].sMeasurements[0].sResDist+50.0;
//		minDist = max(mPlotResolution,(mPosSets[mCurPosI].sMeasurements[0].sDistance
//				/mPosSets[mCurPosI].sMeasurements[0].sResDist-0.5)
//				*mPosSets[mCurPosI].sMeasurements[0].sResDist-50.0);
	}

	if (maxDist>120000) maxDist=17500;
	if (maxDist>3.0*mPlotResolution) 
		Distance = SearchDistance(mPosSets[mCurPosI].sMeasurements[0].sAzimuth,minDist, maxDist); 
*/
//	cout << "cPosEstimation::CI_TA() : Na kry maxDist" << endl;

	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;

	newTestPoint.sMethodUsed = CellID_TA;

	//Error Estimate is the arc at the Distance 
	newTestPoint.sErrorEstimate = max(Distance * mPosSets[mCurPosI].sMeasurements[0].sBeamWidth * PI /180,
					mPosSets[mCurPosI].sMeasurements[0].sResDist);
	
	newTestPoint.sEstimatedLocation.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, 
						Distance, mPosSets[mCurPosI].sMeasurements[0].sAzimuth);
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sAzimuth = mPosSets[mCurPosI].sMeasurements[0].sAzimuth;
	newTestPoint.sDistance = Distance;

	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;
	cout << "In cPosEstimation::CI_TA()   tata" << endl;
	return true;	
}

//***************************************************************************************
// The direction is based on the 'closest' other site. This could be useful for omni-directional sites
bool cPosEstimation::SecondSite()
{
	cout << "In cPosEstimation::SecondSite()" << endl;	
	unsigned i;
	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;

	newTestPoint.sMethodUsed = SSiteDir;

	int ClosestIndex=-1;
	double ClosestValue = 155;
	double pathloss;

	for (i=1; i < mPosSets[mCurPosI].sMeasurements.size(); i++)
	{
		if (mPosSets[mCurPosI].sMeasurements[0].sSiteID !=
			mPosSets[mCurPosI].sMeasurements[i].sSiteID)
		{
			pathloss = - mPosSets[mCurPosI].sMeasurements[i].sMeasValue
					+ mPosSets[mCurPosI].sMeasurements[i].sEIRP
					- 20*log10(mPosSets[mCurPosI].sMeasurements[i].sFrequency);
			if (pathloss < ClosestValue)
			{
				ClosestValue = pathloss;
				ClosestIndex = i;
			}
		}
	}

	if (-1==ClosestIndex)
	return false;

	double Distance=mPosSets[mCurPosI].sMeasurements[0].sDistance;

	double Azimuth = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
			Bearing(mPosSets[mCurPosI].sMeasurements[ClosestIndex].sSiteLocation);
/*
	double SiteToSite = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.
			Distance(mPosSets[mCurPosI].sMeasurements[ClosestIndex].sSiteLocation);

	pathloss = - mPosSets[mCurPosI].sMeasurements[0].sMeasValue
					+ mPosSets[mCurPosI].sMeasurements[0].sEIRP
					- 20*log10(mPosSets[mCurPosI].sMeasurements[0].sFrequency);

	double k = pow(10, (ClosestValue - pathloss)/(10*GAMMA));
	cout << "k = " << k << endl;
	Distance = SiteToSite / (k+1);

//	Distance = SiteToSite / 2;

	if ((fabs((Distance-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
		mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
		&&(mPosSets[mCurPosI].sMeasurements[0].sDistance<120000))
	{
		if ((Distance - mPosSets[mCurPosI].sMeasurements[0].sDistance) < 0)
			Distance = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					- 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
		else 
			Distance = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					+ 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
	}

*/


	//Assuming a more or less regular cellplan
	newTestPoint.sErrorEstimate = max(Distance * PI/6,
					mPosSets[mCurPosI].sMeasurements[0].sResDist);

	newTestPoint.sEstimatedLocation.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, Distance, Azimuth);
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sAzimuth = Azimuth;
	newTestPoint.sDistance = Distance;

	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;
	cout << "In cPosEstimation::SecondSite()  tata" << endl;	
	return true;	
	
}

//**************************************************************************************
// This method base the direction on the antenna patterns of two sectors of the serving site
bool cPosEstimation::CoSecAzi(double &minAzi)
{
	cout << "In cPosEstimation::CoSecAzi" << endl;	
	vBand Bands;
	tBand Band;

	Band.sFrequency = mPosSets[mCurPosI].sMeasurements[0].sFrequency;
	Band.sAIndex = 0;
	Band.sBIndex = -1;
	Band.sMaxMeasValue = -120; 
	Bands.push_back(Band);

	unsigned i,j;

	//Find Co-sited cell in same band
	for (i=1; i < mPosSets[mCurPosI].sMeasurements.size(); i++)
	{
		for (j=0; j < Bands.size(); j++)
		{	// If Co-Sited
			if (mPosSets[mCurPosI].sMeasurements[Bands[j].sAIndex].sSiteID ==
				mPosSets[mCurPosI].sMeasurements[i].sSiteID) 
			{
				// Working in same band ... obstruction will affect different f-band differently
				if (fabs( mPosSets[mCurPosI].sMeasurements[Bands[j].sAIndex].sFrequency -
					mPosSets[mCurPosI].sMeasurements[i].sFrequency) < 500 )
				{
					if (mPosSets[mCurPosI].sMeasurements[i].sMeasValue > Bands[j].sMaxMeasValue)
						Bands[j].sBIndex=i; 

				}
				else 
				{	
					Band.sFrequency = mPosSets[mCurPosI].sMeasurements[i].sFrequency;
					Band.sAIndex = i;
					Band.sBIndex = -1;
					Band.sMaxMeasValue = -120;
					Bands.push_back(Band); 
				}
			}
		}
	}

	j=0;
	if (Bands.size()>0)
	{
		while ((-1==Bands[j].sBIndex)&&(j<Bands.size()-1))
			j++;

		if ((j>=Bands.size()-1)&&(-1==Bands[j].sBIndex))
		{
			Bands.clear();
//			SecondSite();
			cout << "In cPosEstimation::CoSecAzi: ... did not work  tata" << endl;
		 	return false;
		}
	}
	else 
	{
		cout << "In cPosEstimation::CoSecAzi: ... did not work  tata" << endl;
		return false;
	}

	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
	newTestPoint.sMethodUsed = CoSiteSecDir;

	unsigned AIndex = Bands[j].sAIndex;
	unsigned BIndex = Bands[j].sBIndex;
	Bands.clear();

	minAzi = FindAzi(BIndex, AIndex);
	cout << "In cPosEstimation::CoSecAz:  minAzi = " << minAzi << endl;

	double Distance= mPosSets[mCurPosI].sMeasurements[0].sDistance;

/*	double minDist = mPlotResolution;
	double maxDist = 17500;
	if (Distance>120000)
	{	
		if (mPosSets[mCurPosI].sMeasurements.size()>1)
		{
			j=0;
			while ((j<mPosSets[mCurPosI].sMeasurements.size()-1)
						&&(	mPosSets[mCurPosI].sMeasurements[0].sSiteID==
								mPosSets[mCurPosI].sMeasurements[j].sSiteID))
				j++;
		
			if ((mPosSets[mCurPosI].sMeasurements.size()<j)
				||(mPosSets[mCurPosI].sMeasurements[0].sSiteID!=
								mPosSets[mCurPosI].sMeasurements[j].sSiteID))
			{		
				maxDist = 
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
								(mPosSets[mCurPosI].sMeasurements[j].sSiteLocation);
			}
		}
	}
	else
	{
		maxDist = (mPosSets[mCurPosI].sMeasurements[0].sDistance
				/mPosSets[mCurPosI].sMeasurements[0].sResDist+0.5)*
				mPosSets[mCurPosI].sMeasurements[0].sResDist+50.0;
//		minDist = max(mPlotResolution,(mPosSets[mCurPosI].sMeasurements[0].sDistance
//				/mPosSets[mCurPosI].sMeasurements[0].sResDist-0.5)
//				*mPosSets[mCurPosI].sMeasurements[0].sResDist-50.0);
	}

	if (maxDist>120000) maxDist=17500;
	if (maxDist>4.0*mPlotResolution) 
		Distance = SearchDistance(minAzi,minDist, maxDist); 
*/
	cout << "In cPosEstimation::CoSecAz:  Distance = " << Distance << endl;
	cout << "In cPosEstimation::CoSecAz:  minAzi = " << minAzi << endl;

	newTestPoint.sEstimatedLocation.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, Distance, minAzi);
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sAzimuth = minAzi;
	newTestPoint.sDistance = Distance;

	double DegRes = min(5.0,max(90.0*mPosSets[mCurPosI].sMeasurements[AIndex].sResDist/Distance/PI,1.0));
	newTestPoint.sErrorEstimate = max(((double)mPosSets[mCurPosI].sMeasurements[AIndex].sResDist) /2.0,DegRes*PI*Distance/180.0);

	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;
	cout << "In cPosEstimation::CoSecAzi:  tata" << endl;
	return true;
}

//************************************************************************************************
//
double cPosEstimation::SearchDistance(double Azimuth, double min, double max)
{
	unsigned i;
	unsigned StopNum = ceil((max-min)/mPlotResolution);
	cout << "StopNum = " << StopNum << "	mPlotResolution = " << mPlotResolution 
				<< "	max = " << max << "	min = " << min << endl;
	double Distance = (max-min)/2.0;
	double BestCost = 999999;
	double Cost = 999999;

	for (i=0; i<StopNum; i++)
	{
		Cost = CostFunction(min + (double)i*mPlotResolution, Azimuth);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			Distance = min + i*mPlotResolution;
		}
	}	
	return Distance;
}


//*************************************************************************************************
//
double cPosEstimation::FindAzi(unsigned BIndex, unsigned AIndex)
{
	unsigned i;
	double LeftAngle, RightAngle;
	unsigned LeftIndex, RightIndex;
	
	double DeltaAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
			- mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth;
	cout << "DeltaAngle=" << DeltaAngle 
		<< "	AziA=" << mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
		<<	"	AziB="<< mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth;

	if (fabs(DeltaAngle)>180)
	{
		if (DeltaAngle<0)
			mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth -= 360;
		else
			mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth -= 360;
		DeltaAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
				- mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth;		
	}

	if (DeltaAngle < 0)
	{
		LeftAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
				- mPosSets[mCurPosI].sMeasurements[AIndex].sBeamWidth;
		LeftIndex = AIndex;
		RightAngle = mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth;
//		RightAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
//				+ mPosSets[mCurPosI].sMeasurements[AIndex].sBeamWidth;
		RightIndex = BIndex;
	}
	else
	{
//		LeftAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
//				- mPosSets[mCurPosI].sMeasurements[AIndex].sBeamWidth;
		LeftAngle = mPosSets[mCurPosI].sMeasurements[BIndex].sAzimuth;
		LeftIndex = BIndex;
//		RightAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
//				+ mPosSets[mCurPosI].sMeasurements[AIndex].sBeamWidth;
		RightAngle = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth
				+ mPosSets[mCurPosI].sMeasurements[AIndex].sBeamWidth;
		RightIndex = AIndex;
	} 


	cAntennaPattern LeftAnt, RightAnt;
	LeftAnt.SetAntennaPattern(mPosSets[mCurPosI].sMeasurements[LeftIndex].sAntPatternKey, 
				mPosSets[mCurPosI].sMeasurements[LeftIndex].sAzimuth, 
				mPosSets[mCurPosI].sMeasurements[LeftIndex].sTilt);
	RightAnt.SetAntennaPattern(mPosSets[mCurPosI].sMeasurements[RightIndex].sAntPatternKey, 
				mPosSets[mCurPosI].sMeasurements[RightIndex].sAzimuth, 
				mPosSets[mCurPosI].sMeasurements[RightIndex].sTilt);

	double RxLevDiff = mPosSets[mCurPosI].sMeasurements[LeftIndex].sMeasValue 
				- mPosSets[mCurPosI].sMeasurements[RightIndex].sMeasValue;
	double Distance = mPosSets[mCurPosI].sMeasurements[0].sDistance;
	double Elev = 180*atan2(mPosSets[mCurPosI].sMeasurements[0].sHeight,Distance)/PI;
	cout << "Elev = " << Elev << endl;

	double Delta;
	double minAzi = mPosSets[mCurPosI].sMeasurements[AIndex].sAzimuth;
	double Azimuth = minAzi;
	double LeftAntValue = LeftAnt.GetPatternValue(Azimuth, Elev);
	double RightAntValue = RightAnt.GetPatternValue(Azimuth, Elev);
	double minDelta = fabs (RxLevDiff - mPosSets[mCurPosI].sMeasurements[LeftIndex].sEIRP + LeftAntValue
				+ mPosSets[mCurPosI].sMeasurements[RightIndex].sEIRP - RightAntValue);

	//Exaustive search through angles
	double DegRes = min(1.5,max(90*mPosSets[mCurPosI].sMeasurements[AIndex].sResDist/Distance/PI,0.25));
	unsigned NumSteps = (int)((RightAngle - LeftAngle)/DegRes)+1; 

	cout <<  "		Left=" << LeftAngle << "	Right=" << RightAngle << endl;
	for (i=0; i<NumSteps ; i++)
	{
		Azimuth = LeftAngle + DegRes*i;
		LeftAntValue = LeftAnt.GetPatternValue(Azimuth, Elev);
		RightAntValue = RightAnt.GetPatternValue(Azimuth, Elev);
		Delta = fabs (RxLevDiff - mPosSets[mCurPosI].sMeasurements[LeftIndex].sEIRP + LeftAntValue
				+ mPosSets[mCurPosI].sMeasurements[RightIndex].sEIRP - RightAntValue);

	 	if ((Delta<minDelta)&&(LeftAntValue<18)&&(RightAntValue<18))
	 	{
	 		minDelta = Delta;
	 		minAzi = Azimuth;
//			cout << "Azimuth=" << Azimuth << "	MinDelta=" << minDelta << endl;
	 	}
	}
	return minAzi;
}
//**************************************************************************************
bool cPosEstimation::CoSinRule()
{
	cout << "In cPosEstimation::CoSinRule()" << endl;	
	double servSiteAzi=0;
	
	bool haveOwnAngle = CoSecAzi(servSiteAzi);
	int FirstOtherSite =-1;

	vBand Bands;

	tBand Band;

	unsigned i,j,p;
	int AIndex=-1, BIndex=-1;

	i=1;
	if (mPosSets[mCurPosI].sMeasurements.size()>1)
	{
		while ((mPosSets[mCurPosI].sMeasurements[i].sSiteID== mPosSets[mCurPosI].sMeasurements[0].sSiteID)
						&&(i+1<mPosSets[mCurPosI].sMeasurements.size()))
			i++;
		if (mPosSets[mCurPosI].sMeasurements[i].sSiteID!= mPosSets[mCurPosI].sMeasurements[0].sSiteID)
		{
			Band.sFrequency = mPosSets[mCurPosI].sMeasurements[i].sFrequency;
			Band.sAIndex = i;
			Band.sBIndex = -1;
			Band.sMaxMeasValue = -120; 
			Bands.push_back(Band);
			FirstOtherSite = (int)i;
		}
	}
	bool stop = false;

	double A, B, C, alpha, beta, gamma, cosB, cosC, cosA, sinA, sinB;
	cGeoP oldEst(-25.7, 28.2,DEG);
	bool worked = false;
	int OtherSiteIndex;

	if ((haveOwnAngle)&&(FirstOtherSite>0))
	{
		OtherSiteIndex = FirstOtherSite;
		cout << "In cPosEstimation::CoSinRule() OwnAngle, OtherSiteIndex=" << OtherSiteIndex << endl;	
		tTestPoint newTestPoint;
		newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
		newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
		newTestPoint.sMethodUsed = CosRuleAngleDistRatio;

		double Site2SiteDist = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
				(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation);
		C = Site2SiteDist;
		double OwnPathL = - mPosSets[mCurPosI].sMeasurements[0].sMeasValue
			+ mPosSets[mCurPosI].sMeasurements[0].sEIRP
			- 20*log10(mPosSets[mCurPosI].sMeasurements[0].sFrequency);
		double OtherPathL = - mPosSets[mCurPosI].sMeasurements[FirstOtherSite].sMeasValue
			+ mPosSets[mCurPosI].sMeasurements[FirstOtherSite].sEIRP
			- 20*log10(mPosSets[mCurPosI].sMeasurements[FirstOtherSite].sFrequency);

		double k = pow(10, (OtherPathL - OwnPathL)/(10*GAMMA));

		cout << "k = " << k << endl;

		beta = (servSiteAzi-mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Bearing
				(mPosSets[mCurPosI].sMeasurements[FirstOtherSite].sSiteLocation));
		if (beta >180) beta -=360;
		if (beta <-180) beta += 360;
		if (beta <0) beta *= -1;
		
		cosB = cos(beta*PI/180);
		sinB = sin(beta*PI/180);
		
		if (k==1)
			A = C / (2*cosB); 
		else if (k<fabs(sinB)) // non-real solution not possible change k for solution to be real
		{
			k = fabs(sinB);
			A = C *cosB/(1-k*k);
		}
		else
		{
			double A1 = C*(cosB + sqrt(k*k-sinB*sinB))/(1-k*k);
			double A2 = C*(cosB - sqrt(k*k-sinB*sinB))/(1-k*k);
			cout << "beta=" << beta << "	A1=" << A1 << "	A2=" << A2 << endl;
			if (A1<0) A1=A2; // negative answer is non-sensicle
			else if (A2<0) A2=A1; // negative answer is non-sensicle
			else if (fabs((A1-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
				mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
				A1 = A2; // choose solution that fit range
			else if (fabs((A2-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
				mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
				A2 = A1;
			A=min(A1,A2);
			if (A<=0) A = mPosSets[mCurPosI].sMeasurements[0].sDistance;
		}
		if ((fabs((A-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
			mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
			&&(mPosSets[mCurPosI].sMeasurements[0].sDistance<120000))
		{
			if ((A - mPosSets[mCurPosI].sMeasurements[0].sDistance) < 0)
				A = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					- 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
			else 
				A = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					+ 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
		}

		newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, A, servSiteAzi);
		newTestPoint.sErrorEstimate = oldEst.Distance(newTestPoint.sEstimatedLocation);
		newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
		newTestPoint.sAzimuth = servSiteAzi;
		newTestPoint.sDistance = A;

		newTestPoint.sNewTP = mNewTP;	
		mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
		mNewTP++;
	}

	cout << "In cPosEstimation::CoSinRule() Before find other site Azi" << endl;	
	if (mPosSets[mCurPosI].sMeasurements.size()>FirstOtherSite+1)
	{
		for (p=FirstOtherSite+1; (p<mPosSets[mCurPosI].sMeasurements.size())&&(!stop); p++)
		{
			if (mPosSets[mCurPosI].sMeasurements[p].sSiteID 
				!= mPosSets[mCurPosI].sMeasurements[0].sSiteID)
			{
				for (i=p+1; i < mPosSets[mCurPosI].sMeasurements.size(); i++)
				{
					for (j=0; j < Bands.size(); j++)
					{	// If Co-Sited
						if (mPosSets[mCurPosI].sMeasurements[Bands[j].sAIndex].sSiteID ==
							mPosSets[mCurPosI].sMeasurements[i].sSiteID)
						{
							// Working in same band ... obstruction will affect different f-band differently
							if (fabs( mPosSets[mCurPosI].sMeasurements[Bands[j].sAIndex].sFrequency -
							mPosSets[mCurPosI].sMeasurements[i].sFrequency) < 200 )
							{ 		//updating old band
								if (mPosSets[mCurPosI].sMeasurements[i].sMeasValue 
									> Bands[j].sMaxMeasValue)
										Bands[j].sBIndex=i; 
							}
							else //initialise new band
							{	
								Band.sFrequency = mPosSets[mCurPosI].sMeasurements[i].sFrequency;
								Band.sAIndex = i;
								Band.sBIndex = -1;
								Band.sMaxMeasValue = -120;
								Bands.push_back(Band); 
							}
						}
					}
				}
			}
		}
	
		if (Bands.size()>0)
		{
			j=0;
			while ((-1==Bands[j].sBIndex)&&(j<Bands.size()-1))
				j++;

			if ((Bands.size()-1==j)&&(-1==Bands[j].sBIndex))
				Bands.clear();
			else if (Bands.size()-1<j)
				Bands.clear();
			else
			{ 
				AIndex = Bands[j].sAIndex;
				BIndex = Bands[j].sBIndex;
				Bands.clear();
				stop = true;
			}
		}
	}

	int OtherAziIndex=-1;
	if (stop) OtherAziIndex = AIndex;

	int OtherDistanceIndex=-1;
	double OtherSiteDistance = 999999;

	for (p=1; (p<mPosSets[mCurPosI].sMeasurements.size())&&(-1==OtherDistanceIndex); p++)
	{
		if (mPosSets[mCurPosI].sMeasurements[p].sDistance < 120000)
		{
			OtherDistanceIndex = p;
			OtherSiteDistance = mPosSets[mCurPosI].sMeasurements[p].sDistance;
		}
	}

	if ((-1==OtherAziIndex)&&(-1==OtherDistanceIndex))
	{
			cout << "In cPosEstimation::CoSinRule() tata ... did not work" << endl;	
			return worked;
	}


// If one has the Rx-Tx time difference from active sets from different sites
	if (-1!=OtherDistanceIndex)
	{
		cout << "In cPosEstimation::CoSinRule() OtherDistance" << endl;	
		tTestPoint newTestPoint;
		newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
		newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
		newTestPoint.sMethodUsed = CosRuleDistDist;
		worked = true;
		OtherSiteIndex = OtherDistanceIndex;
		double Site2SiteDist = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
				(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation);
		C = Site2SiteDist;

		A = mPosSets[mCurPosI].sMeasurements[0].sDistance;
		B = OtherSiteDistance;
		
		beta = 180*acos((A*A+C*C-B*B)/(2*A*C))/PI;

		double SiteAzi = beta+mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Bearing
					(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation);
		
		newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, A, SiteAzi);
		newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
		newTestPoint.sAzimuth = SiteAzi;
		newTestPoint.sDistance = A;
		newTestPoint.sErrorEstimate = mPosSets[mCurPosI].sMeasurements[0].sResDist;
		oldEst = newTestPoint.sEstimatedLocation;
		newTestPoint.sNewTP = mNewTP;	
		mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
		mNewTP++;
/*
		if (haveOwnAngle)
		{
			beta = (servSiteAzi-mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Bearing
					(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation));
			if (beta >180) beta -=360;
			if (beta <-180) beta += 360;
			
			C = Site2SiteDist;
			B = OtherSiteDistance;

			cosB = cos(beta*PI/180);
			sinB = sin(beta*PI/180);
			A = C*cosB - sqrt(B*B-C*C*sinB*sinB);

			newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, A, servSiteAzi);
			newTestPoint.sErrorEstimate = oldEst.Distance(newTestPoint.sEstimatedLocation);
			newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
			newTestPoint.sAzimuth = servSiteAzi;
			newTestPoint.sDistance = A;
		}
*/
	}

// Have other site angle
	if ((-1!=OtherAziIndex)&&(-1!=BIndex))
	{
		cout << "In cPosEstimation::CoSinRule() OtherAngle" << endl;	
		tTestPoint newTestPoint;
		newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
		newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
		newTestPoint.sMethodUsed = CosRuleDistAngle;
		worked = true;
		double OtherSiteAzi = FindAzi(BIndex, AIndex);
		OtherSiteIndex = OtherAziIndex;
		double Site2SiteDist = mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
					(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation);

		double OtherSiteBearing = mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation.Bearing
					(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation);

		C = Site2SiteDist;
		alpha = (OtherSiteAzi-OtherSiteBearing);
		if (alpha>180) alpha-=360;
		if (alpha<-180) alpha+=360;
		if (alpha<0) alpha*=-1;

		cosA = cos(alpha*PI/180);
		sinA = sin(alpha*PI/180);

		double OwnPathL = - mPosSets[mCurPosI].sMeasurements[0].sMeasValue
			+ mPosSets[mCurPosI].sMeasurements[0].sEIRP
			- 20*log10(mPosSets[mCurPosI].sMeasurements[0].sFrequency);
		double OtherPathL = - mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sMeasValue
			+ mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sEIRP
			- 20*log10(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sFrequency);

		double k = pow(10, (OtherPathL - OwnPathL)/(10*GAMMA));

		cout << "k = " << k << endl;
		
		if (k==1)
			A = C / (2*k*cosA); 
		else if (1<fabs(k*sinA)) /// non-real solution not possible change k such that solution is real
		{
			k= 1.0/fabs(sinA);
			A = C*k*cosA/(k*k-1); 
		}
		else
		{
			double A1 = C*(k*cosA + sqrt(1-k*k*sinA*sinA))/(k*k-1);
			double A2 = C*(k*cosA - sqrt(1-k*k*sinA*sinA))/(k*k-1);
			cout << "alpha=" << alpha << "	A1=" << A1 << "	A2=" << A2 << endl;
			if (A1<0) A1=A2; // negative distance is non-sensicle
			else if (A2<0) A2=A1; // negative distance is non-sensicle
			else if (fabs((A1-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
				mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
				A1 = A2;
			else if (fabs((A2-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
				mPosSets[mCurPosI].sMeasurements[0].sResDist)>3.0)
				A2 = A1;
			A=min(A1,A2); 
			if (A<=0) A = mPosSets[mCurPosI].sMeasurements[0].sDistance;
		}
		if ((fabs((A-mPosSets[mCurPosI].sMeasurements[0].sDistance)/	
			mPosSets[mCurPosI].sMeasurements[0].sResDist)>1.0)
			&&(mPosSets[mCurPosI].sMeasurements[0].sDistance<120000))
		{
			if ((A - mPosSets[mCurPosI].sMeasurements[0].sDistance) <0)
				A = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					- 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
			else 
				A = mPosSets[mCurPosI].sMeasurements[0].sDistance 
					+ 0.5* mPosSets[mCurPosI].sMeasurements[0].sResDist;
		}

//*********************************************************************************************
// The following statement voids the preceeding
		A = mPosSets[mCurPosI].sMeasurements[0].sDistance;
//**********************************************************************************************
		
		if (1.0<fabs(C*sinA/A))
			worked = false;
		else
		{
			double B1 = C*cosA - sqrt(A*A - C*C*sinA*sinA);
			double B2 = C*cosA + sqrt(A*A - C*C*sinA*sinA);

			cout <<  "	B1=" << B1 << "	B2=" << B2 << endl;

			if (B1<0) B1=B2;
				else if (B2<0) B2=B1;
			B = min(B1,B2);
			if (B<=0) worked=false;
			else
			{
				newTestPoint.sEstimatedLocation.FromHere(
					mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation, B, OtherSiteAzi);
				oldEst = newTestPoint.sEstimatedLocation;
				newTestPoint.sErrorEstimate = mPosSets[mCurPosI].sMeasurements[0].sResDist;
				newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
				double beta = 180*acos((A*A+C*C-B*B)/(2*A*C))/PI;
				if (OtherSiteBearing>180)
					newTestPoint.sAzimuth = beta + OtherSiteBearing-180;
				else newTestPoint.sAzimuth = beta + OtherSiteBearing+180;
				newTestPoint.sDistance = A;
			
				newTestPoint.sNewTP = mNewTP;	
				mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
				mNewTP++;
			}
		}
		if (haveOwnAngle)
		{
			cout << "In cPosEstimation::CoSinRule() Both Angles" << endl;	
			worked =true;
			tTestPoint newTestPoint;
			newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
			newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
			newTestPoint.sMethodUsed = CosRuleAngleAngle;
			beta = (servSiteAzi-mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Bearing
					(mPosSets[mCurPosI].sMeasurements[OtherSiteIndex].sSiteLocation));
			if (beta >180) beta -=360;
			if (beta <-180) beta += 360;

			gamma = 180 - alpha -beta;
			cosB = cos(beta*PI/180);
			cosC = cos(gamma*PI/180);

			A = (C*cosC*cosA+C*cosB)/(1-cosC*cosC);
			B = (C*cosC*cosB+C*cosA)/(1-cosC*cosC);
			newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, A, servSiteAzi);
			newTestPoint.sErrorEstimate = oldEst.Distance(newTestPoint.sEstimatedLocation);
			newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);
			newTestPoint.sAzimuth = servSiteAzi;
			newTestPoint.sDistance = A;
			newTestPoint.sNewTP = mNewTP;	
			mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
			mNewTP++;
		}
	}
	cout << "In cPosEstimation::CoSinRule()   tata" << endl;	
	return worked;	
}


//******************************************************************************************************************************
bool cPosEstimation::DCM_ParticleSwarm()
{
	cout << "In cPosEstimation::DCM_ParticleSwarm()" << endl;	
	unsigned i,j;

	unsigned finalstop = 0;

	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
	newTestPoint.sMethodUsed = DCM_PSO;

	// Initialise swarm
	// make position coordinates rho (in meters) and phi (cylindrical coordinates)
	unsigned NumBest = 3;
	double* NbestValue;
	NbestValue = new double[NumBest];
	double* NbestRho;
	NbestRho = new double[NumBest];
	double* NbestPhi;
	NbestPhi = new double[NumBest];
	
	double* rho;
	rho = new double[NUMPARTICLES];
	double* phi;
	phi = new double[NUMPARTICLES];
	double* value;
	value = new double[NUMPARTICLES];
	double* pbestValue;
	pbestValue = new double[NUMPARTICLES];
	double* pbestRho;
	pbestRho = new double[NUMPARTICLES];
	double* pbestPhi;
	pbestPhi = new double[NUMPARTICLES];
	double* rho_snelheid;
	rho_snelheid = new double[NUMPARTICLES];
	double* phi_snelheid;
	phi_snelheid = new double[NUMPARTICLES];
	double gbestValue, gbestRho, gbestPhi;
	double sbestValue, sbestRho, sbestPhi;
	double tempvalue = 0.5;

	double rho_max = 120001.0;
	double rho_min = mPlotResolution;
	if (mPosSets[mCurPosI].sMeasurements[0].sDistance > 120000)
	{
		if (mPosSets[mCurPosI].sMeasurements.size()>1)
		{
			rho_max = 
			mPosSets[mCurPosI].sMeasurements[0].sSiteLocation.Distance
			(mPosSets[mCurPosI].sMeasurements[1].sSiteLocation);
		}
		else rho_max = 17500;
		rho_min = mPlotResolution;
	}
	else
	{
		rho_max = (mPosSets[mCurPosI].sMeasurements[0].sDistance
				/mPosSets[mCurPosI].sMeasurements[0].sResDist+0.5)*
				mPosSets[mCurPosI].sMeasurements[0].sResDist+50.0;
		rho_min = max(mPlotResolution,(mPosSets[mCurPosI].sMeasurements[0].sDistance
				/mPosSets[mCurPosI].sMeasurements[0].sResDist-1.5)
				*mPosSets[mCurPosI].sMeasurements[0].sResDist-50.0);
	}	
	if (rho_max<6*mPlotResolution) rho_max = 6*mPlotResolution;
	cout << "rho_min=" << rho_min << "	rho_max=" << rho_max << endl; 

    	std::random_device RhoRD;
    	std::mt19937_64 Rho_engine(RhoRD());
  	   std::exponential_distribution<double> rho_distE(1);
    	std::random_device RhoRDU;
    	std::mt19937_64 Rho_engineU(RhoRDU());
	   uniform_real_distribution<double> rho_distU(0,1);

	double phi_min = -180;
	double phi_max = 180;
	cout << "mPosSets[mCurPosI].sMeasurements[0].sBeamWidth = " << mPosSets[mCurPosI].sMeasurements[0].sBeamWidth << endl;
	phi_min = mPosSets[mCurPosI].sMeasurements[0].sAzimuth
		- (ceil)(mPosSets[mCurPosI].sMeasurements[0].sBeamWidth);
	phi_max = mPosSets[mCurPosI].sMeasurements[0].sAzimuth
		+ (ceil)(mPosSets[mCurPosI].sMeasurements[0].sBeamWidth);	

    	std::random_device PhiRD;
    	std::mt19937_64 Phi_engine(PhiRD());
	uniform_real_distribution<double> phi_dist(phi_min,phi_max);

	gbestValue = MAXDOUBLE;
	for (i=0; i<NumBest; i++)
		NbestValue[i]=MAXDOUBLE;


//	cout << " cPosEstimation::DCM_ParticleSwarm() VOOR initialisation loop" << endl; 
	for (i=0; i<NUMPARTICLES; i++)
	{
		tempvalue = 2;
		if (i<NUMPARTICLES/2)
			tempvalue = rho_distU(Rho_engineU);
		else
		{
			while (tempvalue>1)
				tempvalue = rho_distE(Rho_engine);
		}
		rho[i] = rho_min + (1.0 - tempvalue) * (rho_max - rho_min);
		phi[i] = phi_dist(Phi_engine);
		if (rho[i]<0)
		{
			rho[i]*=-1;
			if (phi[i]>=180.0)
				phi[i]-=180.0;
			else phi[i]+=180.0;
		}
		if (rho[i]<mPlotResolution) rho[i] = mPlotResolution;
		if (phi[i]>180)
			phi[i]-=360;
		else if (phi[i]<-180)
			phi[i]+=360;

		value[i] = CostFunction(rho[i], phi[i]);
		
//		cout << "i=" << i << "	tempvalue=" << tempvalue <<
//			"	rho[i]=" << rho[i] << "	phi[i]=" << phi[i] << "	Cost=" << value[i]<< endl;

		phi_snelheid[i] = 0.0;
		rho_snelheid[i] = -0.001;
		pbestRho[i] = rho[i];
		pbestPhi[i] = phi[i];
		pbestValue[i] = value[i];
//		cout << 0 <<"	i=" <<i << "	rho=" << rho[i] << "	phi=" << phi[i] << "	value=" << value[i] << endl;
		if (pbestValue[i] < NbestValue[NumBest-1])
		{
			NbestValue[NumBest-1] = pbestValue[i];
			NbestRho[NumBest-1] = pbestRho[i];
			NbestPhi[NumBest-1] = pbestPhi[i];
			for (j=NumBest-1; j>0; j--)
			{
				if (NbestValue[j]<NbestValue[j-1])
				{
					sbestValue = NbestValue[j];					
					sbestRho = NbestRho[j];
					sbestPhi = NbestPhi[j];
					NbestValue[j] = NbestValue[j-1];					
					NbestRho[j] = NbestRho[j-1];
					NbestPhi[j]= NbestPhi[j-1];
					NbestValue[j-1] = sbestValue;					
					NbestRho[j-1] = sbestRho;
					NbestPhi[j-1]= sbestPhi;
				}
			}
		}
		if (pbestValue[i]<gbestValue)
		{
			gbestValue = pbestValue[i];					
			gbestRho = pbestRho[i];
			gbestPhi = pbestPhi[i];
		}
	} 

/*	newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, gbestRho, gbestPhi);
	newTestPoint.sEstimatedLocation.Display();
	cout << gbestRho << endl;
*/	

	//Searching .... using Global Best

//	double oldBestValue = gbestValue;
	unsigned iterationN = 0;
	unsigned LastChangedN = 0;
	double oldBestRho = gbestRho;
	double oldBestPhi = gbestPhi;

    	std::random_device RpRhoRD;
    	std::mt19937_64 RpRho_engine(RpRhoRD());
		std::uniform_real_distribution<double> RpRho_dist(0.0,1.0);

    	std::random_device RgRhoRD;
    	std::mt19937_64 RgRho_engine(RgRhoRD());
	std::uniform_real_distribution<double> RgRho_dist(0.0,1.0);

    	std::random_device RpPhiRD;
    	std::mt19937_64 RpPhi_engine(RpPhiRD());
	std::uniform_real_distribution<double> RpPhi_dist(0.0,1.0);

    	std::random_device RgPhiRD;
    	std::mt19937_64 RgPhi_engine(RgPhiRD());
	std::uniform_real_distribution<double> RgPhi_dist(0.0,1.0);

	bool stop = false;
	bool change = false;
	LastChangedN = 0;

	iterationN = 0;
	while (!stop)
	{
		finalstop++;
		iterationN++;
//		cout << "iterationN=" << iterationN << endl;
		for (i=0; i< NUMPARTICLES; i++)
		{
			change = false;
			if ((value[i]<pbestValue[i])
					&&(rho[i]>rho_min)&&(rho[i]<rho_max))
			{
				if ((phi[i]>phi_min)
					&&(phi[i]<phi_max))
				{
					change = true;
//					for (j=1; j<mPosSets[mCurPosI].sMeasurements.size(); j++)
//					{
//						change = change && ((mCellPathLoss[0]+MARGIN) > mCellPathLoss[j]);
//					}
//					if (change) cout << "changing" << endl;
//					else cout << "not changing" << endl;
				}

			}
			if (change)
			{
				pbestValue[i] = value[i];
				pbestRho[i] = rho[i];
				pbestPhi[i] = phi[i]; 
				if (pbestValue[i] < NbestValue[NumBest-1])
				{
					NbestValue[NumBest-1] = pbestValue[i];
					NbestRho[NumBest-1] = pbestRho[i];
					NbestPhi[NumBest-1] = pbestPhi[i];
					for (j=NumBest-1; j>0; j--)
					{
						if (NbestValue[j]<NbestValue[j-1])
						{
							sbestValue = NbestValue[j];					
							sbestRho = NbestRho[j];
							sbestPhi = NbestPhi[j];
							NbestValue[j] = NbestValue[j-1];					
							NbestRho[j] = NbestRho[j-1];
							NbestPhi[j]= NbestPhi[j-1];
							NbestValue[j-1] = sbestValue;					
							NbestRho[j-1] = sbestRho;
							NbestPhi[j-1]= sbestPhi;
						}
					}
				}
				if (pbestValue[i]<gbestValue)
				{
					gbestValue = pbestValue[i];					
					gbestRho = pbestRho[i];
					gbestPhi = pbestPhi[i];
				}
				stop = ((gbestValue<0.00005) || ((iterationN-LastChangedN)>STOPN));
				LastChangedN = iterationN;
			}

			stop = (stop || ((iterationN-LastChangedN)>STOPN)||(finalstop>MAXITER));

			rho_snelheid[i] = INERTIA*rho_snelheid[i] 
					+ Cp*RpRho_dist(RpRho_engine)*(pbestValue[i] - value[i])
					+ Cg*RgRho_dist(RgRho_engine)*(gbestValue - value[i]);
			phi_snelheid[i] = INERTIA*phi_snelheid[i] 
					+ Cp*RpPhi_dist(RpPhi_engine)*(pbestValue[i] - value[i])
					+ Cg*RgPhi_dist(RgPhi_engine)*(gbestValue - value[i]);			
			rho[i] = rho[i] + rho_snelheid[i];
			phi[i] = phi[i] + phi_snelheid[i];

			phi[i] = fmod(phi[i],360);		
			if (phi[i]>180)
				phi[i]-=360;
			else if (phi[i]<-180)
				phi[i]+=360;

			if (rho[i]<0)
			{
				rho[i]*=-1;
				if (phi[i]>=180.0)
					phi[i]-=180.0;
				else phi[i]+=180.0;
			}
			if ((rho[i]>rho_max*4)||(rho[i]>120000)||(rho[i]<mPlotResolution))
			{
				tempvalue = 2;
				while (tempvalue>1)
					tempvalue = rho_distE(Rho_engine);
				rho[i] = rho_min + (1.0 - tempvalue) * (rho_max - rho_min);	
				rho_snelheid[i] = 0;
				phi[i] = phi_dist(Phi_engine);
				phi_snelheid[i] = 0;		
			}
			value[i] = CostFunction(rho[i], phi[i]);
//			cout << iterationN <<"		i=" <<i << "		rho=" << rho[i] << "		phi=" << phi[i] << "		value=" << value[i] << endl;

		}// end for
	}// end while

	cGeoP BeforeLast(-25.7, 28.2, DEG);
	newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, gbestRho, gbestPhi);
	BeforeLast.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, oldBestRho, oldBestPhi);
	newTestPoint.sErrorEstimate = newTestPoint.sEstimatedLocation.Distance(BeforeLast);	
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sAzimuth = gbestPhi;
	newTestPoint.sDistance = gbestRho;

	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;

	cout << "iterationN=" << iterationN<< "	BestValue= " << gbestValue << "		rho=" <<	gbestRho << "	phi="	<<	gbestPhi << endl;



	double x=0.0;
	double y=0.0;

	unsigned N=NumBest;
	for (i=0; i<NumBest; i++)
	{
		if (NbestValue[i] < 0.4)
		{
			x+=  NbestRho[i]*cos(NbestPhi[i]*PI/180.0);
			y+=  NbestRho[i]*sin(NbestPhi[i]*PI/180.0);
		}
		else N--;
	}
	
	if (N>0)
	{
		sbestRho = sqrt(x*x+y*y)/N;
		sbestPhi = 180*atan2(y,x)/PI;

		tTestPoint newTestPoint;
		newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
		newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
		newTestPoint.sMethodUsed = DCM_PSObestN;
		newTestPoint.sEstimatedLocation.FromHere(
				mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, sbestRho, sbestPhi);
		BeforeLast.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, gbestRho, gbestPhi);
		newTestPoint.sErrorEstimate = newTestPoint.sEstimatedLocation.Distance(BeforeLast);	
		newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
		newTestPoint.sAzimuth = sbestPhi;
		newTestPoint.sDistance = sbestRho;
		newTestPoint.sNewTP = mNewTP;	
		mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
		mNewTP++;
	}

	delete [] rho;
	delete [] phi;
	delete [] pbestRho;
	delete [] pbestPhi;
	delete [] value;
	delete [] pbestValue;
	delete [] rho_snelheid;
	delete [] phi_snelheid;
	delete [] NbestRho;
	delete [] NbestPhi;
	delete [] NbestValue;

	cout << "In cPosEstimation::DCM_ParticleSwarm() tata" << endl;	
	return (!stop);
}


//******************************************************************************************
double cPosEstimation::CostFunction(double rho, double phi)
{
	unsigned i;
//	unsigned NumUsed = mNumInsts;
	double* Delta;
	Delta = new double[mNumInsts];
	double* Prediction;
	Prediction = new double[mNumInsts];
	delete [] mCellPathLoss;
	mCellPathLoss = new double[mNumInsts];
	double sumMeas, sumPred, meanMeas, meanPred, teller, varMeas, varPred, CorrC, altCost;
	double DiffLoss=0, Azimuth, AntValue;
	float Tilt=0;
	cGeoP ParticlePosition(-25.7, 28.2, DEG);

	sumMeas = 0;
	sumPred = 0;
	ParticlePosition.FromHere(mPosSets[mCurPosI].sMeasurements[0].sSiteLocation, rho, phi);
//	ParticlePosition.Display();
	for (i=0; i<mNumInsts; i++)
	{
//			mPosSets[mCurPosI].sMeasurements[i].sSiteLocation.Display();
//			cout << "i = " << i << "	mCurPosI =" << mCurPosI << endl;
			mDEM.GetForLink(mPosSets[mCurPosI].sMeasurements[i].sSiteLocation,
						ParticlePosition, mPlotResolution, mDEMProfile);

			if (mUseClutter)
			{
				mClutter.GetForLink(mPosSets[mCurPosI].sMeasurements[i].sSiteLocation,
						ParticlePosition, mPlotResolution, mClutterProfile);
			}

//			cout << "Voor pathloss setParameter" << endl;
			mPathLoss.setParameters(mkFactor,mPosSets[mCurPosI].sMeasurements[i].sFrequency,
						mPosSets[mCurPosI].sMeasurements[i].sHeight, MOBILEHEIGHT,
						mUseClutter, mClutterClassGroup);
//			cout << "Voor pathloss TotPathloss" << endl;
			mCellPathLoss[i] = mPathLoss.TotPathLoss(mDEMProfile, Tilt, mClutterProfile, DiffLoss);
//			cout << "Voor Bearing" << endl;
			Azimuth = mPosSets[mCurPosI].sMeasurements[i].sSiteLocation.Bearing(ParticlePosition);

//			cout << "Get antvalue" << endl;
			AntValue = mFixedAnts[i].GetPatternValue(Azimuth, Tilt);
			Prediction[i] =  mPosSets[mCurPosI].sMeasurements[i].sEIRP 
							- mCellPathLoss[i] - AntValue;

			sumPred+=Prediction[i];
			sumMeas+=mPosSets[mCurPosI].sMeasurements[i].sMeasValue;
			Delta[i] = (mPosSets[mCurPosI].sMeasurements[i].sMeasValue - Prediction[i]) 
					* (mPosSets[mCurPosI].sMeasurements[i].sMeasValue - Prediction[i]);

//			cout << "	i="<< i 	 << "Tilt = " << Tilt << "	Azimuth = " << Azimuth 
//				<< "	AntValue=" << AntValue
//				<< "	meas=" << mPosSets[mCurPosI].sMeasurements[i].sMeasValue
//				<< "	pmCellPathLoss[i]=" << mCellPathLoss[i] << "	Delta[i]=" << Delta[i] << endl;
	}
	meanPred=sumPred/mNumInsts;
	meanMeas=sumMeas/mNumInsts;
	teller=0.0;
	varPred=0.0;
	varMeas=0.0;

	double Cost=0.0, Sexp=1.0, Pexp=1.0;
//	double Aexp=1.0;
	for (i=0; i<mNumInsts; i++)
	{
/*		if (i>0) Cost += Delta[i-1] + Delta[i] 
			     -2*((mPosSets[mCurPosI].sMeasurements[i-1].sMeasValue-Prediction[i-1])
				*(mPosSets[mCurPosI].sMeasurements[i].sMeasValue-Prediction[i]));
		else Cost += Delta[i];
*/
		Cost += Delta[i];
		teller += (mPosSets[mCurPosI].sMeasurements[i].sMeasValue - meanMeas)*
						(Prediction[i] - meanPred);
		varMeas +=  (mPosSets[mCurPosI].sMeasurements[i].sMeasValue - meanMeas)*
				 (mPosSets[mCurPosI].sMeasurements[i].sMeasValue - meanMeas);
		varPred += (Prediction[i] - meanPred)*(Prediction[i] - meanPred);

/*		if (i>0) Aexp = Aexp*exp(Delta[i-1] + Delta[i] 
			     -2*((mPosSets[mCurPosI].sMeasurements[i-1].sMeasValue-Prediction[i-1])
			*(mPosSets[mCurPosI].sMeasurements[i].sMeasValue-Prediction[i]))/SIGMA2);
		cout << SIGMA2 << "	"	<<exp(-Delta[i]/(SIGMA2)) << endl;
*/
		Sexp = Sexp*exp(-Delta[i]/(SIGMA2));
	}
	
	if ((varMeas>0)&&(varPred>0))
		CorrC = teller/sqrt(varMeas*varPred);
	else CorrC=1.0;
	Pexp = pow(Sexp, (double)(1.0/(double)mNumInsts));

//cout <<"	Cost=" << Cost << "	Sexp=" << Sexp << "	mNumInsts=" << mNumInsts << "		Pexp=" << Pexp << "	CorrC=" << CorrC << endl;
	double Pcost = 1.0 - Pexp;

//	altCost =  0.95*Pcost+(1.0-CorrC)*0.05;
//	altCost = 0.5*Pcost + (1.0-CorrC)*0.5;

	delete [] Delta;
	delete [] Prediction;
//	return Cost;
//	return (1.0-CorrC);
	return Pcost;
	return altCost;
}

//*******************************************************************
bool cPosEstimation::ANNrun()
{
	unsigned p,q;
	double *Input;
	double *OutputA;
	double *OutputD;
	tTestPoint newTestPoint;
	newTestPoint.sOriginalTP = mPosSets[mCurPosI].sTestPoints[0].sOriginalTP;
	newTestPoint.sOriginalLocation = mPosSets[mCurPosI].sTestPoints[0].sOriginalLocation;
	newTestPoint.sMethodUsed = ANN;

	cout << " In cPosEstimation::ANNrun()" << endl;
	cout << "mCurPosI = " << mCurPosI << "	mCurSiteI=" << mCurSiteI
			<< "	mPosSets[mCurPosI].sMeasurements[0].sSiteID=" <<mPosSets[mCurPosI].sMeasurements[0].sSiteID
			<< "	mSites[mCurSiteI].sSiteID=" <<mSites[mCurSiteI].sSiteID << endl;

	if (mPosSets[mCurPosI].sMeasurements[0].sSiteID!=mSites[mCurSiteI].sSiteID)
	{
		while ((mPosSets[mCurPosI].sMeasurements[0].sSiteID!=mSites[mCurSiteI].sSiteID)
			&&(mCurSiteI<mNumSites))
			mCurSiteI++;
		if (mPosSets[mCurPosI].sMeasurements[0].sSiteID!=mSites[mCurSiteI].sSiteID)
		{
			mCurSiteI=0;
			while ((mPosSets[mCurPosI].sMeasurements[0].sSiteID!=mSites[mCurSiteI].sSiteID)
				&&(mCurSiteI<mNumSites))
				mCurSiteI++;
		}
		if (mCurSiteI > mSites.size()-1)
		{
			mCurSiteI = 0;
			cout << " In cPosEstimation::ANNrun():  Did not find site.  ... tata" << endl;
			return false;
		}
		if (mPosSets[mCurPosI].sMeasurements[0].sSiteID!=mSites[mCurSiteI].sSiteID)
		{
			mCurSiteI = 0;
			cout << " In cPosEstimation::ANNrun():  Did not find site.  ... tata" << endl;
			return false;
		}

		mCurANNa->destroy();
		mCurANNa->create_from_file(mSites[mCurSiteI].sANNfileA);
		mCurANNd->destroy();
		mCurANNd->create_from_file(mSites[mCurSiteI].sANNfileD);
	}

	cout << " In cPosEstimation::ANNrun():  Found Site." << endl;
	cout << "mSites[mCurSiteI].sNumInputs = " << mSites[mCurSiteI].sNumInputs << endl;
	Input = new double[mSites[mCurSiteI].sNumInputs];

	Input[0] = 1;	
	Input[1] = cos(mPosSets[mCurPosI].sMeasurements[0].sAzimuth*PI/180);
	Input[2] = sin(mPosSets[mCurPosI].sMeasurements[0].sAzimuth*PI/180);
	Input[3] = (((double)mPosSets[mCurPosI].sMeasurements[0].sTA+0.5)*mPosSets[mCurPosI].sMeasurements[0].sResDist
						- mSites[mCurSiteI].sMaxDist/2)/mSites[mCurSiteI].sMaxDist;
	Input[4] = mPosSets[mCurPosI].sMeasurements[0].sResDist/1000;

	for (q=0; q<mSites[mCurSiteI].sCellSet.size(); q++)
	{
		Input[3*q+5] = -1; // scaled
		Input[3*q+6] = -1; // scaled
		Input[3*q+7] = (945+FREQ_OFFSET)*FREQ_SCALE; 	
	}

	for (p=0; p<mPosSets[mCurPosI].sNumMeas; p++)
	{
		q=0;
		if (mSites[mCurSiteI].sCellSet.size()>1)
		while ((mSites[mCurSiteI].sCellSet[q].sCI!=mPosSets[mCurPosI].sMeasurements[p].sCellID)
			&&(q+1< mSites[mCurSiteI].sCellSet.size()))
			q++;

		if (q<mSites[mCurSiteI].sCellSet.size())
		if (mSites[mCurSiteI].sCellSet[q].sCI==mPosSets[mCurPosI].sMeasurements[p].sCellID)
		{
//			cout << "3*q+7=" << 3*q+7 << "	mSites[mCurSiteI].sNumInputs="	<< mSites[mCurSiteI].sNumInputs << endl;
			Input[3*q+5] = (-mPosSets[mCurPosI].sMeasurements[p].sRFDistEstimate
							+RFDist_OFFSET)*MEAS_SCALE;
			if (fabs(Input[3*q+5])>1)  Input[3*q+5]/=fabs(Input[3*q+5]);
			Input[3*q+6] = (mPosSets[mCurPosI].sMeasurements[p].sMeasValue +MEAS_OFFSET)*MEAS_SCALE;
			if (fabs(Input[3*q+6])>1)  Input[3*q+6]/=fabs(Input[3*q+6]);
			Input[3*q+7] = (mPosSets[mCurPosI].sMeasurements[p].sFrequency
							+FREQ_OFFSET)*FREQ_SCALE;
			if (fabs(Input[3*q+7])>1)  Input[3*q+7]/=fabs(Input[3*q+7]);
/*			cout << mPosSets[mCurPosI].sMeasurements[p].sRFDistEstimate << "	"
				<< mPosSets[mCurPosI].sMeasurements[p].sMeasValue << "	"
				<< mPosSets[mCurPosI].sMeasurements[p].sFrequency << endl;
			cout << Input[3*q+5] << "	"<< Input[3*q+6] << "	"<< Input[3*q+7] << endl;
*/		} 
	}


/*
	for (q=0; q< mSites[mCurSiteI].sNumInputs; q++)
		cout << "q=" << q << "	Input=" << Input[q] << endl;
	for (q=0; q< mSites[mCurSiteI].sNumOutputs; q++)
		cout << "q=" << q << "	Output=" << Output[q] << endl;
*/
	OutputA = mCurANNa->run(Input);
	newTestPoint.sAzimuth = 180*atan2(OutputA[1],OutputA[0])/PI;

	OutputD = mCurANNd->run(Input);
	newTestPoint.sDistance = OutputD[0]*mSites[mCurSiteI].sMaxDist + mSites[mCurSiteI].sMaxDist/2;

	newTestPoint.sEstimatedLocation.FromHere(mSites[mCurSiteI].sPosition, 
						newTestPoint.sDistance, newTestPoint.sAzimuth);
	newTestPoint.sErrorActual = newTestPoint.sEstimatedLocation.Distance(newTestPoint.sOriginalLocation);	
	newTestPoint.sErrorEstimate = newTestPoint.sErrorActual;
	newTestPoint.sNewTP = mNewTP;	
	mPosSets[mCurPosI].sTestPoints.push_back(newTestPoint);
	mNewTP++;

	cout << " In cPosEstimation::ANNrun(): before delete" << endl;

	if (Input!=NULL) delete [] Input;
//	if (Output!=NULL) delete [] Output;
	cout << " In cPosEstimation::ANNrun(): tata" << endl;
	return true;

}

//*******************************************************************************************************************************
int cPosEstimation::SaveResults()
{
	unsigned i,j;
	string query, PQuery;
	char * temp;
	temp = new char[33];
	double Lat, Lon;
	QString PosString;

	string queryM = "INSERT into testpoint (id, originaltp, positionsource, location) Values (";
	string queryP = "INSERT into PositionEstimate (id, tp, azimuth, distance, error) Values (";

	for (i=0; i<mNumPoints; i++)
	{

		if (mPosSets[i].sTestPoints.size()>0)
		{
			PQuery = queryP;
			gcvt(mPosSets[i].sTestPoints[0].sNewTP,9,temp);
			PQuery +=temp;
			PQuery +=",";
			PQuery +=temp;

			PQuery += ", ";
			gcvt(mPosSets[i].sTestPoints[0].sAzimuth,9,temp);
			PQuery += temp;
			PQuery += ", ";
			gcvt(mPosSets[i].sTestPoints[0].sDistance,9,temp);
			PQuery += temp;
	
			PQuery += ", 0); ";

			if (!gDb.PerformRawSql(PQuery))
			{
				string err = "Error inserting TestPoint by running query: ";
				err += PQuery;
				cout << err <<endl; 
				QRAP_WARN(err.c_str());
				return false;
			}
		}		

		for (j=1; j < mPosSets[i].sTestPoints.size(); j++)
		{
			if (mPosSets[i].sTestPoints[j].sErrorActual > 0)
			{
				query = queryM;
				PQuery = queryP;
				gcvt(mPosSets[i].sTestPoints[j].sNewTP,9,temp);
				query += temp;
				PQuery +=temp;
				PQuery +=",";
				PQuery +=temp;
	
				query += ", ";
				gcvt(mPosSets[i].sTestPoints[j].sOriginalTP,9,temp);
				query += temp;
	
				query += ", ";
				gcvt(mPosSets[i].sTestPoints[j].sMethodUsed+1,9,temp);
				query += temp;
	
				mPosSets[i].sTestPoints[j].sEstimatedLocation.Get(Lat,Lon);
				PosString=QString(",ST_GeomFromText('POINT(%1 %2)',4326)").arg(Lon).arg(Lat);
				query += PosString.toStdString();
				query +=");";

				if (!gDb.PerformRawSql(query))
				{
					string err = "Error inserting TestPoint by running query: ";
					err += query;
					cout << err <<endl; 
					QRAP_WARN(err.c_str());
					return false;
				}
	
				PQuery += ", ";
				gcvt(mPosSets[i].sTestPoints[j].sAzimuth,9,temp);
				PQuery += temp;

				PQuery += ", ";
				gcvt(mPosSets[i].sTestPoints[j].sDistance,9,temp);
				PQuery += temp;
	
				PQuery += ", ";
				gcvt(mPosSets[i].sTestPoints[j].sErrorActual,9,temp);
				PQuery += temp;
				PQuery += "); ";
	
				if (!gDb.PerformRawSql(PQuery))
				{
					string err = "Error inserting TestPoint by running query: ";
					err += PQuery;
					cout << err <<endl; 
					QRAP_WARN(err.c_str());
					return false;
				}
			}

		}

	}

	delete [] temp;
	return true;
}


