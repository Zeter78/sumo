/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.dev/sumo
// Copyright (C) 2013-2024 German Aerospace Center (DLR) and others.
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// https://www.eclipse.org/legal/epl-2.0/
// This Source Code may also be made available under the following Secondary
// Licenses when the conditions for such availability set forth in the Eclipse
// Public License 2.0 are satisfied: GNU General Public License, version 2
// or later which is available at
// https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html
// SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
/****************************************************************************/
/// @file    MSDevice_Bluelight.cpp
/// @author  Daniel Krajzewicz
/// @author  Michael Behrisch
/// @author  Jakob Erdmann
/// @author  Laura Bieker
/// @date    01.06.2017
///
// A device for emergency vehicle. The behaviour of other traffic participants will be triggered with this device.
// For example building a rescue lane.
/****************************************************************************/
#include <config.h>

#include <utils/common/StringUtils.h>
#include <utils/common/StringTokenizer.h>
#include <utils/options/OptionsCont.h>
#include <utils/iodevices/OutputDevice.h>
#include <utils/vehicle/SUMOVehicle.h>
#include <microsim/MSNet.h>
#include <microsim/MSLane.h>
#include <microsim/MSEdge.h>
#include <microsim/MSLink.h>
#include <microsim/MSVehicle.h>
#include <microsim/lcmodels/MSAbstractLaneChangeModel.h>
#include <microsim/MSVehicleControl.h>
#include <microsim/MSVehicleType.h>
#include "MSDevice_Tripinfo.h"
#include "MSDevice_Bluelight.h"

//#define DEBUG_BLUELIGHT
//#define DEBUG_BLUELIGHT_RESCUELANE

#define INFLUENCED_BY "rescueLane"

// ===========================================================================
// method definitions
// ===========================================================================
// ---------------------------------------------------------------------------
// static initialisation methods
// ---------------------------------------------------------------------------
void
MSDevice_Bluelight::insertOptions(OptionsCont& oc) {
    oc.addOptionSubTopic("Bluelight Device");
    insertDefaultAssignmentOptions("bluelight", "Bluelight Device", oc);

    oc.doRegister("device.bluelight.reactiondist", new Option_Float(25.0));
    oc.addDescription("device.bluelight.reactiondist", "Bluelight Device", TL("Set the distance at which other drivers react to the blue light and siren sound"));
    oc.doRegister("device.bluelight.mingapfactor", new Option_Float(1.));
    oc.addDescription("device.bluelight.mingapfactor", "Bluelight Device", TL("Reduce the minGap for reacting vehicles by the given factor"));
    oc.doRegister("device.bluelight.activated", new Option_Bool(true));
    oc.addDescription("device.bluelight.activated", "Bluelight Device", TL("Boolean which decides if bluelight device is activated. Only when true the vehicle has special rights"));
    oc.doRegister("device.bluelight.invertDirection", new Option_Bool(false));
    oc.addDescription("device.bluelight.invertDirection", "Bluelight Device", TL("Boolean trigger, which forces driving direction to be inverted if set to true.  The boolean is automatically reset to false afterwards. Use case: forced change into opposite lane while overtaking via traci."));
}


void
MSDevice_Bluelight::buildVehicleDevices(SUMOVehicle& v, std::vector<MSVehicleDevice*>& into) {
    OptionsCont& oc = OptionsCont::getOptions();
    if (equippedByDefaultAssignmentOptions(oc, "bluelight", v, false)) {
        if (MSGlobals::gUseMesoSim) {
            WRITE_WARNINGF(TL("bluelight device is not compatible with mesosim (ignored for vehicle '%')"), v.getID());
        } else {
            MSDevice_Bluelight* device = new MSDevice_Bluelight(v, "bluelight_" + v.getID(),
                    getFloatParam(v, oc, "bluelight.reactiondist", oc.getFloat("device.bluelight.reactiondist")),
                    getFloatParam(v, oc, "bluelight.mingapfactor", oc.getFloat("device.bluelight.mingapfactor")),
                    getBoolParam(v, oc, "bluelight.activated", oc.getBool("device.bluelight.activated")),
                    getBoolParam(v, oc, "bluelight.invertDirection", oc.getBool("device.bluelight.invertDirection")));
            into.push_back(device);
        }
    }
}


