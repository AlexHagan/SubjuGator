/**
 * Author: Patrick Emami
 * Date: 9/28/15
 *
 */
#ifndef SUB8_TGEN_MANAGER
#define SUB8_TGEN_MANAGER

#include "ompl/base/ProblemDefinition.h"
#include "ompl/base/Planner.h"
#include "ompl/base/State.h"
#include "sub8_space_information.h"
#include "sub8_msgs/Waypoint.h"
#include "sub8_msgs/Trajectory.h"

using ompl::base::PlannerPtr;
using ompl::base::State;
using ompl::base::ProblemDefinition;
using ompl::control::SpaceInformationPtr;

namespace sub8 {

namespace trajectory_generator {

class Sub8TGenManager;
// typedefs
typedef boost::shared_ptr<Sub8TGenManager> Sub8TGenManagerPtr;
typedef boost::shared_ptr<ProblemDefinition> ProblemDefinitionPtr;

// Mediates communication between ROS and the TGEN
class Sub8TGenManager {
 public:
  // The planner param will be passed in from the
  // param server. Instantiates a SpaceInformation
  // obj and the Planner
  Sub8TGenManager(int planner);

  // Create an ompl::base::ProblemDefinition object for planning a trajectory
  // from start_state to goal_state
  void setProblemDefinition(const State* start_state, const State* goal_state); 

  // Call the Planner's solve function, returning a flag marking success
  // or failure to the caller
  bool solve();

  // The traversability map has been updated, so check whether the current
  // trajectory
  // is still valid by calling SpaceInformation's StateValidityChecker
  //
  // Automatically start replanning if the path is invalid
  void validateCurrentTrajectory();

  /////////////////////////////////////////
  // ROS->OMPL and OMPL->ROS conversions 
  /////////////////////////////////////////

  // Converts a sub8_msgs::Waypoint msg into an OMPL state object 
  State* waypointToState(const boost::shared_ptr<sub8_msgs::Waypoint>& wpoint); 

  // Converts an OMPL State obj to a sub8_msgs::Waypoint
  sub8_msgs::Waypoint stateToWaypoint(const State* state);

  // If a solution was found by the planner, then that 
  // path is converted to a sub8_msgs::Trajectory message type 
  // and returned, to be sent back in a MotionPlan service response
  // 
  // To use the trajectory in Python, the caller will need to 
  // convert from std::vector to list
  sub8_msgs::Trajectory getTrajectory(); 
 private: 
  // If the current trajectory is determined to be invalid,
  // generate a new ProblemDefinition and solve again.
  //
  // On failure, send out system alarm and default to a safety-path,
  // if available. System shutdown?
  bool replan();
  
  PlannerPtr _sub8_planner;

  SpaceInformationPtr _sub8_si;
};
}
}
#endif /* SUB8_TGEN_MANAGER */