// ---------------------------------------------------------------------------
// MSDevice_Bluelight-methods
// ---------------------------------------------------------------------------
MSDevice_Bluelight::MSDevice_Bluelight(SUMOVehicle& holder, const std::string& id,
                                       const double reactionDist, const double minGapFactor, const bool activated, const bool invertDirection) :
    MSVehicleDevice(holder, id),
    myReactionDist(reactionDist),
    myMinGapFactor(minGapFactor),
    activated(activated),
    invertDirection(invertDirection) {
        if (activated) {
            // if the initial value of activated is true we need to give the vehicle the special rights, that only need to be done ONCE and not on every move
            // if the initial value of activated is false we don't need to do anything, just keep the standard default values
        
            MSVehicle& ego = dynamic_cast<MSVehicle&>(holder);
            MSVehicle::Influencer& influencer = ego.getInfluencer();

            // violate red lights
            influencer.setSpeedMode(39);

            // change vClass to emergency
            // vClass is defined in vType, to change it for ONLY this single vehicle we need a singular type
            MSVehicleType& newType = ego.getSingularType();
            newType.setVClass(SUMOVehicleClass::SVC_EMERGENCY);

            // DO NOT reroute here, because that will crash the program (the route will anyways be computed correctly afterwards)

            // set speedFactor, so that vehicle can drive up to 1.5 faster than the normal speed limit
            newType.setSpeedFactor(1.50);
        }

    #ifdef DEBUG_BLUELIGHT
        std::cout << SIMTIME << " initialized device '" << id << "' with myReactionDist=" << myReactionDist << "\n";
    #endif
}


MSDevice_Bluelight::~MSDevice_Bluelight() {
}


bool
MSDevice_Bluelight::notifyMove(SUMOTrafficObject& veh, double /* oldPos */,
                               double /* newPos */, double newSpeed) {
    #ifdef DEBUG_BLUELIGHT
        std::cout << SIMTIME  << " device '" << getID() << "' notifyMove: newSpeed=" << newSpeed << "\n";
    #else
        UNUSED_PARAMETER(newSpeed);
    #endif

    if (activated) {
        MSVehicle& ego = dynamic_cast<MSVehicle&>(veh);
        const double vMax = ego.getLane()->getVehicleMaxSpeed(&ego);

        if (ego.getSpeed() < 0.5 * vMax) {
            // advance as far as possible (assume vehicles will keep moving out of the way)
            ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_STRATEGIC_PARAM), "-1");
            ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_SPEEDGAIN_LOOKAHEAD), "0");
            try {
                ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_MINGAP_LAT), "0");
            } catch (InvalidArgument&) {
                // not supported by the current laneChangeModel
            }
        } else {
            // restore defaults (also when bluelight device was deactivated we need to restore the defaults, BUT that is done in the activatedChanged
            // method)
            ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_STRATEGIC_PARAM),
                                                ego.getVehicleType().getParameter().getLCParamString(SUMO_ATTR_LCA_STRATEGIC_PARAM, "1"));
            ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_SPEEDGAIN_LOOKAHEAD),
                                                ego.getVehicleType().getParameter().getLCParamString(SUMO_ATTR_LCA_SPEEDGAIN_LOOKAHEAD, "5"));
            try {
                ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_MINGAP_LAT),
                                                toString(ego.getVehicleType().getMinGapLat()));
            } catch (InvalidArgument&) {
                // not supported by the current laneChangeModel
            }
        }
        // build a rescue lane for all vehicles on the route of the emergency vehicle within the range of the siren
        MSVehicleType* vt = MSNet::getInstance()->getVehicleControl().getVType(veh.getVehicleType().getID());
        vt->setPreferredLateralAlignment(LatAlignmentDefinition::ARBITRARY);

        MSVehicleControl& vc = MSNet::getInstance()->getVehicleControl();
        // use edges on the way of the emergency vehicle
        std::vector<const MSEdge*> upcomingEdges;
        std::set<MSVehicle*, ComparatorIdLess> upcomingVehicles;
        std::set<std::string> lastStepInfluencedVehicles = myInfluencedVehicles;
        std::vector<MSLink*> upcomingLinks;
        double affectedJunctionDist = ego.getPositionOnLane() + myReactionDist;
        for (const MSLane* const l : ego.getUpcomingLanesUntil(myReactionDist)) {
            upcomingEdges.push_back(&l->getEdge());

            affectedJunctionDist -= l->getLength();
            if (affectedJunctionDist > 0 && l->isInternal()) {
                upcomingLinks.push_back(l->getIncomingLanes()[0].viaLink);
            }
        }

        for (const MSEdge* const e : upcomingEdges) {
            //inform all vehicles on upcomingEdges
            for (const SUMOVehicle* v : e->getVehicles()) {
                upcomingVehicles.insert(dynamic_cast<MSVehicle*>(const_cast<SUMOVehicle*>(v)));
                if (lastStepInfluencedVehicles.count(v->getID()) > 0) {
                    lastStepInfluencedVehicles.erase(v->getID());
                }
            }
        }

        // reset all vehicles that were in myInfluencedVehicles in the previous step but not in the current step 
        // TODO: refactor
        for (std::string vehID : lastStepInfluencedVehicles) {
            myInfluencedVehicles.erase(vehID);
            Parameterised::Map::iterator it = myInfluencedTypes.find(vehID);
            MSVehicle* veh2 = dynamic_cast<MSVehicle*>(vc.getVehicle(vehID));
            if (veh2 != nullptr && it != myInfluencedTypes.end()) {
                // The vehicle gets back its old VehicleType after the emergency vehicle have passed them
                resetVehicle(veh2, it->second);
            }
        }

        for (MSVehicle* veh2 : upcomingVehicles) {
            assert(veh2 != nullptr);
            if (veh2->getLane() == nullptr) {
                continue;
            }

            if (std::find(upcomingEdges.begin(), upcomingEdges.end(), &veh2->getLane()->getEdge()) != upcomingEdges.end()) {
                if (veh2->getDevice(typeid(MSDevice_Bluelight)) != nullptr) {
                    // vehicles with bluelight device should not react
                    continue;
                }

                const int numLanes = (int)veh2->getLane()->getEdge().getNumLanes();
                // make sure that vehicles are still building the rescue lane as they might have moved to a new edge or changed lanes
                if (myInfluencedVehicles.count(veh2->getID()) > 0) {
                    // Vehicle gets a new Vehicletype to change the alignment and the lanechange options
                    MSVehicleType& t = veh2->getSingularType();
                    // Setting the lateral alignment to build a rescue lane
                    LatAlignmentDefinition align = LatAlignmentDefinition::RIGHT;

                    // if we only have one lane, vehicles should always move to the right
                    if(numLanes != 1){
                        // bicycles should ALWAYS change to the right sublane, while all other vehicles change according to their position and the position of the emergency vehicle
                        //if(t.getVehicleClass() != SVC_BICYCLE) {
                            // if veh2 is in the leftmost lane (index == numLanes - 1) OR the index of veh2 is greater than the index of the emergency vehicle 
                            // (= veh2 is to the left of the emergency vehicle), then veh2 must align to the left
                            // in all other cases it should align to the right
                            if (veh2->getLane()->getIndex() == numLanes - 1 || veh2->getLane()->getIndex() > ego.getLane()->getIndex()) {
                                align = LatAlignmentDefinition::LEFT;
                            }
                        //}
                    }

                    t.setPreferredLateralAlignment(align);

                    // bicycles which are in the rightmost sublane of their lane are slowed down to 5km/h (1,39m/s) shortly
                    // we do this to allow bicycles in sublanes more to the left to get into the rightmost sublane
/*                     if(t.getVehicleClass() == SVC_BICYCLE) {
                        if(isInRightmostSublaneOfLane(*veh2)){
                            std::vector<std::pair<SUMOTime, double> > speedTimeLine;
                            speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep(), (*veh2).getSpeed()));
                            speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep() + TIME2STEPS(2), 1.39));

                            MSVehicle::Influencer& speedTimeLineAdjuster = (*veh2).getInfluencer();
                            speedTimeLineAdjuster.setSpeedTimeLine(speedTimeLine);
                        }
                    } */

                    #ifdef DEBUG_BLUELIGHT_RESCUELANE
                        std::cout << "Refresh alignment for vehicle: " << veh2->getID()
                                << " laneIndex=" << veh2->getLane()->getIndex() << " numLanes=" << numLanes
                                << " alignment=" << toString(align) << "\n";
                    #endif
                }

                double distanceDelta = veh.getPosition().distanceTo(veh2->getPosition());
                //emergency vehicle has to slow down when entering the rescue lane
                if (distanceDelta <= 10 && veh.getID() != veh2->getID() && myInfluencedVehicles.count(veh2->getID()) > 0 && veh2->getSpeed() < 1) {
                    // set ev speed to 20 km/h 0 5.56 m/s
                    std::vector<std::pair<SUMOTime, double> > speedTimeLine;
                    speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep(), veh.getSpeed()));
                    speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep() + TIME2STEPS(2), 5.56));

                    
                    MSVehicle::Influencer& speedTimeLineAdjuster = ego.getInfluencer();
                    speedTimeLineAdjuster.setSpeedTimeLine(speedTimeLine);
                }

                // the perception of the sound of the siren should be around 25 meters
                // TODO: only vehicles in front of the emergency vehicle should react
                if (distanceDelta <= myReactionDist && veh.getID() != veh2->getID() && myInfluencedVehicles.count(veh2->getID()) == 0) {
                    // only a percentage of vehicles should react to the emergency vehicle to make the behaviour more realistic
                    double reaction = RandHelper::rand();

                    // the vehicles should react according to the distance to the emergency vehicle taken from real world data
                    double reactionProb = (
                                            distanceDelta < getFloatParam(myHolder, OptionsCont::getOptions(), "bluelight.near-dist", 12.5, false)
                                            ? getFloatParam(myHolder, OptionsCont::getOptions(), "bluelight.reaction-prob-near", 0.577, false)
                                            : getFloatParam(myHolder, OptionsCont::getOptions(), "bluelight.reaction-prob-far", 0.189, false));
                    
                    // TODO: works only for one second steps
                    //std::cout << SIMTIME << " veh2=" << veh2->getID() << " distanceDelta=" << distanceDelta << " reaction=" << reaction << " reactionProb=" << reactionProb << "\n";
                    if (veh2->isActionStep(SIMSTEP) && reaction < reactionProb * veh2->getActionStepLengthSecs()) {
                        myInfluencedVehicles.insert(veh2->getID());
                        myInfluencedTypes.insert(std::make_pair(veh2->getID(), veh2->getVehicleType().getID()));
                        if (myMinGapFactor != 1.) {
                            // TODO: this is a permanent change to the vtype!
                            MSNet::getInstance()->getVehicleControl().getVType(veh2->getVehicleType().getID())->getCarFollowModel().setCollisionMinGapFactor(myMinGapFactor);
                        }

                        // Vehicle gets a new Vehicletype to change the alignment and the lanechange options
                        MSVehicleType& t = veh2->getSingularType();
                        
                        // Setting the lateral alignment to build a rescue lane
                        LatAlignmentDefinition align = LatAlignmentDefinition::RIGHT;

                        // if we only have one lane, vehicles should always move to the right
                        if(numLanes != 1){
                            // bicycles should ALWAYS change to the right sublane, while all other vehicles change according to their position and the position of the emergency vehicle
                            //if(t.getVehicleClass() != SVC_BICYCLE) {
                                // if veh2 is in the leftmost lane (index == numLanes - 1) OR the index of veh2 is greater than the index of the emergency vehicle 
                                // (= veh2 is to the left of the emergency vehicle), then veh2 must align to the left
                                // in all other cases it should align to the right
                                if (veh2->getLane()->getIndex() == numLanes - 1 || veh2->getLane()->getIndex() > ego.getLane()->getIndex()) {
                                    align = LatAlignmentDefinition::LEFT;
                                }
                            //}
                        }

                        t.setPreferredLateralAlignment(align);

                        // bicycles which are in the rightmost sublane of their lane are slowed down to 5km/h (1,39m/s) shortly
                        // we do this to allow bicycles in sublanes more to the left to get into the rightmost sublane
/*                         if(t.getVehicleClass() == SVC_BICYCLE) {
                            if(isInRightmostSublaneOfLane(*veh2)){
                                std::vector<std::pair<SUMOTime, double> > speedTimeLine;
                                speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep(), (*veh2).getSpeed()));
                                speedTimeLine.push_back(std::make_pair(MSNet::getInstance()->getCurrentTimeStep() + TIME2STEPS(2), 1.39));

                                MSVehicle::Influencer& speedTimeLineAdjuster = (*veh2).getInfluencer();
                                speedTimeLineAdjuster.setSpeedTimeLine(speedTimeLine);
                            }
                        } */

                        t.setMinGap(t.getMinGap() * myMinGapFactor);

                        const_cast<SUMOVTypeParameter&>(t.getParameter()).jmParameter[SUMO_ATTR_JM_STOPLINE_GAP] = toString(myMinGapFactor);
                        
                        #ifdef DEBUG_BLUELIGHT_RESCUELANE
                            std::cout << SIMTIME << " device=" << getID() << " formingRescueLane=" << veh2->getID()
                                    << " laneIndex=" << veh2->getLane()->getIndex() << " numLanes=" << numLanes
                                    << " alignment=" << toString(align) << "\n";
                        #endif
                        
                        std::vector<std::string> influencedBy = StringTokenizer(veh2->getParameter().getParameter(INFLUENCED_BY, "")).getVector();
                        if (std::find(influencedBy.begin(), influencedBy.end(), myHolder.getID()) == influencedBy.end()) {
                            influencedBy.push_back(myHolder.getID());
                            const_cast<SUMOVehicleParameter&>(veh2->getParameter()).setParameter(INFLUENCED_BY, toString(influencedBy));
                        }

                        //other vehicle should not use the rescue lane so they should not make any lane changes
                        MSVehicle::Influencer& lanechange = veh2->getInfluencer();
                        
                        lanechange.setLaneChangeMode(1536);
                        

                        // disable strategic lane-changing
                        //veh2->getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_STRATEGIC_PARAM), "-1");
                    }
                }

            } else {
                //if vehicle is passed all vehicles which had to react should get their state back after they leave the communication range
                if (myInfluencedVehicles.count(veh2->getID()) > 0) {
                    double distanceDelta = veh.getPosition().distanceTo(veh2->getPosition());
                    if (distanceDelta > myReactionDist && veh.getID() != veh2->getID()) {
                        myInfluencedVehicles.erase(veh2->getID());
                        Parameterised::Map::iterator it = myInfluencedTypes.find(veh2->getID());
                        if (it != myInfluencedTypes.end()) {
                            // The vehicle gets back its old VehicleType after the emergency vehicle have passed them
                            resetVehicle(veh2, it->second);
                        }
                    }
                }
            }
        }

        // make upcoming junction foes slow down
        for (MSLink* link : upcomingLinks) {
            auto avi = link->getApproaching(&ego);
            MSLink::BlockingFoes blockingFoes;
            link->opened(avi.arrivalTime, avi.arrivalSpeed, avi.arrivalSpeed, ego.getLength(),
                        0, ego.getCarFollowModel().getMaxDecel(), ego.getWaitingTime(), ego.getLateralPositionOnLane(), &blockingFoes, true, &ego);
            
            const SUMOTime timeToArrival = avi.arrivalTime - SIMSTEP;
            for (const SUMOVehicle* foe : blockingFoes) {
                const double dist = ego.getPosition().distanceTo2D(foe->getPosition());
                if (dist < myReactionDist) {
                    MSVehicle* microFoe = dynamic_cast<MSVehicle*>(const_cast<SUMOVehicle*>(foe));
                    if (microFoe->getDevice(typeid(MSDevice_Bluelight)) != nullptr) {
                        // emergency vehicles should not react
                        continue;
                    }
                    const double timeToBrake = foe->getSpeed() / 4.5;
                    if (timeToArrival < TIME2STEPS(timeToBrake + 1)) {
                        std::vector<std::pair<SUMOTime, double> > speedTimeLine;
                        speedTimeLine.push_back(std::make_pair(SIMSTEP, foe->getSpeed()));
                        speedTimeLine.push_back(std::make_pair(avi.arrivalTime, 0));
                        microFoe->getInfluencer().setSpeedTimeLine(speedTimeLine);
                        //std::cout << SIMTIME << " foe=" << foe->getID() << " dist=" << dist << " timeToBrake= " << timeToBrake << " ttA=" << STEPS2TIME(timeToArrival) << "\n";
                    }
                }
            }
        }

        // ego is at the end of its current lane and cannot continue
        const double distToEnd = ego.getLane()->getLength() - ego.getPositionOnLane();
        //std::cout << SIMTIME << " " << getID() << " lane=" << ego.getLane()->getID() << " pos=" << ego.getPositionOnLane() << " distToEnd=" << distToEnd << " conts=" << toString(ego.getBestLanesContinuation()) << " furtherEdges=" << upcomingEdges.size() << "\n";
        if (ego.getBestLanesContinuation().size() == 1 && distToEnd <= POSITION_EPS
                // route continues
                && upcomingEdges.size() > 1) {
            const MSEdge* currentEdge = &ego.getLane()->getEdge();
            // move onto the intersection as if there was a connection from the current lane
            const MSEdge* next = currentEdge->getInternalFollowingEdge(upcomingEdges[1], ego.getVClass());
            if (next == nullptr) {
                next = upcomingEdges[1];
            }
            // pick the lane that causes the minimizes lateral jump
            const std::vector<MSLane*>* allowed = next->allowedLanes(ego.getVClass());
            MSLane* nextLane = next->getLanes().front();
            double bestJump = std::numeric_limits<double>::max();
            double newPosLat = 0;
            if (allowed != nullptr) {
                for (MSLane* nextCand : *allowed) {
                    for (auto ili : nextCand->getIncomingLanes()) {
                        if (&ili.lane->getEdge() == currentEdge) {
                            double jump = fabs(ego.getLatOffset(ili.lane) + ego.getLateralPositionOnLane());
                            if (jump < bestJump) {
                                //std::cout << SIMTIME << " nextCand=" << nextCand->getID() << " from=" << ili.lane->getID() << " jump=" << jump << "\n";
                                bestJump = jump;
                                nextLane = nextCand;
                                // stay within newLane
                                const double maxVehOffset = MAX2(0.0, nextLane->getWidth() - ego.getVehicleType().getWidth()) * 0.5;
                                newPosLat = ego.getLatOffset(ili.lane) + ego.getLateralPositionOnLane();
                                newPosLat = MAX2(-maxVehOffset, newPosLat);
                                newPosLat = MIN2(maxVehOffset, newPosLat);
                            }
                        }
                    }
                }
            }
            ego.leaveLane(NOTIFICATION_JUNCTION, nextLane);
            ego.getLaneChangeModel().cleanupShadowLane();
            ego.getLaneChangeModel().cleanupTargetLane();
            ego.setTentativeLaneAndPosition(nextLane, 0, newPosLat); // update position
            ego.enterLaneAtMove(nextLane);
            // sublane model must adapt state to the new lane
            ego.getLaneChangeModel().prepareStep();
        }
    }

    return true; // keep the device
}

bool
MSDevice_Bluelight::isInRightmostSublaneOfLane(MSVehicle& veh2) const {
    // this method is in parts the same as the one defined in GUIVehicle.cpp

    // the distance from the rightmost part of the vehicle to the right side of the edge
    const double rightSide = veh2.getRightSideOnEdge();

    // the vector contains the right starting point in meters of each sublane of the whole edge
    const std::vector<double>& sublaneSides = veh2.getLane()->getEdge().getSubLaneSides();
    
    // sublaneIndex is the index the rightmost part of our vehicle is in for the WHOLE edge
    // if we never overwrite it in the for loop below, we are in the leftmost sublane of the whole edge, therefore the index must be sublaneSides.size() - 1
    int sublaneIndex = (int)sublaneSides.size() - 1;

    for (int i = 0; i < (int)sublaneSides.size(); ++i) {
        // as soon as the sublaneSide is bigger than our rightSide position of the vehicle we know that our vehicles rightest part must be in the sublane one prior
        if (sublaneSides[i] > rightSide) {
            sublaneIndex = MAX2(i - 1, 0);
            // we must now break out of the for loop
            break;
        }
    }

    // if our sublaneIndex is the same as the index of the righmost sublane of our lane, we know that the rightest part of our vehicle is in the rightmost sublane of the lane it is in, therefore we return true
    return (sublaneIndex == veh2.getLane()->getRightmostSublane());
}


void
MSDevice_Bluelight::resetVehicle(MSVehicle* veh2, const std::string& targetTypeID) {
    MSVehicleType* targetType = MSNet::getInstance()->getVehicleControl().getVType(targetTypeID);
    //targetType is nullptr if the vehicle type has already changed to its old vehicleType
    if (targetType != nullptr) {
        #ifdef DEBUG_BLUELIGHT_RESCUELANE
            std::cout << SIMTIME << " device=" << getID() << " reset " << veh2->getID() << "\n";
        #endif

        std::vector<std::string> influencedBy = StringTokenizer(veh2->getParameter().getParameter(INFLUENCED_BY, "")).getVector();
        auto it = std::find(influencedBy.begin(), influencedBy.end(), myHolder.getID());
        if (it != influencedBy.end()) {
            influencedBy.erase(it);
            const_cast<SUMOVehicleParameter&>(veh2->getParameter()).setParameter(INFLUENCED_BY, toString(influencedBy));
        }
        if (influencedBy.size() == 0) {
            MSVehicle::Influencer& lanechange = veh2->getInfluencer();
            lanechange.setLaneChangeMode(1621);

            veh2->replaceVehicleType(targetType);
            //veh2->getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_STRATEGIC_PARAM),
            //                                        targetType->getParameter().getLCParamString(SUMO_ATTR_LCA_STRATEGIC_PARAM, "1"));
        }
    }
}

void MSDevice_Bluelight::activatedChanged(){
    MSVehicle& ego = dynamic_cast<MSVehicle&>(this->getHolder());
    MSVehicle::Influencer& influencer = ego.getInfluencer();

    if (activated) {
        // activated got set to true --> activate the special rights, that only need to be done ONCE and not on every move
        
        // violate red lights
        influencer.setSpeedMode(39);

        // change vClass to emergency
        // vClass is defined in vType, to change it for ONLY this single vehicle we need a singular type
        MSVehicleType& newType = ego.getSingularType();
        newType.setVClass(SUMOVehicleClass::SVC_EMERGENCY);
        // we need to compute a new route, when vClass changes
        ego.reroute(MSNet::getInstance()->getCurrentTimeStep(), "device:bluelightVClassChanged", ego.getBaseInfluencer().getRouterTT(ego.getRNGIndex(), ego.getVClass()));

        // set speedFactor, so that vehicle can drive up to 1.5 faster than the normal speed limit
        newType.setSpeedFactor(1.50);
    } else {
        // activated got set to false --> deactivate the special rights, that only need to be done ONCE and not on every move and reset
        // ALL influenced vehicles and reset all possibly changed vehicle parameters of the holder vehicle to the default values
        
        // reset violation of red lights back to default value
        influencer.setSpeedMode(31);

        // change the vType back to the old one: this automatically resets the vClass and the speedFactor to the normal ones
        MSVehicleType* targetType = MSNet::getInstance()->getVehicleControl().getVType(ego.getVehicleType().getOriginalID());
        if (targetType != nullptr) {
            this->getHolder().replaceVehicleType(targetType);
        }
        // we need to compute a new route, when vClass changes
        ego.reroute(MSNet::getInstance()->getCurrentTimeStep(), "device:bluelightVClassChanged", ego.getBaseInfluencer().getRouterTT(ego.getRNGIndex(), ego.getVClass()));

        // reset ALL influenced vehicles
        for (std::string vehID : myInfluencedVehicles) {
            myInfluencedVehicles.erase(vehID);
            Parameterised::Map::iterator it = myInfluencedTypes.find(vehID);
            MSVehicleControl& vc = MSNet::getInstance()->getVehicleControl();
            MSVehicle* veh2 = dynamic_cast<MSVehicle*>(vc.getVehicle(vehID));
            if (veh2 != nullptr && it != myInfluencedTypes.end()) {
                // The vehicle gets back its old VehicleType, when the bluelight device gets deactivated
                resetVehicle(veh2, it->second);
            }
        }

        // when bluelight device was deactivated we need to restore the defaults of the holder vehicle
        ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_STRATEGIC_PARAM),
                                            ego.getVehicleType().getParameter().getLCParamString(SUMO_ATTR_LCA_STRATEGIC_PARAM, "1"));
        ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_LCA_SPEEDGAIN_LOOKAHEAD),
                                            ego.getVehicleType().getParameter().getLCParamString(SUMO_ATTR_LCA_SPEEDGAIN_LOOKAHEAD, "5"));
        try {
            ego.getLaneChangeModel().setParameter(toString(SUMO_ATTR_MINGAP_LAT),
                                            toString(ego.getVehicleType().getMinGapLat()));
        } catch (InvalidArgument&) {
            // not supported by the current laneChangeModel
        }
    }
}



bool
MSDevice_Bluelight::notifyEnter(SUMOTrafficObject& veh, MSMoveReminder::Notification reason, const MSLane* enteredLane) {
    UNUSED_PARAMETER(veh);
    #ifdef DEBUG_BLUELIGHT
        std::cout << SIMTIME << " device '" << getID() << "' notifyEnter: reason=" << toString(reason) << " enteredLane=" << Named::getIDSecure(enteredLane)  << "\n";
    #else
        UNUSED_PARAMETER(reason);
        UNUSED_PARAMETER(enteredLane);
    #endif

    return true; // keep the device
}


bool
MSDevice_Bluelight::notifyLeave(SUMOTrafficObject& veh, double /*lastPos*/, MSMoveReminder::Notification reason, const MSLane* enteredLane) {
    UNUSED_PARAMETER(veh);
    #ifdef DEBUG_BLUELIGHT
        std::cout << SIMTIME << " device '" << getID() << "' notifyLeave: reason=" << toString(reason) << " approachedLane=" << Named::getIDSecure(enteredLane) << "\n";
    #else
        UNUSED_PARAMETER(reason);
        UNUSED_PARAMETER(enteredLane);
    #endif

    return true; // keep the device
}


void
MSDevice_Bluelight::generateOutput(OutputDevice* tripinfoOut) const {
    if (tripinfoOut != nullptr) {
        tripinfoOut->openTag("bluelight");
        tripinfoOut->closeTag();
    }
}

std::string
MSDevice_Bluelight::getParameter(const std::string& key) const {
    if (key == "reactiondist") {
        return toString(myReactionDist);
    } else if (key == "mingapfactor"){
        return toString(myMinGapFactor);
    } else if (key == "activated"){
        return toString(activated);
    } else if (key == "invertDirection"){
        return toString(invertDirection);
    }
    throw InvalidArgument("Parameter '" + key + "' is not supported for device of type '" + deviceName() + "'");
}


void
MSDevice_Bluelight::setParameter(const std::string& key, const std::string& value) {
    if (key == "reactiondist") {
        double doubleValue;
        try {
            doubleValue = StringUtils::toDouble(value);
        } catch (NumberFormatException&) {
            throw InvalidArgument("Setting parameter '" + key + "' requires a number for device of type '" + deviceName() + "'");
        }
        myReactionDist = doubleValue;
    } else if (key == "mingapfactor"){
        double doubleValue;
        try {
            doubleValue = StringUtils::toDouble(value);
        } catch (NumberFormatException&) {
            throw InvalidArgument("Setting parameter '" + key + "' requires a number for device of type '" + deviceName() + "'");
        }
        myMinGapFactor = doubleValue;
    } else if (key == "activated"){
        bool boolValue;
        try {
            boolValue = StringUtils::toBool(value);
        } catch (BoolFormatException&) {
            throw InvalidArgument("Setting parameter '" + key + "' requires a bool for device of type '" + deviceName() + "'");
        }
        activated = boolValue;
        // activated got changed --> adjust vehicle
        activatedChanged();
    } else if (key == "invertDirection"){
        bool boolValue;
        try {
            boolValue = StringUtils::toBool(value);
        } catch (BoolFormatException&) {
            throw InvalidArgument("Setting parameter '" + key + "' requires a bool for device of type '" + deviceName() + "'");
        }
        invertDirection = boolValue;
        if (invertDirection){
            MSVehicle& ego = dynamic_cast<MSVehicle&>(this->getHolder());
            ego.getLaneChangeModel().changedToOpposite();
            invertDirection = false;
        }
    } else {
        throw InvalidArgument("Setting parameter '" + key + "' is not supported for device of type '" + deviceName() + "'");
    }
}


/****************************************************************************/
